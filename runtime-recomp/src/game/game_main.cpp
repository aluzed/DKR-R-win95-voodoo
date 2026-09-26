#include "diagnostic_log.hpp"
#include "exclusive_section.hpp"
#include "percentile_histogram.hpp"
#include "timing_export.hpp"
#if defined(DKR_TARGET_WIN95)
#include "sampler.h"
#include "switch_probe.h"
#endif
#if defined(DKR_TARGET_WIN95)
extern "C" {
#include "aspmain_hle.h"
}
#endif
#if defined(DKR_TARGET_WIN95)
extern "C" {
#include "window.h"
#include "render/glide.h"
#include "clock.h"
}
#endif
#include "game_registration.hpp"
#include "glide_renderer.hpp"
#include "revision_addresses.hpp"
#include "null_renderer.hpp"
#include "rev_a_asset_mutex.hpp"
#include "runtime_magic_codes.hpp"
#include "runtime_platform.hpp"
#include "netplay_presence.hpp"
#if DKR_RUNTIME_HAS_NETPLAY
#include "runtime_netplay.hpp"
#endif
#include "runtime_support.hpp"
#include "save_manager.hpp"
#include "startup_performance.hpp"
#include "virtual_pak.hpp"
#include "runtime_legacy_mods.hpp"
#if DKR_LEGACY_QUALIFICATION
#include "legacy_runtime_qualification.hpp"
#endif
#if DKR_RUNTIME_HAS_RT64
#include "custom_tracks.hpp"
#include "rt64_renderer.hpp"
#include "runtime_texture_packs.hpp"
#include "runtime_ui.hpp"
#include "runtime_hud_layout.hpp"
#include <SDL.h>
#endif

#include "librecomp/game.hpp"
#include "librecomp/rsp.hpp"
#include "ultramodern/config.hpp"
#include "ultramodern/ultramodern.hpp"

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdlib>
#if defined(DKR_TARGET_WIN95)
#include "win95/startup.h"
// `_commit` and `_fileno`: force Windows 95's write-behind cache and the update
// of the directory entry, without which a frozen program's log stays at zero
// bytes. See dkr_diag_commit below.
#include <io.h>
#endif
#include <cstdio>
#include <exception>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "win95/fileio.hpp"
#include "win95/sync.hpp"

#ifndef _WIN32
#include <csignal>
#include <cerrno>
#if defined(__linux__)
#include <execinfo.h>
#endif
#include <unistd.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#if !defined(DKR_TARGET_WIN95)
#include <DbgHelp.h>
#endif
#endif

extern RspUcodeFunc dkrAspMain;

/* **Is the audio microcode running?** The same one bit the renderer answers with
 * `dkr_renderer_busy`, for the same question: when no guest thread is running,
 * what has the processor? The microcode runs on ultramodern's SP task thread,
 * outside every guest thread, so on one processor its time can only show up as
 * a silence. Read weakly by patch 0050 in ultramodern's threads.cpp. */
#if defined(DKR_TARGET_WIN95)
/* **The idle meter (E08-S01): how much of the processor nobody wants.**
 *
 * The frame budget leaves 17% unaccounted, time in which the idle guest thread
 * sleeps and no timed section runs. That is either spare processor or host
 * threads nobody times: the VI thread, the Glide driver, the message loop, the
 * kernel. A thread at THREAD_PRIORITY_IDLE runs only when no other thread of any
 * process is ready, so the time it gets to spin is spare time by definition.
 * It adds up the short gaps between two cycle-counter reads; a long gap means
 * it was preempted and is not counted. It takes nothing from anyone else.
 *
 * DKR_TRACE_IDLE_METER=1. Reports `[trace][idle-meter]` every five seconds of
 * cycle counter, from the meter thread itself, so a machine that is never idle
 * reports late -- which is itself the answer. */
static DWORD WINAPI IdleMeterThread(LPVOID) {
    const unsigned long long hz = dkr_cycles_hz();
    const unsigned long long preempted = hz / 100000ull;     /* 10 us */
    unsigned long long idle = 0, last = dkr_cycles_now();
    const unsigned long long start = last;
    unsigned long long next_report = start + hz * 5ull;
    for (;;) {
        const unsigned long long now = dkr_cycles_now();
        const unsigned long long gap = now - last;
        if (gap < preempted) { idle += gap; }
        last = now;
        if (now >= next_report) {
            const unsigned long long wall = now - start;
            std::fprintf(stderr, "[trace][idle-meter] idle=%llu us wall=%llu us share=%llu%%\n",
                         idle / (hz / 1000000ull), wall / (hz / 1000000ull),
                         wall ? (100ull * idle) / wall : 0ull);
            next_report = now + hz * 5ull;
        }
    }
    return 0;
}

static void StartIdleMeter() {
    if (std::getenv("DKR_TRACE_IDLE_METER") == nullptr || !dkr_cycles_init()) {
        return;
    }
    DWORD id = 0;
    HANDLE thread = CreateThread(nullptr, 0, IdleMeterThread, nullptr, 0, &id);
    if (thread != nullptr) {
        SetThreadPriority(thread, THREAD_PRIORITY_IDLE);
        CloseHandle(thread);
        std::fprintf(stderr, "[boot][idle-meter] started at %llu Hz\n", dkr_cycles_hz());
    }
}
#endif

static std::atomic<int> g_audio_busy{0};

extern "C" int dkr_audio_busy(void) {
    return g_audio_busy.load(std::memory_order_relaxed);
}

/* **Does the audio microcode's cost follow the sound played, or the frames
 * drawn?** At 8 fps the game submits 1.52 audio tasks a frame, which suggests the
 * former; if so, the microcode is a fixed share of the processor at any frame rate
 * and E03-S03 comes first. The answer is read from `[audio][rate]`, which sets the
 * cumulative cost against the samples handed to the audio interface and against
 * the display lists drawn, every five seconds of wall clock. */
static std::atomic<unsigned long long> g_audio_samples_queued{0};
static std::atomic<unsigned> g_audio_frequency{0};
extern "C" unsigned long long dkr_display_lists_drawn(void);
extern "C" void dkr_osd_audio_task(unsigned long us);

/* **Audio task capture, the oracle's input (E03-S03).**
 *
 * `DKR_AUDIO_CAPTURE=first,step,count` writes `count` audio tasks, starting at
 * task `first` and every `step` after it, to `D:\AUDnnn.BIN`. Each file holds
 * the RSP's DMEM and the low four megabytes of RDRAM, both **before** the
 * microcode runs and **after**, so that a host harness can replay the task
 * through the same recompiled `dkrAspMain` and check two things: that the host
 * reproduces the target bit for bit, and how far a high-level mixer strays
 * from it. Four megabytes because DKR never writes above that line
 * (`[trace][snap-extent]`).
 *
 * Layout, little-endian: "DKRA", version 1, ucode address, RDRAM bytes,
 * then DMEM[4096], RDRAM[n] before, DMEM[4096], RDRAM[n] after. Both memories
 * are stored exactly as the runtime holds them, byte-swapped words included. */
constexpr std::uint32_t kAudioCaptureRdramBytes = 0x400000u;
struct AudioCapturePlan {
    unsigned long first = 0, step = 1, count = 0;
};
static AudioCapturePlan ReadAudioCapturePlan() {
    AudioCapturePlan plan;
    if (const char* v = std::getenv("DKR_AUDIO_CAPTURE")) {
        std::sscanf(v, "%lu,%lu,%lu", &plan.first, &plan.step, &plan.count);
        if (plan.step == 0) { plan.step = 1; }
    }
    return plan;
}
static const AudioCapturePlan g_audio_capture = ReadAudioCapturePlan();

static void WriteAudioCapturePart(std::FILE* f, const std::uint8_t* rdram) {
    std::fwrite(dmem, 1, 0x1000, f);
    std::fwrite(rdram, 1, kAudioCaptureRdramBytes, f);
}

static void CountAndQueueAudio(std::int16_t* samples, std::size_t sample_count) {
    g_audio_samples_queued.fetch_add(sample_count, std::memory_order_relaxed);
    dkr::runtime::platform::queue_audio(samples, sample_count);
}

static void RecordAndSetAudioFrequency(std::uint32_t frequency) {
    g_audio_frequency.store(frequency, std::memory_order_relaxed);
    dkr::runtime::platform::set_audio_frequency(frequency);
}

