#pragma once

// E08-S01: "report distributions, not means: median and 99th percentile per
// item. It is the high percentile that produces the stutters and the audio
// dropouts."
//
// A fixed histogram of 256 bins of `bin_us` each, the last one catching
// everything above. A percentile is reported as the upper edge of the bin that
// holds it, so the resolution is the bin width, and the figure errs on the high
// side. A percentile equal to 256 bin widths means "at least that much": the
// first audio histogram, at 100 us a bin, read its p99 as exactly 25,600 us. No allocation and no floating point in `add`: it runs once per display
// list and once per audio task.

#include <cstdint>

namespace dkr::runtime {

class PercentileHistogram {
public:
    explicit PercentileHistogram(unsigned long bin_us) : bin_us_(bin_us ? bin_us : 1) {}

    void add(unsigned long long us) {
        unsigned long long bin = us / bin_us_;
        if (bin > kBins - 1) { bin = kBins - 1; }
        bins_[bin]++;
        count_++;
    }

    unsigned long count() const { return count_; }

    // Starts a new window. Budgets are read in steady state, not from boot:
    // the loading screens would otherwise weigh on the high percentiles.
    void reset() {
        for (unsigned long b = 0; b < kBins; b++) { bins_[b] = 0; }
        count_ = 0;
    }

    // The upper edge of the bin holding the `per_mille`-th sample (500 for the
    // median, 990 for the 99th percentile), in microseconds. Zero when empty.
    unsigned long long percentile(unsigned per_mille) const {
        if (count_ == 0) { return 0; }
        const unsigned long long rank =
            (static_cast<unsigned long long>(count_) * per_mille + 999ULL) / 1000ULL;
        unsigned long long seen = 0;
        for (unsigned long b = 0; b < kBins; b++) {
            seen += bins_[b];
            if (seen >= rank) { return static_cast<unsigned long long>(b + 1) * bin_us_; }
        }
        return static_cast<unsigned long long>(kBins) * bin_us_;
    }

private:
    static constexpr unsigned long kBins = 256;
    unsigned long bin_us_;
    unsigned long bins_[kBins] = {0};
    unsigned long count_ = 0;
};

}  // namespace dkr::runtime
