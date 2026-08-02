#include "game_registration.hpp"
#include "null_renderer.hpp"
#include "runtime_platform.hpp"
#include "virtual_pak.hpp"
#if DKR_RUNTIME_HAS_RT64
#include "rt64_renderer.hpp"
#include "runtime_ui.hpp"
#endif

#include "librecomp/game.hpp"
#include "librecomp/rsp.hpp"
#include "ultramodern/config.hpp"
#include "ultramodern/ultramodern.hpp"

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DbgHelp.h>
#include <TlHelp32.h>
#endif

extern RspUcodeFunc dkrAspMain;
extern "C" std::uint64_t dkr_scheduler_sp_handler_count();
extern "C" std::uint64_t dkr_scheduler_dp_handler_count();

namespace {

std::atomic_flag g_logged_rsp_bootstrap = ATOMIC_FLAG_INIT;
std::atomic<std::uint64_t> g_audio_rsp_tasks{0};
#ifdef _WIN32
std::atomic_flag g_crash_filter_active = ATOMIC_FLAG_INIT;

std::string ThreadDescription(HANDLE thread) {
    PWSTR wide_description = nullptr;
    if (GetThreadDescription(thread, &wide_description) != S_OK ||
        wide_description == nullptr) {
        return {};
    }
    const int required = WideCharToMultiByte(CP_UTF8, 0, wide_description, -1,
                                             nullptr, 0, nullptr, nullptr);
    std::string description;
    if (required > 1) {
        description.resize(static_cast<std::size_t>(required));
        WideCharToMultiByte(CP_UTF8, 0, wide_description, -1,
                            description.data(), required, nullptr, nullptr);
        description.pop_back();
    }
    LocalFree(wide_description);
    return description;
}

std::uint64_t FileTimeValue(const FILETIME& value) {
    ULARGE_INTEGER converted{};
    converted.LowPart = value.dwLowDateTime;
    converted.HighPart = value.dwHighDateTime;
    return converted.QuadPart;
}

void DumpWatchdogThreadStacks() {
    HANDLE process = GetCurrentProcess();
    const DWORD64 module_base =
        reinterpret_cast<DWORD64>(GetModuleHandleW(nullptr));
    std::fprintf(stderr, "[boot][watchdog][stack-base] module=0x%016llX\n",
                 static_cast<unsigned long long>(module_base));
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);
    const DWORD process_id = GetCurrentProcessId();
    const DWORD current_thread_id = GetCurrentThreadId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }

    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Thread32First(snapshot, &entry); more;
         more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != process_id ||
            entry.th32ThreadID == current_thread_id) {
            continue;
        }
        HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION | THREAD_GET_CONTEXT |
                                       THREAD_SUSPEND_RESUME,
                                   FALSE, entry.th32ThreadID);
        if (thread == nullptr) {
            continue;
        }
        FILETIME created{}, exited_time{}, kernel{}, user{};
        const bool have_times = GetThreadTimes(thread, &created, &exited_time,
                                               &kernel, &user) != FALSE;
        const std::uint64_t cpu_ms = have_times
            ? (FileTimeValue(kernel) + FileTimeValue(user)) / 10000ULL
            : 0ULL;
        const std::string description = ThreadDescription(thread);
        const bool is_dkr_thread = description.rfind("DKR-", 0) == 0;
        if (!is_dkr_thread && cpu_ms < 250ULL) {
            CloseHandle(thread);
            continue;
        }

        std::fprintf(stderr, "[boot][watchdog][thread] id=%lu name=%s cpu-ms=%llu\n",
                     entry.th32ThreadID,
                     description.empty() ? "(unnamed)" : description.c_str(),
                     static_cast<unsigned long long>(cpu_ms));
        if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
            CloseHandle(thread);
            continue;
        }
        CONTEXT context{};
        context.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(thread, &context) != FALSE) {
            STACKFRAME64 frame{};
            frame.AddrPC.Offset = context.Rip;
            frame.AddrPC.Mode = AddrModeFlat;
            frame.AddrStack.Offset = context.Rsp;
            frame.AddrStack.Mode = AddrModeFlat;
            frame.AddrFrame.Offset = context.Rbp;
            frame.AddrFrame.Mode = AddrModeFlat;
            std::array<unsigned char, sizeof(SYMBOL_INFO) + MAX_SYM_NAME> storage{};
            auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage.data());
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = MAX_SYM_NAME;
            for (unsigned index = 0; index < 16; ++index) {
                if (index != 0 &&
                    !StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame,
                                 &context, nullptr, SymFunctionTableAccess64,
                                 SymGetModuleBase64, nullptr)) {
                    break;
                }
                if (frame.AddrPC.Offset == 0) {
                    break;
                }
                DWORD64 displacement = 0;
                if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) {
                    std::fprintf(stderr,
                                 "[boot][watchdog][stack] id=%lu #%u %s+0x%llX\n",
                                 entry.th32ThreadID, index, symbol->Name,
                                 static_cast<unsigned long long>(displacement));
                } else {
                    std::fprintf(stderr,
                                 "[boot][watchdog][stack] id=%lu #%u 0x%016llX "
                                 "rva=0x%llX\n",
                                 entry.th32ThreadID, index,
                                 static_cast<unsigned long long>(frame.AddrPC.Offset),
                                 static_cast<unsigned long long>(
                                     frame.AddrPC.Offset - module_base));
                }
            }
        }
        ResumeThread(thread);
        CloseHandle(thread);
    }
    CloseHandle(snapshot);
    SymCleanup(process);
}
#endif