namespace {

#ifdef _WIN32
std::atomic_flag g_crash_filter_active = ATOMIC_FLAG_INIT;
std::filesystem::path g_crash_directory;
#endif

bool ConfigurePersistentRuntimeLog(
    const std::filesystem::path& config_directory) {
    if (!dkr::runtime::support::diagnostic_logging_enabled()) {
        return true;
    }
    std::error_code error;
    const std::filesystem::path& log_directory =
        dkr::runtime::support::log_directory();
    dkr::fs::create_directories(log_directory, error);
    if (error) {
        return false;
    }
    const std::filesystem::path current = log_directory / "runtime.log";
    const std::filesystem::path previous =
        log_directory / "runtime-previous.log";
    dkr::fs::remove(previous, error);
    error.clear();
    if (dkr::fs::exists(current, error)) {
        error.clear();
        dkr::fs::rename(current, previous, error);
    }
    // **Say where the rest of the log went.**
    //
    // `RedirectDiagnosticsToFile` has already pointed stderr at `DKRR.LOG`, and
    // this call takes it away. Without this line that file ends after the two
    // startup marks, which is exactly what it looks like when the process dies
    // at `support-configure` -- an instrument that stops recording and does not
    // say so. Written before the handover, so it lands in the file being left.
    std::fprintf(stderr,
                 "[boot][log] diagnostics continue in %s\n",
                 current.string().c_str());
    std::fflush(stderr);
#ifdef _WIN32
    FILE* stream = nullptr;
    if (_wfreopen_s(&stream, current.c_str(), L"w", stderr) != 0 ||
        stream == nullptr) {
        return false;
    }
#else
    if (std::freopen(current.c_str(), "w", stderr) == nullptr) {
        return false;
    }
#endif
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::fprintf(stderr, "[boot] DKR-R %s persistent runtime log\n",
                 DKR_RELEASE_VERSION);
    return true;
}

std::filesystem::path DefaultConfigDirectory(const char* executable_argument) {
    std::error_code error;
    const std::filesystem::path executable = dkr::fs::absolute(
        std::filesystem::u8path(executable_argument), error);
    const std::filesystem::path executable_directory = error
        ? dkr::fs::current_path()
        : executable.parent_path();
    if (dkr::fs::exists(executable_directory / "portable.txt")) {
        return executable_directory / "dkr-runtime-data";
    }
#if defined(_WIN32)
    if (const char* app_data = std::getenv("APPDATA"); app_data != nullptr && *app_data != '\0') {
        return std::filesystem::path(app_data) / "DKRPort";
    }
#else
    if (const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
        xdg_config != nullptr && *xdg_config != '\0') {
        return std::filesystem::path(xdg_config) / "dkr-port";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".config" / "dkr-port";
    }
#endif
    return executable_directory / "dkr-runtime-data";
}

RspExitReason EmptyAudioTask(std::uint8_t*, std::uint32_t) {
    return RspExitReason::Broke;
}

RspUcodeFunc* GetRspMicrocode(const OSTask* task) {
    /* Whether any RSP task arrives at all, said once. Without it, "no audio cost"
       and "no audio task" are the same silence. */
    static bool announced = false;
    if (!announced) {
        announced = true;
        std::fprintf(stderr, "[audio][cost] first RSP task: type=%u data_size=%u\n",
                     task->t.type, task->t.data_size);
    }
    if (task->t.type == M_AUDTASK &&
        task->t.ucode == dkr::runtime::revision_addresses::AspMainTextStart) {
        // DKR can submit a zero-command audio frame when the host-reported AI
        // queue already satisfies the synthesizer's requested frame size. The
        // original scheduler treats that as completed work; entering the ABI
        // dispatcher with a zero-byte list would DMA and execute stale memory.
        if (task->t.data_size == 0) {
            return EmptyAudioTask;
        }
        // **What the audio microcode costs, per frame.**
        //
        // E00-S03's verdict says in one place that "the audio is in none of these
        // numbers" and its own table says three lines above that the 147 ms is
        // "recompiled code, scheduler, **audio**". Both cannot be true, and this
        // dispatcher settles which: `dkrAspMain` is wired and reached, so the
        // microcode does run inside the measured frame.
        //
        // That matters for what the verdict concludes rather than for its
        // arithmetic. The ceiling of 22.2 fps assumes E08-S02 *optimising* the
        // recompiled code. E03-S03 does not optimise this part - it **replaces**
        // it with a high-level mixer, because E00-S04 measured the microcode path
        // at 3.9 % of the throughput it needs. Work that is going to be deleted
        // does not belong in a ceiling computed over work that has to be kept.
        //
        // So the cost is measured rather than argued about.
        return +[](std::uint8_t* rdram, std::uint32_t ucode_address) {
            static unsigned long long calls = 0, total_us = 0;
            static unsigned long captured = 0;
            unsigned long long dt = 0;
            RspExitReason r;
            std::FILE* capture = nullptr;
            if (captured < g_audio_capture.count && calls >= g_audio_capture.first &&
                (calls - g_audio_capture.first) % g_audio_capture.step == 0) {
                char path[32];
                std::snprintf(path, sizeof(path), "D:\\AUD%03lu.BIN", captured);
                capture = std::fopen(path, "wb");
                if (capture != nullptr) {
                    const std::uint32_t header[4] = {0x41524B44u, 1u, ucode_address,
                                                     kAudioCaptureRdramBytes};
                    std::fwrite(header, sizeof(header), 1, capture);
                    WriteAudioCapturePart(capture, rdram);
                }
            }
            {
                const dkr::runtime::ExclusiveSection exclusive;
                const unsigned long long t0 = dkr_clock_now_us();
                g_audio_busy.store(1, std::memory_order_relaxed);
#if defined(DKR_TARGET_WIN95)
                // E03-S03. The high-level mixer, bit-exact against the
                // microcode, unless DKR_AUDIO_MICROCODE asks for the microcode.
                static const bool use_microcode = std::getenv("DKR_AUDIO_MICROCODE") != nullptr;
                static bool announced_path = false;
                if (!announced_path) {
                    announced_path = true;
                    std::fprintf(stderr, "[audio][path] %s\n",
                                 use_microcode ? "recompiled microcode (DKR_AUDIO_MICROCODE)"
                                               : "high-level mixer (platform/audio/aspmain_hle.c)");
                }
                // DKR_TRACE_AUDIO_ZONES: the mixer's cost per command, on the
                // cycle counter (platform/win95/clock.h), reported every 200
                // tasks as total microseconds / calls.
                static const bool audio_zones =
                    std::getenv("DKR_TRACE_AUDIO_ZONES") != nullptr && dkr_cycles_init();
                if (audio_zones && dkr_aspmain_hle_clock == nullptr) {
                    dkr_aspmain_hle_clock = +[]() -> unsigned long long { return dkr_cycles_now(); };
                }
                if (use_microcode) {
                    r = dkrAspMain(rdram, ucode_address);
                } else {
                    (void)dkr_aspmain_hle(rdram, dmem);
                    r = RspExitReason::Broke;
                }
                if (audio_zones && (calls % 200ull) == 199ull) {
                    static const char* const kNames[16] = {
                        "spnoop", "adpcm", "clearbuff", "envmixer", "loadbuff", "resample",
                        "savebuff", "segment", "setbuff", "setvol", "dmemmove", "loadadpcm",
                        "mixer", "interleave", "polef", "setloop"};
                    const unsigned long long hz = dkr_cycles_hz();
                    std::fprintf(stderr, "[audio][zones] tasks=%llu", calls + 1);
                    for (int c = 0; c < 16; c++) {
                        if (dkr_aspmain_hle_calls[c] != 0) {
                            std::fprintf(stderr, " %s=%llu/%lu", kNames[c],
                                         dkr_aspmain_hle_ticks[c] * 1000ull / (hz / 1000ull),
                                         dkr_aspmain_hle_calls[c]);
                        }
                    }
                    std::fprintf(stderr, " us\n");
                }
#else
                r = dkrAspMain(rdram, ucode_address);
#endif
                g_audio_busy.store(0, std::memory_order_relaxed);
                dt = dkr_clock_now_us() - t0;
            }
            if (capture != nullptr) {
                WriteAudioCapturePart(capture, rdram);
                std::fclose(capture);
                std::fprintf(stderr, "[audio][capture] task %llu -> AUD%03lu.BIN\n",
                             calls, captured);
                captured++;
            }
            calls++;
            total_us += dt;
            static dkr::runtime::PercentileHistogram audio_hist{250};
            audio_hist.add(dt);
            dkr_osd_audio_task(static_cast<unsigned long>(dt));
            // AUDIO.BIN: the task's wall time, a reserved zero, and the samples queued
            // since boot. The record's time is the task's end.
            static dkr::runtime::TimingExport audio_export{"AUDIO.BIN"};
            audio_export.add(static_cast<std::uint32_t>(dkr_clock_now_us() / 1000ULL),
                             static_cast<std::uint32_t>(dt), 0u,
                             static_cast<std::uint32_t>(g_audio_samples_queued.load()));
            {
                static unsigned long long last_rate_us = 0;
                const unsigned long long now = dkr_clock_now_us();
                if (calls == 1ull || now - last_rate_us >= 5000000ull) {
                    last_rate_us = now;
                    std::fprintf(stderr,
                                 "[audio][rate] wall=%llu us calls=%llu cost=%llu us "
                                 "samples=%llu freq=%u dls=%llu\n",
                                 now, calls, total_us,
                                 (unsigned long long)g_audio_samples_queued.load(),
                                 g_audio_frequency.load(),
                                 dkr_display_lists_drawn());
                }
            }
            /* A line at the **first** event, then sparsely. The first version
               reported every fiftieth call and produced nothing at all, which
               reads identically to an audio path that is never reached - the
               failure `cpu-budget.md` records as this project's most repeated,
               and which it names the cheap fix for. */
            if (calls <= 3ull || (calls % 50ull) == 0ull) {
                std::fprintf(stderr,
                             "[audio][cost] calls=%llu total=%llu us mean=%llu us\n",
                             calls, total_us, total_us / calls);
            }
            // Median and 99th percentile per task over windows of 300 tasks
            // (about 10 s of sound), not from boot (E08-S01).
            if (audio_hist.count() >= 300) {
                std::fprintf(stderr,
                             "[audio][percentiles] window=%lu p50=%llu p99=%llu us\n",
                             audio_hist.count(), audio_hist.percentile(500),
                             audio_hist.percentile(990));
                audio_hist.reset();
            }
            return r;
        };
    }
    std::fprintf(stderr,
                 "[boot][rsp] unsupported task type=%u flags=0x%08X "
                 "ucode=0x%08X ucode_size=%u ucode_data=0x%08X data=0x%08X data_size=%u\n",
                 task->t.type, task->t.flags, task->t.ucode, task->t.ucode_size,
                 task->t.ucode_data, task->t.data_ptr, task->t.data_size);
    return nullptr;
}

void MessageBox(const char* message) {
    std::fprintf(stderr, "[boot][runtime-error] %s\n", message);
}

std::string GetThreadName(const OSThread* thread) {
    return "DKR-" + std::to_string(thread->id);
}

// This filter rests on DbgHelp - SymInitialize, StackWalk64, SymFromAddr - to
// walk a symbolised stack. Windows 95 does not have it: its `imagehlp.dll` carries
// a far earlier API, without any of those functions, and the link fails before
// running it is even a question.
//
// The target is not deprived for all that. `platform/win95/startup.c` already
// installs its own filter, which runs the cleanup registry before displaying
// anything at all - it is what restores the video mode and releases the Voodoo
// hardware, which counts for more on this machine than a call stack. Without this
// guard, the installation below would overwrite it:
// `SetUnhandledExceptionFilter` keeps only the last caller.
#if defined(_WIN32) && !defined(DKR_TARGET_WIN95)
void WriteWindowsMinidump(EXCEPTION_POINTERS* exception) {
    if (!dkr::runtime::support::crash_dumps_enabled() ||
        g_crash_directory.empty()) {
        return;
    }
    SYSTEMTIME time{};
    GetSystemTime(&time);
    wchar_t name[96]{};
    swprintf_s(name, L"DKR-R-crash-%04u%02u%02u-%02u%02u%02u.dmp",
               time.wYear, time.wMonth, time.wDay,
               time.wHour, time.wMinute, time.wSecond);
    const std::filesystem::path path = g_crash_directory / name;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                              nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION information{};
    information.ThreadId = GetCurrentThreadId();
    information.ExceptionPointers = exception;
    information.ClientPointers = FALSE;
    const BOOL written = MiniDumpWriteDump(
        GetCurrentProcess(), GetCurrentProcessId(), file,
        MiniDumpWithIndirectlyReferencedMemory,
        exception != nullptr ? &information : nullptr, nullptr, nullptr);
    CloseHandle(file);
    std::fprintf(stderr, "[boot][crash] minidump=%ls status=%s\n",
                 path.c_str(), written ? "written" : "failed");
}

LONG WINAPI RuntimeCrashFilter(EXCEPTION_POINTERS* exception) {
    if (g_crash_filter_active.test_and_set()) {
        Sleep(5000);
        return EXCEPTION_EXECUTE_HANDLER;
    }
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);

