#pragma once

// E08-S01: "the export to a file allows offline analysis."
//
// One record per event, written raw to a file that `tools/win95/timing_report.py`
// turns into CSV and percentiles. `DKR_TIMING_EXPORT=<prefix>` turns it on; the
// streams are `<prefix>FRAMES.BIN` (one record per display list) and
// `<prefix>AUDIO.BIN` (one per audio task). On the target the prefix is `D:\`,
// the transfer disk.
//
// One file per stream because each has a single writer: the graphics thread
// for one, the audio task for the other. The target has no `<mutex>` (see
// check-cpp-subset.py), and one stream per writer needs none.
//
// Records are buffered and written 128 at a time, then flushed: a run is ended
// by killing the machine, and what was flushed survives it (checked on the
// target: every complete chunk of a 108 s run was in the file). Only the last
// partial chunk is lost, under five seconds of display lists. The file is capped
// at kMaxRecords -- an unbounded file is how the transfer image was corrupted
// twice, and the cap keeps a forgotten variable from filling the disk.
//
// File: "DKRT", u32 version (1), u32 record size (16), then records of four
// little-endian u32: time in ms since the clock started, then three values
// whose meaning depends on the stream (see timing_report.py).

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace dkr::runtime {

class TimingExport {
public:
    // `name` is appended to the prefix, e.g. "FRAMES.BIN".
    explicit TimingExport(const char* name) {
        const char* prefix = std::getenv("DKR_TIMING_EXPORT");
        if (prefix == nullptr) { return; }
        char path[260];
        if (std::strlen(prefix) + std::strlen(name) + 1 > sizeof(path)) { return; }
        std::strcpy(path, prefix);
        std::strcat(path, name);
        file_ = std::fopen(path, "wb");
        if (file_ == nullptr) {
            std::fprintf(stderr, "[timing] cannot open %s\n", path);
            return;
        }
        const std::uint32_t header[2] = {1u, 16u};
        std::fwrite("DKRT", 1, 4, file_);
        std::fwrite(header, sizeof(header), 1, file_);
        std::fflush(file_);
        std::fprintf(stderr, "[timing] exporting to %s\n", path);
    }

    bool enabled() const { return file_ != nullptr; }

    void add(std::uint32_t t_ms, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        if (file_ == nullptr) { return; }
        std::uint32_t* r = buffer_[used_];
        r[0] = t_ms; r[1] = a; r[2] = b; r[3] = c;
        if (++used_ == kChunk) { flush(); }
    }

private:
    void flush() {
        std::fwrite(buffer_, sizeof(buffer_[0]), used_, file_);
        std::fflush(file_);
        written_ += used_;
        used_ = 0;
        if (written_ >= kMaxRecords) {
            std::fclose(file_);
            file_ = nullptr;
            std::fprintf(stderr, "[timing] export capped at %lu records\n", written_);
        }
    }

    static constexpr unsigned long kChunk = 128;
    static constexpr unsigned long kMaxRecords = 131072;   // 2 MB
    std::FILE* file_ = nullptr;
    std::uint32_t buffer_[kChunk][4] = {};
    unsigned long used_ = 0;
    unsigned long written_ = 0;
};

}  // namespace dkr::runtime