std::filesystem::path DefaultConfigDirectory(const char* executable_argument) {
    std::error_code error;
    const std::filesystem::path executable = std::filesystem::absolute(
        std::filesystem::u8path(executable_argument), error);
    const std::filesystem::path executable_directory = error
        ? std::filesystem::current_path()
        : executable.parent_path();
    if (std::filesystem::exists(executable_directory / "portable.txt")) {
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
    if (task->t.type == M_AUDTASK && task->t.ucode == 0x800D7600U) {
        const auto task_index = ++g_audio_rsp_tasks;
        if (task_index <= 20 || task->t.data_size == 0) {
            std::fprintf(stderr,
                         "[boot][rsp] audio-task=%llu data=0x%08X size=%u output=0x%08X output-size=%u\n",
                         static_cast<unsigned long long>(task_index), task->t.data_ptr,
                         task->t.data_size, task->t.output_buff, task->t.output_buff_size);
        }
        // DKR can submit a zero-command audio frame when the host-reported AI
        // queue already satisfies the synthesizer's requested frame size. The
        // original scheduler treats that as completed work; entering the ABI
        // dispatcher with a zero-byte list would DMA and execute stale memory.
        if (task->t.data_size == 0) {
            return EmptyAudioTask;
        }
        return +[](std::uint8_t* rdram, std::uint32_t ucode_address) {
            if (!g_logged_rsp_bootstrap.test_and_set()) {
                const std::uint32_t data_address = RSP_MEM_W_LOAD(0x30, 0xFC0);
                std::fprintf(stderr,
                             "[boot][rsp] asp DMEM table=%04X,%04X,%04X,%04X "
                             "RDRAM table=%04X,%04X,%04X,%04X "
                             "commands@%08X=%08X,%08X,%08X,%08X\n",
                             RSP_MEM_HU_LOAD(0, 0x10), RSP_MEM_HU_LOAD(0, 0x12),
                             RSP_MEM_HU_LOAD(0, 0x14), RSP_MEM_HU_LOAD(0, 0x16),
                             static_cast<unsigned>(MEM_HU(0x10, 0xFFFFFFFF800E98D0ULL)),
                             static_cast<unsigned>(MEM_HU(0x12, 0xFFFFFFFF800E98D0ULL)),
                             static_cast<unsigned>(MEM_HU(0x14, 0xFFFFFFFF800E98D0ULL)),
                             static_cast<unsigned>(MEM_HU(0x16, 0xFFFFFFFF800E98D0ULL)),
                             data_address,
                             static_cast<unsigned>(MEM_W(0x00, static_cast<gpr>(static_cast<std::int32_t>(data_address)))),
                             static_cast<unsigned>(MEM_W(0x04, static_cast<gpr>(static_cast<std::int32_t>(data_address)))),
                             static_cast<unsigned>(MEM_W(0x08, static_cast<gpr>(static_cast<std::int32_t>(data_address)))),
                             static_cast<unsigned>(MEM_W(0x0C, static_cast<gpr>(static_cast<std::int32_t>(data_address)))));
            }
            return dkrAspMain(rdram, ucode_address);
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

#ifdef _WIN32
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
    std::fprintf(stderr, "[boot][crash] module-base=0x%016llX rva=0x%llX\n",
                 static_cast<unsigned long long>(module_base),
                 static_cast<unsigned long long>(fault_address - module_base));
    std::fprintf(stderr,
                 "[boot][crash] registers rcx=0x%016llX rdx=0x%016llX "
                 "r8=0x%016llX r9=0x%016llX rsp=0x%016llX\n",
                 static_cast<unsigned long long>(exception->ContextRecord->Rcx),
                 static_cast<unsigned long long>(exception->ContextRecord->Rdx),
                 static_cast<unsigned long long>(exception->ContextRecord->R8),
                 static_cast<unsigned long long>(exception->ContextRecord->R9),
                 static_cast<unsigned long long>(exception->ContextRecord->Rsp));

    CONTEXT context = *exception->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    alignas(SYMBOL_INFO) unsigned char symbol_storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_storage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;

    for (unsigned index = 0; index < 32; ++index) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame,
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

#endif

} // namespace

int DkrMain(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
#ifdef _WIN32
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

    if (argc > 4) {
        std::fprintf(stderr,
                     "Usage: DKRPortGame [rom.z64] [config-directory] [timeout-seconds]\n");
        return 2;
    }

    std::filesystem::path rom_path;
    if (argc >= 2) {
        rom_path = std::filesystem::u8path(argv[1]);
    }
    const std::filesystem::path config_directory = argc >= 3
        ? std::filesystem::u8path(argv[2])
        : DefaultConfigDirectory(argv[0]);
    const unsigned timeout_seconds = argc >= 4 ? static_cast<unsigned>(std::stoul(argv[3])) : 0U;
    std::filesystem::create_directories(config_directory);
    dkr::runtime::pak::configure(config_directory);

    if (!dkr::runtime::platform::initialise()) {
        return 4;
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

    dkr::runtime::RegisterGame(config_directory);
#if DKR_RUNTIME_HAS_RT64
    dkr::runtime::ui::configure(config_directory);
    const auto window_handle = dkr::runtime::platform::create_window();
#if defined(_WIN32)
    if (window_handle.window == nullptr) {
#else
    if (window_handle == nullptr) {
#endif
        std::fprintf(stderr, "[boot][window] failed to create the DKR Port window\n");
        dkr::runtime::platform::shutdown();
        return 4;
    }
    if (rom_path.empty()) {
        const auto startup = dkr::runtime::ui::run_startup_screen(
            static_cast<SDL_Window*>(dkr::runtime::platform::sdl_window()));
        if (!startup.start_game) {
            dkr::runtime::platform::shutdown();
            return 0;
        }
        rom_path = startup.rom_path;
    }
#else
    const ultramodern::renderer::WindowHandle window_handle{};
    if (rom_path.empty()) {
        std::fprintf(stderr, "The diagnostic runtime requires a ROM path.\n");
        dkr::runtime::platform::shutdown();
        return 2;
    }
#endif

    std::string rom_error;
    if (!dkr::runtime::SelectRom(rom_path, rom_error)) {
        std::fprintf(stderr, "[boot][rom] %s\n", rom_error.c_str());
        return 3;
    }
    std::fprintf(stderr, "[boot][rom] validated and registered\n");

    const recomp::rsp::callbacks_t rsp_callbacks{.get_rsp_microcode = GetRspMicrocode};
    const ultramodern::renderer::callbacks_t renderer_callbacks{
#if DKR_RUNTIME_HAS_RT64
        .create_render_context = dkr::runtime::CreateRT64Renderer};
#else
        .create_render_context = dkr::runtime::CreateDiagnosticRenderer};
#endif
    const ultramodern::audio_callbacks_t audio_callbacks{
        .queue_samples = dkr::runtime::platform::queue_audio,
        .get_frames_remaining = dkr::runtime::platform::audio_frames_remaining,
        .set_frequency = dkr::runtime::platform::set_audio_frequency,
    };
    const ultramodern::input::callbacks_t input_callbacks{
        .poll_input = dkr::runtime::platform::poll_input,
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
    const ultramodern::events::callbacks_t events_callbacks{};
    const ultramodern::error_handling::callbacks_t error_callbacks{.message_box = MessageBox};
    const ultramodern::threads::callbacks_t thread_callbacks{.get_game_thread_name = GetThreadName};

    const recomp::Configuration configuration{
        .project_version = {.major = 1, .minor = 0, .patch = 0, .suffix = ""},
        .window_handle = window_handle,
        .rsp_callbacks = rsp_callbacks,
        .renderer_callbacks = renderer_callbacks,
        .audio_callbacks = audio_callbacks,
        .input_callbacks = input_callbacks,
        .gfx_callbacks = gfx_callbacks,
        .events_callbacks = events_callbacks,
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

    std::fprintf(stderr, "[boot] runtime initialized; waiting for first safe VI state\n");
    std::atomic<bool> runtime_finished{false};
    std::exception_ptr runtime_failure;
    std::thread runtime_thread([&] {
        try {
            recomp::start(configuration);
        } catch (...) {
            runtime_failure = std::current_exception();
        }
        runtime_finished.store(true, std::memory_order_release);
    });

    const auto runtime_started_at = std::chrono::steady_clock::now();
    bool timeout_requested = false;
#if DKR_RUNTIME_HAS_RT64
    const bool ui_smoke_test = std::getenv("DKR_UI_SMOKE_TEST") != nullptr;
    const bool ui_smoke_hold_open =
        std::getenv("DKR_UI_SMOKE_HOLD_OPEN") != nullptr;
    bool ui_toggle_injected = false;
    bool ui_open_observed = false;
    bool ui_close_injected = false;
    bool ui_test_quit_requested = false;
#endif
    while (!runtime_finished.load(std::memory_order_acquire)) {
#if DKR_RUNTIME_HAS_RT64
        // The SDL video subsystem and native window were created on this
        // thread. Keep all window/input event pumping here for Windows, X11
        // and Wayland compatibility while the recompiler owns its worker.
        dkr::runtime::platform::pump_window_events(nullptr);
        const auto ui_test_elapsed = std::chrono::steady_clock::now() - runtime_started_at;
        if (ui_smoke_test && !ui_toggle_injected &&
            ui_test_elapsed >= std::chrono::seconds(3)) {
            dkr::runtime::platform::inject_overlay_toggle_for_test();
            ui_toggle_injected = true;
        }
        if (ui_smoke_test && ui_toggle_injected && !ui_open_observed &&
            dkr::runtime::ui::overlay_visible()) {
            ui_open_observed = true;
            std::fprintf(stderr, "[test][ui] PASS: Escape opened the overlay\n");
        }
        if (ui_smoke_test && !ui_smoke_hold_open && ui_open_observed && !ui_close_injected &&
            ui_test_elapsed >= std::chrono::seconds(5)) {
            dkr::runtime::platform::inject_overlay_toggle_for_test();
            ui_close_injected = true;
        }
        if (ui_smoke_test && ui_close_injected && !ui_test_quit_requested &&
            !dkr::runtime::ui::overlay_visible()) {
            std::fprintf(stderr, "[test][ui] PASS: Escape closed the overlay\n");
            ui_test_quit_requested = true;
            ultramodern::quit();
        }
#endif
        if (!timeout_requested && timeout_seconds != 0 &&
            std::chrono::steady_clock::now() - runtime_started_at >=
                std::chrono::seconds(timeout_seconds)) {
#ifdef _WIN32
            if (std::getenv("DKR_WATCHDOG_STACKS") != nullptr) {
                DumpWatchdogThreadStacks();
            }
#endif
#if DKR_RUNTIME_HAS_RT64
            const auto event_diagnostics = ultramodern::get_event_diagnostics();
            const auto message_diagnostics = ultramodern::get_message_diagnostics();
            std::fprintf(stderr, "[boot][watchdog] completed-f3ddkr-tasks=%llu\n",
                         static_cast<unsigned long long>(
                             dkr::runtime::completed_f3ddkr_task_count()));
            std::fprintf(stderr,
                         "[boot][watchdog] gfx-submitted=%llu gfx-dequeued=%llu "
                         "non-gfx-submitted=%llu sp-started=%llu sp-acked=%llu "
                         "dp-published=%llu\n",
                         static_cast<unsigned long long>(event_diagnostics.gfx_tasks_submitted),
                         static_cast<unsigned long long>(event_diagnostics.gfx_tasks_dequeued),
                         static_cast<unsigned long long>(event_diagnostics.non_gfx_tasks_submitted),
                         static_cast<unsigned long long>(event_diagnostics.sp_completions_started),
                         static_cast<unsigned long long>(event_diagnostics.sp_completions_acknowledged),
                         static_cast<unsigned long long>(event_diagnostics.dp_completions_published));
            std::fprintf(stderr,
                         "[boot][watchdog] scheduler-sp-handled=%llu "
                         "scheduler-dp-handled=%llu\n",
                         static_cast<unsigned long long>(dkr_scheduler_sp_handler_count()),
                         static_cast<unsigned long long>(dkr_scheduler_dp_handler_count()));
            std::fprintf(stderr,
                         "[boot][watchdog] dp-enqueue-requests=%llu "
                         "dp-enqueued-with-queue=%llu dp-enqueue-successes=%llu "
                         "dp-send-attempts=%llu dp-send-blocked=%llu dp-sent=%llu "
                         "external-queue-depth=%llu\n",
                         static_cast<unsigned long long>(message_diagnostics.dp_enqueue_requests),
                         static_cast<unsigned long long>(message_diagnostics.dp_enqueued_with_queue),
                         static_cast<unsigned long long>(message_diagnostics.dp_enqueue_successes),
                         static_cast<unsigned long long>(message_diagnostics.dp_send_attempts),
                         static_cast<unsigned long long>(message_diagnostics.dp_send_blocked),
                         static_cast<unsigned long long>(message_diagnostics.dp_sent),
                         static_cast<unsigned long long>(message_diagnostics.external_queue_depth));
#endif
            std::fprintf(stderr, "[boot][watchdog] stopping after %u seconds\n",
                         timeout_seconds);
            timeout_requested = true;
            ultramodern::quit();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    runtime_thread.join();
    dkr::runtime::platform::shutdown();
    if (runtime_failure != nullptr) {
        try {
            std::rethrow_exception(runtime_failure);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "[boot] runtime failed: %s\n", error.what());
        } catch (...) {
            std::fprintf(stderr, "[boot] runtime failed with an unknown exception\n");
        }
        return 5;
    }
    std::fprintf(stderr, "[boot] runtime stopped cleanly\n");
    return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return DkrMain(__argc, __argv);
}
#else
int main(int argc, char** argv) {
    return DkrMain(argc, argv);
}
#endif