    const DWORD64 fault_address = reinterpret_cast<DWORD64>(exception->ExceptionRecord->ExceptionAddress);
    const DWORD64 module_base = reinterpret_cast<DWORD64>(GetModuleHandleW(nullptr));
    std::fprintf(stderr, "[boot][crash] exception=0x%08lX address=0x%016llX\n",
                 exception->ExceptionRecord->ExceptionCode,
                 static_cast<unsigned long long>(fault_address));
    WriteWindowsMinidump(exception);
    if (exception->ExceptionRecord->NumberParameters >= 2U) {
        const ULONG_PTR operation = exception->ExceptionRecord->ExceptionInformation[0];
        const char* operation_name = operation == 0U ? "read" :
            operation == 1U ? "write" : operation == 8U ? "execute" : "unknown";
        std::fprintf(stderr,
                     "[boot][crash] memory-operation=%s(%llu) "
                     "memory-address=0x%016llX\n",
                     operation_name,
                     static_cast<unsigned long long>(operation),
                     static_cast<unsigned long long>(
                         exception->ExceptionRecord->ExceptionInformation[1]));
    }
    std::fprintf(stderr, "[boot][crash] module-base=0x%016llX rva=0x%llX\n",
                 static_cast<unsigned long long>(module_base),
                 static_cast<unsigned long long>(fault_address - module_base));
#if defined(_M_AMD64) || defined(__x86_64__)
    std::fprintf(stderr,
                 "[boot][crash] registers rcx=0x%016llX rdx=0x%016llX "
                 "r8=0x%016llX r9=0x%016llX rsp=0x%016llX\n",
                 static_cast<unsigned long long>(exception->ContextRecord->Rcx),
                 static_cast<unsigned long long>(exception->ContextRecord->Rdx),
                 static_cast<unsigned long long>(exception->ContextRecord->R8),
                 static_cast<unsigned long long>(exception->ContextRecord->R9),
                 static_cast<unsigned long long>(exception->ContextRecord->Rsp));
    std::fprintf(stderr,
                 "[boot][crash] registers rax=0x%016llX rbx=0x%016llX "
                 "rbp=0x%016llX rsi=0x%016llX rdi=0x%016llX\n",
                 static_cast<unsigned long long>(exception->ContextRecord->Rax),
                 static_cast<unsigned long long>(exception->ContextRecord->Rbx),
                 static_cast<unsigned long long>(exception->ContextRecord->Rbp),
                 static_cast<unsigned long long>(exception->ContextRecord->Rsi),
                 static_cast<unsigned long long>(exception->ContextRecord->Rdi));
    std::fprintf(stderr,
                 "[boot][crash] registers r10=0x%016llX r11=0x%016llX "
                 "r12=0x%016llX r13=0x%016llX r14=0x%016llX "
                 "r15=0x%016llX rip=0x%016llX\n",
                 static_cast<unsigned long long>(exception->ContextRecord->R10),
                 static_cast<unsigned long long>(exception->ContextRecord->R11),
                 static_cast<unsigned long long>(exception->ContextRecord->R12),
                 static_cast<unsigned long long>(exception->ContextRecord->R13),
                 static_cast<unsigned long long>(exception->ContextRecord->R14),
                 static_cast<unsigned long long>(exception->ContextRecord->R15),
                 static_cast<unsigned long long>(exception->ContextRecord->Rip));
#else
    // The same report for a 32-bit x86. The registers do not have the same names
    // in CONTEXT - Eax and not Rax - and there are eight instead of sixteen. This
    // is not a degradation: these are the registers the machine has.
    std::fprintf(stderr,
                 "[boot][crash] registers eax=0x%08lX ebx=0x%08lX "
                 "ecx=0x%08lX edx=0x%08lX\n",
                 static_cast<unsigned long>(exception->ContextRecord->Eax),
                 static_cast<unsigned long>(exception->ContextRecord->Ebx),
                 static_cast<unsigned long>(exception->ContextRecord->Ecx),
                 static_cast<unsigned long>(exception->ContextRecord->Edx));
    std::fprintf(stderr,
                 "[boot][crash] registers esi=0x%08lX edi=0x%08lX "
                 "ebp=0x%08lX esp=0x%08lX eip=0x%08lX\n",
                 static_cast<unsigned long>(exception->ContextRecord->Esi),
                 static_cast<unsigned long>(exception->ContextRecord->Edi),
                 static_cast<unsigned long>(exception->ContextRecord->Ebp),
                 static_cast<unsigned long>(exception->ContextRecord->Esp),
                 static_cast<unsigned long>(exception->ContextRecord->Eip));
#endif

    CONTEXT context = *exception->ContextRecord;
    STACKFRAME64 frame{};
#if defined(_M_AMD64) || defined(__x86_64__)
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
#else
    frame.AddrPC.Offset = context.Eip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Ebp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Esp;
#endif
    frame.AddrStack.Mode = AddrModeFlat;

    alignas(SYMBOL_INFO) unsigned char symbol_storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_storage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;

    for (unsigned index = 0; index < 32; ++index) {
        // The machine type must follow the architecture, otherwise StackWalk64
        // reads the frame with the wrong widths and walks a stack of fanciful
        // values - which is worse than no stack at all.
#if defined(_M_AMD64) || defined(__x86_64__)
        constexpr DWORD kMachineType = IMAGE_FILE_MACHINE_AMD64;
#else
        constexpr DWORD kMachineType = IMAGE_FILE_MACHINE_I386;
#endif
        if (!StackWalk64(kMachineType, process, GetCurrentThread(), &frame,
                         &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
            frame.AddrPC.Offset == 0) {
            break;
        }
        DWORD64 displacement = 0;
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) {
            std::fprintf(stderr, "[boot][crash] #%u %s+0x%llX\n", index, symbol->Name,
                         static_cast<unsigned long long>(displacement));
        } else {
            std::fprintf(stderr, "[boot][crash] #%u 0x%016llX\n", index,
                         static_cast<unsigned long long>(frame.AddrPC.Offset));
        }
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD line_displacement = 0;
        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &line_displacement, &line)) {
            std::fprintf(stderr, "[boot][crash]     %s:%lu+0x%lX\n",
                         line.FileName, line.LineNumber, line_displacement);
        }
    }
    SymCleanup(process);
    return EXCEPTION_EXECUTE_HANDLER;
}

// `#elif !defined(_WIN32)` and not a bare `#else`: the guard above excludes
// Windows 95 from the DbgHelp filter, and a plain else would then hand this
// target the POSIX branch -- which wants `sigaction`, a function it does not
// have. Windows 95 takes neither: `platform/win95/startup.c` installs its own
// filter, and that one runs the cleanup registry.
#elif !defined(_WIN32)

void RuntimeSignalHandler(int signal_number) {
    std::fprintf(stderr, "[boot][crash] signal=%d\n", signal_number);
#if defined(__linux__)
    void* frames[48]{};
    const int count = backtrace(frames, 48);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
#endif
    std::fflush(stderr);
    _exit(128 + signal_number);
}

void InstallRuntimeSignalHandlers() {
    struct sigaction action {};
    action.sa_handler = RuntimeSignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESETHAND;
    for (const int signal_number : {SIGSEGV, SIGABRT, SIGFPE, SIGILL}) {
        sigaction(signal_number, &action, nullptr);
    }
}

#endif

struct LaunchOptions {
    std::filesystem::path rom_path;
    std::filesystem::path config_directory;
    unsigned timeout_seconds = 0;
};

bool ParseLaunchOptions(int argc, char** argv, LaunchOptions& options,
                        std::string& error) {
    unsigned positional_index = 0;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        auto require_value = [&](const char* option) -> const char* {
            if (index + 1 >= argc) {
                error = std::string(option) + " requires a value.";
                return nullptr;
            }
            return argv[++index];
        };

        if (argument == "--rom") {
            const char* value = require_value("--rom");
            if (value == nullptr) {
                return false;
            }
            options.rom_path = std::filesystem::u8path(value);
        } else if (argument == "--config") {
            const char* value = require_value("--config");
            if (value == nullptr) {
                return false;
            }
            options.config_directory = std::filesystem::u8path(value);
        } else if (argument == "--timeout") {
            const char* value = require_value("--timeout");
            if (value == nullptr) {
                return false;
            }
            try {
                const unsigned long parsed = std::stoul(value);
                if (parsed > std::numeric_limits<unsigned>::max()) {
                    throw std::out_of_range("timeout");
                }
                options.timeout_seconds = static_cast<unsigned>(parsed);
            } catch (...) {
                error = "--timeout requires a non-negative whole number.";
                return false;
            }
        } else if (argument.starts_with("--")) {
            error = "Unknown DKR-R option: " + std::string(argument);
            return false;
        } else {
            // Preserve the original positional invocation for developer and
            // diagnostic scripts: ROM, config directory, optional timeout.
            if (positional_index == 0U) {
                options.rom_path = std::filesystem::u8path(argv[index]);
            } else if (positional_index == 1U) {
                options.config_directory = std::filesystem::u8path(argv[index]);
            } else if (positional_index == 2U) {
                try {
                    const unsigned long parsed = std::stoul(argv[index]);
                    if (parsed > std::numeric_limits<unsigned>::max()) {
                        throw std::out_of_range("timeout");
                    }
                    options.timeout_seconds = static_cast<unsigned>(parsed);
                } catch (...) {
                    error = "The timeout must be a non-negative whole number.";
                    return false;
                }
            } else {
                error = "Too many positional arguments.";
                return false;
            }
            ++positional_index;
        }
    }
    if (options.config_directory.empty()) {
        options.config_directory = DefaultConfigDirectory(argv[0]);
    }
    return true;
}

bool PrepareCanonicalRomPath(std::filesystem::path& rom_path,
                             dkr::runtime::rom::Identity& identity,
                             const std::filesystem::path& config_directory,
                             std::string& error) {
    if (identity.byte_order == dkr::runtime::rom::ByteOrder::BigEndian) {
        return true;
    }
    std::filesystem::path canonical_path;
    if (!dkr::runtime::rom::materialize_canonical(
            rom_path, identity, config_directory / "rom-cache",
            canonical_path, error)) {
        return false;
    }
    rom_path = canonical_path;
    identity = dkr::runtime::rom::inspect(rom_path);
    if (!identity.supported() ||
        identity.byte_order != dkr::runtime::rom::ByteOrder::BigEndian) {
        error = "The prepared ROM cache did not retain the selected revision.";
        return false;
    }
    std::fprintf(stderr,
                 "[boot][rom] normalised selected ROM into the local big-endian cache\n");
    error.clear();
    return true;
}

} // namespace

bool RelaunchApplication(int argc, char** argv) {
#ifdef _WIN32
    (void)argc;
    (void)argv;
    // The wide family is a stub under Windows 9x: `CreateProcessW` returns 0
    // there without doing anything, and the quick restart would fail in silence -
    // the worst case, since the program loads and appears to work.
    //
    // The narrow form does the same thing everywhere else, so it is used
    // everywhere: two paths of which only one is exercised are worth less than a
    // single path.
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::string command_line = GetCommandLineA();
    if (command_line.empty() ||
        !CreateProcessA(nullptr, command_line.data(), nullptr, nullptr, FALSE,
                        0, nullptr, nullptr, &startup, &process)) {
        std::fprintf(stderr, "[boot][restart] CreateProcessA failed: %lu\n",
                     static_cast<unsigned long>(GetLastError()));
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#else
    if (argc <= 0 || argv == nullptr || argv[0] == nullptr) {
        return false;
    }
    std::error_code error;
    const std::filesystem::path executable =
        dkr::fs::absolute(std::filesystem::u8path(argv[0]), error);
    const std::string executable_utf8 = error
        ? std::string(argv[0])
        : executable.string();
    execv(executable_utf8.c_str(), argv);
    std::fprintf(stderr, "[boot][restart] execv failed: errno=%d\n", errno);
    return false;
#endif
}

#if defined(DKR_TARGET_WIN95)
// **Redirect stderr to a file, on this target only.**
//
// The whole runtime log goes through stderr - including the [boot][crash]
// handler, which prints exception code, address, module base and RVA. But Windows
// 95's COMMAND.COM **cannot redirect stderr**: it has no `2>&1` syntax. Without
// this outlet, the only way to read a crash on the target machine is to photograph
// a console window and transcribe its contents by hand.
//
// **The call is in `DkrMain` and not in `main`**, because the entry point under
// Windows is `WinMain`: a first version had placed it in the `#else` branch, where
// it was never compiled. The symptom was mute - the program ran, the file did not
// appear, and nothing said why.
//
// `DKR_LOG` allows its location to be changed; by default the file lands in the
// current directory.
static void RedirectDiagnosticsToFile() {
    const char* path = std::getenv("DKR_LOG");
    if (path == nullptr || path[0] == '\0') {
        path = "DKRR.LOG";
    }
    if (std::freopen(path, "w", stderr) != nullptr) {
        // Unbuffered: a crash leaves no time to flush a buffer, and it is
        // precisely that message which matters most.
        std::setvbuf(stderr, nullptr, _IONBF, 0);
    }
}

// **Making the log readable for a program that does not terminate.**
//
// `_IONBF` suffices for a crash, because the exception filter closes the stream
// before writing. It does not suffice for a **hang**: Windows 95 only updates the
// size in the directory when the file is closed, and the write-behind cache holds
// the sectors. A frozen program therefore leaves a zero-byte log, whatever it
// wrote.
//
// The symptom is cruel: one reads "zero bytes" and concludes the program produced
// nothing, hence that it stopped early - when it may have written tens of
// thousands of lines. Two opposite diagnoses behind the same observation.
//
// `_commit` calls `FlushFileBuffers`, present in Windows 95, which forces the
// cache **and** the update of the directory entry. Called now and then, it makes
// the log readable while the program is still running - and that is the only way
// to observe a hang from the outside.
extern "C" void dkr_diag_commit(void) {
    std::fflush(stderr);
    {
        const int fd = _fileno(stderr);
        if (fd >= 0) { _commit(fd); }
    }
}
#else
extern "C" void dkr_diag_commit(void) { std::fflush(stderr); }
#endif

namespace dkr::runtime {

// The renderer choice, through `DKR_RENDERER`.
//
// The default value is Glide: it is the port's target, and a setting one must
// remember to set in order to get the normal behaviour always ends up missing
// somewhere.
//
// `DKR_RENDERER=null` keeps the diagnostic renderer, which counts display lists
// without reading them. It is not a leftover: it is the only configuration that
// starts when the card is at fault, and therefore the only way to separate a
// defect of the port from a defect of the rendering. It served to establish that
// the game reached 9822 display lists while not a pixel had yet been written.
std::unique_ptr<ultramodern::renderer::RendererContext> SelectRenderContext(
    std::uint8_t* rdram,
    ultramodern::renderer::WindowHandle window_handle,
    bool developer_mode) {
    const char* choice = std::getenv("DKR_RENDERER");
    if (choice != nullptr && std::string_view{choice} == "null") {
        std::fprintf(stderr, "[boot][gfx] diagnostic renderer (DKR_RENDERER=null)\n");
        return CreateDiagnosticRenderer(rdram, window_handle, developer_mode);
    }
    return CreateGlideRenderer(rdram, window_handle, developer_mode);
}

} // namespace dkr::runtime

int DkrMain(int argc, char** argv) {
#if defined(DKR_TARGET_WIN95)
    RedirectDiagnosticsToFile();
    // **Install the startup layer, which carries the exception filter.**
    //
    // `RuntimeCrashFilter`'s comment above disables the runtime's filter on this
    // target, explaining that `platform/win95/startup.c` "already installs its own
    // filter". That was true of the platform witness and false of the game:
    // `dkr_win95_startup` was only called by `witness.c`.
    //
    // The game therefore ran **with no exception filter at all**. The symptom
    // observed on the machine: a Windows "illegal operation" box, no trace in the
    // log, and the video mode not restored - that last point being the most
    // serious, the Voodoo card holding the screen through its analogue relay.
    {
        const int rc = dkr_win95_startup("DKR-R");
        if (rc != DKR_WIN95_STARTUP_OK) {
            std::fprintf(stderr, "[boot][win95] startup refused: %d\n", rc);
            return 5;
        }
    }
#endif
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    dkr::runtime::startup_performance::mark("process-entry");
#if defined(_WIN32) && !defined(DKR_TARGET_WIN95)
    // On Windows 95 it is the platform layer's filter that stays in place: it
    // runs the cleanup registry, which this one does not have.
    SetUnhandledExceptionFilter(RuntimeCrashFilter);
#endif

    if (argc >= 2 && std::string_view(argv[1]) == "--self-test-pak") {
        const std::filesystem::path test_directory = argc >= 3
            ? std::filesystem::u8path(argv[2])
            : DefaultConfigDirectory(argv[0]) / "pak-self-test";
        std::string error;
        if (!dkr::runtime::pak::self_test(test_directory, error)) {
            std::fprintf(stderr, "[test][pak] FAILED: %s\n", error.c_str());
            return 1;
        }
        std::fprintf(stderr, "[test][pak] PASS: round-trip and backup recovery\n");
        return 0;
    }

#if DKR_RUNTIME_HAS_RT64
    if (argc >= 3 && std::string_view(argv[1]) == "--self-test-hud-settings") {
        const bool passed = dkr::runtime::hud::self_test_basic_settings(std::filesystem::u8path(argv[2]));
        std::fprintf(stderr,"[test][hud-settings] %s: preset switching, restart, custom suppression and save failure\n",passed?"PASS":"FAILED");
        return passed ? 0 : 1;
    }
    if (argc >= 2 &&
        std::string_view(argv[1]) == "--self-test-input-switch") {
        const std::filesystem::path test_directory = argc >= 3
            ? std::filesystem::u8path(argv[2])
            : DefaultConfigDirectory(argv[0]) / "input-switch-self-test";
        dkr::fs::create_directories(test_directory);
        dkr::runtime::platform::configure_input(test_directory);
        dkr::runtime::platform::set_requested_input_backend(
            dkr::runtime::platform::InputBackend::SDL2Compatibility);
        if (!dkr::runtime::platform::initialise()) {
            std::fprintf(stderr,
                         "[test][input-switch] FAILED: platform initialization\n");
            return 1;
        }
        constexpr Uint32 kInvariantSubsystems = SDL_INIT_VIDEO | SDL_INIT_AUDIO;
        constexpr Uint32 kControllerSubsystems =
            SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC | SDL_INIT_SENSOR;
        const auto fail = [](const char* reason) {
            std::fprintf(stderr, "[test][input-switch] FAILED: %s (%s)\n",
                         reason,
                         dkr::runtime::platform::input_backend_detail().c_str());
            dkr::runtime::platform::shutdown();
            return 1;
        };
        const auto wait_for_backend = [](auto expected) {
            const auto deadline = std::chrono::steady_clock::now() +
                std::chrono::seconds(5);
            do {
                dkr::runtime::platform::pump_input_backend_events();
                if (dkr::runtime::platform::active_input_backend() == expected &&
                    !dkr::runtime::platform::input_backend_switch_pending()) {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            } while (std::chrono::steady_clock::now() < deadline);
            return false;
        };
        for (int cycle = 0; cycle < 2; ++cycle) {
            dkr::runtime::platform::set_requested_input_backend(
                dkr::runtime::platform::InputBackend::SDL3Native);
            if (!wait_for_backend(
                    dkr::runtime::platform::InputBackend::SDL3Native)) {
                return fail("SDL2 to SDL3 handover");
            }
            if ((SDL_WasInit(kInvariantSubsystems) & kInvariantSubsystems) !=
                kInvariantSubsystems) {
                return fail("video or audio subsystem changed during SDL3 handover");
            }
            if ((SDL_WasInit(kControllerSubsystems) &
                 kControllerSubsystems) != 0U) {
                return fail("SDL2 retained controller subsystem ownership");
            }

            dkr::runtime::platform::set_requested_input_backend(
                dkr::runtime::platform::InputBackend::SDL2Compatibility);
            if (!wait_for_backend(
                    dkr::runtime::platform::InputBackend::SDL2Compatibility)) {
                return fail("SDL3 to SDL2 handover");
            }
            if ((SDL_WasInit(kInvariantSubsystems) & kInvariantSubsystems) !=
                kInvariantSubsystems) {
                return fail("video or audio subsystem changed during SDL2 handover");
            }
            if ((SDL_WasInit(kControllerSubsystems) &
                 kControllerSubsystems) != kControllerSubsystems) {
                return fail("SDL2 controller subsystems were not restored");
            }
        }
        dkr::runtime::platform::shutdown();
        std::fprintf(stderr,
                     "[test][input-switch] PASS: two live round trips\n");
        return 0;
    }
#endif

#if DKR_RUNTIME_HAS_RT64
    if (argc == 4 && std::string_view(argv[1]) == "--self-test-rice-pack") {
        const std::filesystem::path source = std::filesystem::u8path(argv[2]);
        const std::filesystem::path test_directory = std::filesystem::u8path(argv[3]);
        dkr::runtime::texture_packs::configure(test_directory);
        std::string status;
        if (!dkr::runtime::texture_packs::import_archive(source, status)) {
            std::fprintf(stderr, "[test][rice] FAILED: %s\n", status.c_str());
            return 1;
        }
        const auto packs = dkr::runtime::texture_packs::snapshot();
        if (packs.size() != 1 || !packs.front().compatible ||
            packs.front().format != dkr::runtime::texture_packs::Format::RiceRt64) {
            std::fprintf(stderr, "[test][rice] FAILED: converted pack did not validate natively.\n");
            return 1;
        }
        const std::string pack_id = packs.front().id;
        const std::filesystem::path managed_path = packs.front().path;
        if (!dkr::runtime::texture_packs::set_hidden(pack_id, true, status) ||
            !dkr::runtime::texture_packs::snapshot().empty()) {
            std::fprintf(stderr, "[test][rice] FAILED: hide-from-list lifecycle failed: %s\n",
                         status.c_str());
            return 1;
        }
        const auto hidden_packs = dkr::runtime::texture_packs::snapshot(true);
        if (hidden_packs.size() != 1 || !hidden_packs.front().hidden) {
            std::fprintf(stderr, "[test][rice] FAILED: hidden pack was not retained for restoration.\n");
            return 1;
        }
        if (!dkr::runtime::texture_packs::set_hidden(pack_id, false, status) ||
            dkr::runtime::texture_packs::snapshot().size() != 1) {
            std::fprintf(stderr, "[test][rice] FAILED: restore-to-list lifecycle failed: %s\n",
                         status.c_str());
            return 1;
        }
        if (!dkr::runtime::texture_packs::delete_managed(pack_id, status) ||
            !dkr::runtime::texture_packs::snapshot(true).empty() ||
            dkr::fs::exists(managed_path)) {
            std::fprintf(stderr, "[test][rice] FAILED: permanent managed deletion failed: %s\n",
                         status.c_str());
            return 1;
        }
        std::fprintf(stderr,
                     "[test][rice] PASS: import, hide, restore and permanent deletion\n");
        return 0;
    }
#endif

    LaunchOptions launch{};
    std::string rom_error;
    if (!ParseLaunchOptions(argc, argv, launch, rom_error)) {
        std::fprintf(stderr,
                     "Usage: DKR-R [rom.z64] [config-directory] [timeout-seconds]\n"
                     "       DKR-R --rom <path> [--config <path>] [--timeout <seconds>]\n"
                     "[boot][arguments] %s\n", rom_error.c_str());
        return 2;
    }
    std::filesystem::path& rom_path = launch.rom_path;
#if DKR_RUNTIME_HAS_LEGACY_MODS
    std::shared_ptr<const dkr::mods::PreparedModLaunch> prepared_mods;
#endif
    const std::filesystem::path& config_directory = launch.config_directory;
    const unsigned timeout_seconds = launch.timeout_seconds;
    dkr::fs::create_directories(config_directory);
    {
        dkr::runtime::startup_performance::ScopedPhase phase(
            "support-configure");
        dkr::runtime::support::configure(config_directory);
    }
    bool log_configured = ConfigurePersistentRuntimeLog(config_directory);
    if (!log_configured) {
        std::fprintf(stderr,
                     "[boot][log] could not create the persistent runtime log\n");
    }
    dkr::runtime::startup_performance::mark("persistent-log-ready");
    {
        dkr::runtime::startup_performance::ScopedPhase phase(
            "rom-identity-cache-configure");
        dkr::runtime::rom::configure_identity_cache(config_directory);
    }
#if defined(_WIN32)
    g_crash_directory = dkr::runtime::support::crash_dump_directory();
    if (dkr::runtime::support::crash_dumps_enabled()) {
        std::error_code crash_directory_error;
        dkr::fs::create_directories(g_crash_directory,
                                    crash_directory_error);
        if (crash_directory_error) {
            g_crash_directory.clear();
        }
    }
#endif
#ifndef _WIN32
    InstallRuntimeSignalHandlers();
#endif

    dkr::runtime::rom::Identity rom_identity{};
    bool rom_identified = false;
    if (!rom_path.empty()) {
        if (!dkr::runtime::ValidateRomForLauncher(
                rom_path, rom_identity, rom_error)) {
            std::fprintf(stderr, "[boot][rom] %s\n", rom_error.c_str());
            return 3;
        }
        if (!PrepareCanonicalRomPath(rom_path, rom_identity,
                                     config_directory, rom_error)) {
            std::fprintf(stderr, "[boot][rom] %s\n", rom_error.c_str());
            return 3;
        }
        rom_identified = true;
    }

    {
        dkr::runtime::startup_performance::ScopedPhase phase(
            "pak-save-input-configure");
        dkr::runtime::pak::configure(config_directory);
        dkr::runtime::saves::configure(config_directory);
        dkr::runtime::platform::configure_input(config_directory);
#if DKR_RUNTIME_HAS_RT64
        // Custom tracks are user content beside the other imported assets, so
        // they live in the configuration directory rather than next to the
        // executable. Scanning here keeps the registry populated before the
        // first asset table load reaches the Patch Pipeline hooks.
        //
        // Deliberately not "mods": librecomp owns that directory for its own
        // .nrm mod format and reports "Mod is missing a mod.json" for anything
        // else it finds there, which would surface as an error for every user
        // who installs a track.
        dkr::runtime::custom_tracks::scan(config_directory / "custom-tracks");
#endif
    }

    {
        dkr::runtime::startup_performance::ScopedPhase phase(
            "platform-initialise");
        if (!dkr::runtime::platform::initialise()) {
            return 4;
        }
    }
    // N64ModernRuntime intentionally leaves its process-wide graphics options
    // value-initialized for applications with a settings frontend. Supply
    // parity-first defaults here so RT64 does not silently remain at 320x240.
    ultramodern::renderer::GraphicsConfig graphics_config{};
    graphics_config.developer_mode = false;
    graphics_config.res_option = ultramodern::renderer::Resolution::Auto;
    graphics_config.wm_option = ultramodern::renderer::WindowMode::Windowed;
    graphics_config.hr_option = ultramodern::renderer::HUDRatioMode::Original;
    graphics_config.api_option = ultramodern::renderer::GraphicsApi::Auto;
    graphics_config.ar_option = ultramodern::renderer::AspectRatio::Original;
    graphics_config.msaa_option = ultramodern::renderer::Antialiasing::None;
    graphics_config.rr_option = ultramodern::renderer::RefreshRate::Original;
    graphics_config.hpfb_option =
        ultramodern::renderer::HighPrecisionFramebuffer::Auto;
    graphics_config.rr_manual_value = 30;
    graphics_config.ds_option = 1;
    ultramodern::renderer::set_graphics_config(graphics_config);

#if DKR_RUNTIME_HAS_RT64
    {
        dkr::runtime::startup_performance::ScopedPhase phase("ui-configure");
        dkr::runtime::ui::configure(config_directory);
    }
    dkr::runtime::ui::reset_lifecycle_request();
    const auto window_started_at =
        dkr::runtime::startup_performance::Clock::now();
    auto window_handle = dkr::runtime::platform::create_window();
    dkr::runtime::startup_performance::report("window-create",
                                               window_started_at);
#if defined(_WIN32) || defined(__APPLE__)
    if (window_handle.window == nullptr) {
#else
    if (window_handle == nullptr) {
#endif
        std::fprintf(stderr, "[boot][window] failed to create the DKR-R window\n");
        dkr::runtime::platform::shutdown();
        return 4;
    }
    if (rom_path.empty()) {
        const auto startup = dkr::runtime::ui::run_startup_screen(
            static_cast<SDL_Window*>(dkr::runtime::platform::sdl_window()));
        if (!startup.start_game) {
            dkr::runtime::platform::shutdown();
            if (startup.lifecycle_request ==
                dkr::runtime::ui::LifecycleRequest::Restart) {
                return RelaunchApplication(argc, argv) ? 0 : 6;
            }
            return 0;
        }
        rom_path = startup.rom_path;
        prepared_mods = startup.mods;
        rom_identified = false;
    }
#else
    const ultramodern::renderer::WindowHandle window_handle{};
    if (rom_path.empty()) {
        std::fprintf(stderr, "The diagnostic runtime requires a ROM path.\n");
        dkr::runtime::platform::shutdown();
        return 2;
    }
#endif

    if (!rom_identified && !dkr::runtime::ValidateRomForLauncher(
            rom_path, rom_identity, rom_error)) {
        std::fprintf(stderr, "[boot][rom] %s\n", rom_error.c_str());
        dkr::runtime::platform::shutdown();
        return 3;
    }
    if (!rom_identified && !PrepareCanonicalRomPath(
            rom_path, rom_identity, config_directory, rom_error)) {
        std::fprintf(stderr, "[boot][rom] %s\n", rom_error.c_str());
        dkr::runtime::platform::shutdown();
        return 3;
    }
    if (!log_configured && !ConfigurePersistentRuntimeLog(config_directory)) {
        std::fprintf(stderr,
                     "[boot][log] could not create the persistent runtime log\n");
    }
#if defined(DKR_TARGET_WIN95)
    // How fine is std::chrono::high_resolution_clock here? ultramodern times
    // everything with it -- osGetCount, osGetTime, its timers, the VI loop -- and
    // on this target it is libstdc++'s system clock over winpthreads'
    // clock_gettime. Measured against the 8254 over 200 ms: the distinct steps
    // it takes, and the largest one.
    // The clock is initialised here, not relied upon: DkrMain only calls
    // dkr_clock_init further down, and the first version of this probe spun
    // forever on a clock that read zero -- the game never took the screen, and
    // the harness cut the machine's power with the log open. The iteration cap
    // makes sure no clock can hang it again.
    if (dkr_clock_init()) {
        const unsigned long long t0 = dkr_clock_now_us();
        auto last = std::chrono::high_resolution_clock::now();
        unsigned long steps = 0, spins = 0;
        long long largest = 0;
        while (dkr_clock_now_us() - t0 < 200000ULL && ++spins < 5000000UL) {
            const auto now = std::chrono::high_resolution_clock::now();
            if (now != last) {
                const long long d = std::chrono::duration_cast<std::chrono::microseconds>(now - last).count();
                if (d > largest) { largest = d; }
                steps++;
                last = now;
            }
        }
        std::fprintf(stderr, "[boot][clock] high_resolution_clock: %lu steps in 200 ms, "
                             "largest %lld us\n", steps, largest);
        // And the clock ultramodern now uses instead (patch 0055).
        const unsigned long long t1 = dkr_clock_now_us();
        auto plast = ultramodern::precise_now();
        unsigned long psteps = 0, pspins = 0;
        long long plargest = 0;
        while (dkr_clock_now_us() - t1 < 200000ULL && ++pspins < 5000000UL) {
            const auto now = ultramodern::precise_now();
            if (now != plast) {
                const long long d = std::chrono::duration_cast<std::chrono::microseconds>(now - plast).count();
                if (d > plargest) { plargest = d; }
                psteps++;
                plast = now;
            }
        }
        std::fprintf(stderr, "[boot][clock] ultramodern::precise_now: %lu steps in 200 ms, "
                             "largest %lld us\n", psteps, plargest);
    }
    if (std::getenv("DKR_PROBE_SWITCH") != nullptr) { dkr_switch_probe(); }
    // DKR_TRACE_AUDIO_ZONES calibrates the cycle counter here, not in the first
    // audio task: the calibration takes 60 ms, and a first task that long is
    // dropped by the game's scheduler as late, which then never sends another
    // one. The zones run of 26 September 2026 had no sound past its first task.
    if (std::getenv("DKR_TRACE_AUDIO_ZONES") != nullptr && !dkr_cycles_init()) {
        std::fprintf(stderr, "[boot][audio] cycle counter not calibrated; no audio zones\n");
    }
    StartIdleMeter();
    if (dkr_sampler_start()) {
        dkr_sampler_register_current_thread();
        std::fprintf(stderr, "[boot][sampler] sampling every 1 ms into D:\\SAMPLES.BIN\n");
    }
#endif
#if DKR_RUNTIME_HAS_RT64
    if (!rom_identified) {
        window_handle = dkr::runtime::platform::prepare_window_for_game();
#if defined(_WIN32) || defined(__APPLE__)
        if (window_handle.window == nullptr) {
#else
        if (window_handle == nullptr) {
#endif
            std::fprintf(stderr,
                         "[boot][window] failed to prepare the game renderer window\n");
            dkr::runtime::platform::shutdown();
            return 4;
        }
    }
#endif

    if (!dkr::runtime::RegisterGame(config_directory, rom_identity.revision,
                                    rom_error)) {
        std::fprintf(stderr, "[boot][rom] %s\n", rom_error.c_str());
        dkr::runtime::platform::shutdown();
        return 3;
    }
    const dkr::runtime::rom::Revision registered_revision = rom_identity.revision;
    dkr::runtime::startup_performance::mark("game-registered");
    std::fprintf(stderr, "[boot][rom] validated and registered\n");

    const recomp::rsp::callbacks_t rsp_callbacks{.get_rsp_microcode = GetRspMicrocode};
    const ultramodern::renderer::callbacks_t renderer_callbacks{
#if DKR_RUNTIME_HAS_RT64
        .create_render_context = dkr::runtime::CreateRT64Renderer};
#else
        .create_render_context = dkr::runtime::SelectRenderContext};
#endif
    const ultramodern::audio_callbacks_t audio_callbacks{
        .queue_samples = CountAndQueueAudio,
        .get_frames_remaining = dkr::runtime::platform::audio_frames_remaining,
        .set_frequency = RecordAndSetAudioFrequency,
#if DKR_RUNTIME_HAS_NETPLAY
        .external_work_allowed = []() {
            return dkr::runtime::netplay::external_side_effects_allowed();
        },
#endif
    };
    const ultramodern::input::callbacks_t input_callbacks{
        .poll_input = dkr::runtime::platform::poll_input,
#if DKR_RUNTIME_HAS_NETPLAY
        .frame_boundary = dkr::runtime::netplay::on_frame_boundary,
        .physical_poll_allowed = []() {
            return dkr::runtime::netplay::physical_input_poll_allowed();
        },
#endif
        .get_input = dkr::runtime::platform::get_input,
        .set_rumble = dkr::runtime::platform::set_rumble,
        .get_connected_device_info = dkr::runtime::platform::get_connected_device_info,
    };
    const ultramodern::renderer::callbacks_t unused_renderer_callbacks = renderer_callbacks;
    (void)unused_renderer_callbacks;
    // SDL's window event queue is serviced explicitly on DkrMain's thread
    // below. Do not rely on librecomp's optional update callback: that path is
    // not consistently serviced by every pinned runtime configuration and can
    // leave Escape, window close and Exit to Desktop unresponsive.
    const ultramodern::gfx_callbacks_t gfx_callbacks{};
    const ultramodern::events::callbacks_t events_callbacks{
#if DKR_RUNTIME_HAS_NETPLAY
        .authored_simulation_pacing_scale_milli_callback = []() {
            return dkr::runtime::netplay::
                authored_simulation_pacing_scale_milli();
        },
        .presentation_allowed_callback = []() {
            return dkr::runtime::netplay::external_side_effects_allowed();
        }
#endif
    };
    const ultramodern::error_handling::callbacks_t error_callbacks{.message_box = MessageBox};
    const ultramodern::threads::callbacks_t thread_callbacks{.get_game_thread_name = GetThreadName};

    const recomp::Configuration configuration{
        .project_version = {.major = 1, .minor = 0, .patch = 0,
                            .suffix = DKR_RELEASE_VERSION},
        .window_handle = window_handle,
        .rsp_callbacks = rsp_callbacks,
        .renderer_callbacks = renderer_callbacks,
        .audio_callbacks = audio_callbacks,
        .input_callbacks = input_callbacks,
        .gfx_callbacks = gfx_callbacks,
        .events_callbacks = events_callbacks,
#if DKR_RUNTIME_HAS_NETPLAY
        .save_write_allowed_callback = []() {
            return dkr::runtime::netplay::external_side_effects_allowed();
        },
#endif
        .error_handling_callbacks = error_callbacks,
        .threads_callbacks = thread_callbacks,
        // DKR's scheduler interrupt queue can briefly be full while the VI and
        // audio managers are active. SP/DP completion edges must be retained
        // until the scheduler accepts them or gfxtask_wait can block forever.
        // The renderer now parses from immutable submission snapshots and all
        // Release tasks complete comfortably inside DKR's watchdog, so retrying
        // a blocked edge cannot outlive the task that owns it.
        .message_queue_control = {.requeue_sp = true, .requeue_dp = true},
    };

#if defined(DKR_TARGET_WIN95)
    // --- The clock, before anything that reads it ---------------------------
    //
    // `dkr_clock_now` answers **zero** until `dkr_clock_init` has run, and zero
    // does not fail -- it reports every interval as instantaneous. That cost the
    // frame budget once already, on 28 August, when nothing in the game called
    // it and only the witnesses did. It was then initialised lazily inside the
    // renderer, which put it after the guest threads start; the guest-execution
    // measurement in `threads.cpp` reads the clock from those threads and got
    // zero for its trouble, so its whole accounting stayed switched off and said
    // nothing about it.
    //
    // Initialised here, once, before a thread or a window exists. A time base is
    // not a renderer's property.
    {
        const int ok = dkr_clock_init();
        std::fprintf(stderr, "[boot][clock] source=%s frequency=%lu Hz ok=%d\n",
                     dkr_clock_source_name(),
                     static_cast<unsigned long>(dkr_clock_frequency()), ok);
    }

    // --- E06-S01: the window, created here and not later ---------------------
    //
    // **On this thread**, because a window belongs to the thread that created it:
    // its messages go to that thread's queue and nowhere else. This loop is the
    // only thread that lives for the whole session without belonging to
    // `ultramodern`, and it already polls at one millisecond -- the cadence a
    // message pump wants.
    //
    // **And before the runtime thread**, because that thread opens Glide, and
    // Glide must be given this window. Measured on the machine on 28 August 2026:
    // with the window created afterwards, `grSstWinOpen` had already taken a
    // full-screen context bound to nothing, the new foreground window put the
    // desktop back on the screen, and the game stopped advancing at list 300.
    // The order is therefore load-bearing and not a tidy-up.
    //
    // Failing to create it is not fatal: the port rendered its intro for a
    // fortnight with no window at all, and a player who cannot press Start is
    // better off than one who cannot start the game.
    const bool have_window = dkr_window_open("DKR-R") != 0;
    if (have_window) {
        dkr_glide_set_window(dkr_window_handle());
    }
#endif

// The two above are created **once**, before upstream's session loop, and not
// per session: a window belongs to the thread that created it, and the clock is
// a time base rather than a session's property. The loop can only iterate on the
// RT64 path, which this target does not build, so once is also exactly once.

    for (;;) {
#if DKR_RUNTIME_HAS_RT64
        dkr::runtime::ui::reset_lifecycle_request();
#endif
        if (!dkr::runtime::SelectRom(rom_path, rom_error)) {
            std::fprintf(stderr, "[boot][rom] %s\n", rom_error.c_str());
            dkr::runtime::platform::shutdown();
            return 3;
        }
        dkr::runtime::startup_performance::mark("runtime-rom-selected");

        // The application can return to its launcher and start another fresh
        // emulated DKR session without restarting the process. Reset only the
        // per-session Magic Code state before the new RDRAM is created.
        // A lobby's accepted manifest, not later overlay preferences, owns
        // the simulation selection on every peer.
        std::optional<std::uint32_t> online_magic_codes;
#if DKR_RUNTIME_HAS_NETPLAY
        if (dkr::runtime::netplay::session().active()) {
            online_magic_codes = static_cast<std::uint32_t>(
                dkr::runtime::netplay::session().view().room.manifest.magic_codes_hash);
        }
#endif
        dkr::runtime::magic_codes::begin_game_session(online_magic_codes);
#if DKR_RUNTIME_HAS_NETPLAY
        dkr::runtime::netplay::reset_runtime_state();
#endif
        dkr::runtime::rev_a_asset_mutex::reset_statistics();
#if DKR_RUNTIME_HAS_LEGACY_MODS
        dkr::runtime::legacy::begin_session(nullptr);
        try {
            // Launcher preparation already ran on a worker with a progress
            // modal. Explicit command-line launches validate here instead.
            if(!prepared_mods)prepared_mods=dkr::mods::prepare_mod_launch(config_directory,rom_path,
#if DKR_RUNTIME_HAS_NETPLAY
                dkr::runtime::netplay::session().active(),
#else
                false,
#endif
                [](const char* stage){std::fprintf(stderr,"[legacy][launch] %s\n",stage);});
#if DKR_RUNTIME_HAS_NETPLAY
            if(prepared_mods->session && dkr::runtime::netplay::session().active())
                throw dkr::mods::Error("Offline custom assets cannot enter an online runtime.");
#endif
            dkr::runtime::legacy::begin_prepared(prepared_mods);
            dkr::runtime::pak::begin_session_directory(prepared_mods->pak_directory);
        } catch(const std::exception& error) {
            std::fprintf(stderr,"[legacy][launch] %s\n",error.what());
            dkr::runtime::platform::shutdown();return 4;
        }
#endif
#if DKR_LEGACY_QUALIFICATION
        if (const char* recipe = std::getenv("DKR_LEGACY_QUALIFICATION_RECIPE")) {
#if DKR_RUNTIME_HAS_NETPLAY
            if (dkr::runtime::netplay::session().active()) {
                std::fprintf(stderr, "[legacy][qualification] Refusing to modify an online session.\n");
                return 4;
            }
#endif
            try {
                dkr::runtime::legacy::configure_qualification(rom_path, std::filesystem::u8path(recipe));
            } catch (const std::exception& error) {
                std::fprintf(stderr, "[legacy][qualification] preparation failed: %s\n", error.what());
                return 4;
            }
        }
#endif
        std::fprintf(stderr,
                     "[boot] runtime initialized; waiting for first safe VI state\n");
        std::atomic<bool> runtime_finished{false};
        std::exception_ptr runtime_failure;
        dkr::sync::thread runtime_thread([&] {
            try {
                recomp::start(configuration);
            } catch (...) {
                runtime_failure = std::current_exception();
            }
            runtime_finished.store(true, std::memory_order_release);
        });
        dkr::runtime::startup_performance::mark("runtime-thread-started");

        const auto runtime_started_at = std::chrono::steady_clock::now();
        bool timeout_requested = false;
#if defined(DKR_TARGET_WIN95)
        bool window_quit = false;
#endif
        while (!runtime_finished.load(std::memory_order_acquire)) {
#if defined(DKR_TARGET_WIN95)
            if (have_window && !window_quit && !dkr_window_pump()) {
                // Every shutdown route -- the close button, Alt+F4, the session
                // ending -- arrives here as a single answer, and it asks the
                // runtime to stop rather than tearing it down: the game's
                // threads are running and the display is the card's until it
                // gives it back.
                std::fprintf(stderr, "[boot][window] shutdown requested\n");
                window_quit = true;
                ultramodern::quit();
            }
#endif
#if DKR_RUNTIME_HAS_RT64
            // The SDL video subsystem and native window were created on this
            // thread. Keep all window/input event pumping here for Windows,
            // X11 and Wayland compatibility while the recompiler owns its
            // worker.
            dkr::runtime::platform::pump_window_events(nullptr);
            dkr::runtime::service_online_wait_presentation();
#endif
            if (!timeout_requested && timeout_seconds != 0 &&
                std::chrono::steady_clock::now() - runtime_started_at >=
                    std::chrono::seconds(timeout_seconds)) {
#if DKR_RUNTIME_HAS_RT64
                std::fprintf(
                    stderr,
                    "[boot][watchdog] completed-f3ddkr-tasks=%llu\n",
                    static_cast<unsigned long long>(
                        dkr::runtime::completed_f3ddkr_task_count()));
#endif
                std::fprintf(stderr,
                             "[boot][watchdog] stopping after %u seconds\n",
                             timeout_seconds);
                timeout_requested = true;
                ultramodern::quit();
            }
            // Every millisecond. Measured against 16 ms on the target (E08-S01,
            // frame-budget.md): no difference in the frame or in spare time.
            dkr::sync::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        runtime_thread.join();
#if defined(DKR_TARGET_WIN95)
        // After the join and before any return: every exit from here -- clean
        // stop, watchdog, a runtime exception -- passes through this point, and
        // the window must be given back on all of them.
        dkr_window_close();
#endif
        dkr::runtime::pak::begin_session_directory({});
#if DKR_RUNTIME_HAS_LEGACY_MODS
        prepared_mods.reset();
        const auto mod_failure=dkr::runtime::legacy::failure();
        dkr::runtime::legacy::begin_session(nullptr);
#endif
        if (registered_revision == dkr::runtime::rom::Revision::UsV80) {
            const auto mutex_stats =
                dkr::runtime::rev_a_asset_mutex::statistics();
            std::fprintf(
                stderr,
                "[perf][v1.1-asset-mutex] fast=%" PRIu64 "/%" PRIu64
                " scheduler=%" PRIu64 "/%" PRIu64 "\n",
                mutex_stats.fast_acquires, mutex_stats.fast_releases,
                mutex_stats.scheduler_acquires,
                mutex_stats.scheduler_releases);
        }
#if DKR_RUNTIME_HAS_RT64
        const auto lifecycle_request = dkr::runtime::ui::lifecycle_request();
#endif
        if (runtime_failure != nullptr) {
            dkr::runtime::platform::shutdown();
            try {
                std::rethrow_exception(runtime_failure);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "[boot] runtime failed: %s\n",
                             error.what());
            } catch (...) {
                std::fprintf(
                    stderr,
                    "[boot] runtime failed with an unknown exception\n");
            }
            return 5;
        }
#if DKR_RUNTIME_HAS_RT64
        if (lifecycle_request == dkr::runtime::ui::LifecycleRequest::StopGame || !mod_failure.empty()) {
            if(!mod_failure.empty())dkr::runtime::ui::report_mod_error("Custom content stopped safely: "+mod_failure);
            std::fprintf(stderr,
                         "[boot][stop] game stopped; returning to launcher\n");
            dkr::runtime::ui::reset_lifecycle_request();
            const auto startup = dkr::runtime::ui::run_startup_screen(
                static_cast<SDL_Window*>(
                    dkr::runtime::platform::sdl_window()),
                rom_path);
            if (!startup.start_game) {
                dkr::runtime::platform::shutdown();
                if (startup.lifecycle_request ==
                    dkr::runtime::ui::LifecycleRequest::Restart) {
                    return RelaunchApplication(argc, argv) ? 0 : 6;
                }
                std::fprintf(stderr, "[boot] launcher closed cleanly\n");
                return 0;
            }

            dkr::runtime::rom::Identity next_identity{};
            std::filesystem::path next_rom_path = startup.rom_path;
            if (!dkr::runtime::ValidateRomForLauncher(
                    next_rom_path, next_identity, rom_error) ||
                !PrepareCanonicalRomPath(next_rom_path, next_identity,
                                         config_directory, rom_error)) {
                std::fprintf(stderr, "[boot][rom] %s\n", rom_error.c_str());
                dkr::runtime::platform::shutdown();
                return 3;
            }
            if (next_identity.revision != registered_revision) {
                std::fprintf(
                    stderr,
                    "[boot][rom] changing ROM revisions requires a clean "
                    "runtime relaunch\n");
                dkr::runtime::platform::shutdown();
                return RelaunchApplication(argc, argv) ? 0 : 6;
            }
            rom_path = std::move(next_rom_path);
#if DKR_RUNTIME_HAS_LEGACY_MODS
            prepared_mods = startup.mods;
#endif
            rom_identity = next_identity;
            std::fprintf(stderr,
                         "[boot][start] launching a new game session\n");
            continue;
        }
        if (lifecycle_request == dkr::runtime::ui::LifecycleRequest::Restart) {
            dkr::runtime::platform::shutdown();
            std::fprintf(stderr, "[boot][restart] relaunching DKR-R\n");
            return RelaunchApplication(argc, argv) ? 0 : 6;
        }
#endif
        dkr::runtime::platform::shutdown();
        std::fprintf(stderr, "[boot] runtime stopped cleanly\n");
        return 0;
    }
}

#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return DkrMain(__argc, __argv);
}
#else
int main(int argc, char** argv) {
    return DkrMain(argc, argv);
}
#endif
