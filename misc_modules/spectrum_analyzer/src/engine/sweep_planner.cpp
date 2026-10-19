#include "sweep_planner.h"
#include <signal_path/native_sweep.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace spectrum {
    namespace {
        std::vector<ValidWindow> genericWindows(double halfU, double g) {
            if (g <= 0) {
                return { { -halfU, halfU, PRIORITY_CLEAN } };
            }
            return { { -halfU, -g, PRIORITY_CLEAN }, { -g, g, PRIORITY_DC }, { g, halfU, PRIORITY_CLEAN } };
        }

        struct Interval {
            double lo, hi;
        };

        // Merges intervals and returns covered length and gaps inside [a, b].
        void mergeCoverage(std::vector<Interval> iv, double a, double b, double tol, double& covered, double& maxGap, int& gapCount) {
            covered = 0;
            maxGap = 0;
            gapCount = 0;
            std::sort(iv.begin(), iv.end(), [](const Interval& x, const Interval& y) { return x.lo < y.lo; });
            double cursor = a;
            for (auto& i : iv) {
                double lo = std::max(i.lo, a);
                double hi = std::min(i.hi, b);
                if (hi <= lo) { continue; }
                if (lo > cursor + tol) {
                    maxGap = std::max(maxGap, lo - cursor);
                    gapCount++;
                }
                if (hi > cursor) {
                    covered += hi - std::max(lo, cursor);
                    cursor = hi;
                }
            }
            if (b > cursor + tol) {
                maxGap = std::max(maxGap, b - cursor);
                gapCount++;
            }
        }
    }

    int nextPow2(int v) {
        int p = 1;
        while (p < v && p < (1 << 30)) { p <<= 1; }
        return p;
    }

    int autoFftSize(double sampleRateHz, double spanHz, int displayBins, int minSize, int maxSize) {
        if (!(sampleRateHz > 0) || !(spanHz > 0) || displayBins <= 0) { return minSize; }
        double displayBinHz = spanHz / (double)displayBins;
        double target = sampleRateHz / displayBinHz;
        int n = nextPow2((int)std::min(target, (double)(1 << 30)));
        return std::clamp(n, minSize, maxSize);
    }

    double minOverlapForDcCoverage(double usableBandwidthHz, double dcGuardHz) {
        if (usableBandwidthHz <= 0 || dcGuardHz <= 0) { return 0.0; }
        // Neighbouring clean windows cover the DC region when step <= U/2 - g.
        double step = usableBandwidthHz / 2.0 - dcGuardHz;
        if (step <= 0) { return 0.95; }
        return std::clamp(1.0 - step / usableBandwidthHz, 0.0, 0.95);
    }

    SweepPlan planGenericSweep(const GenericPlanParams& p) {
        SweepPlan plan;
        plan.range = p.range;
        plan.sampleRateHz = p.sampleRateHz;
        plan.native = false;

        if (!(p.sampleRateHz > 0)) {
            plan.error = "Sample rate of the source is unknown (is the receiver running?)";
            return plan;
        }
        double span = p.range.spanHz();
        if (!(span > 0)) {
            plan.error = "Stop frequency must be above the start frequency";
            return plan;
        }

        double frac = std::clamp(p.usableFraction, 0.05, 1.0);
        double U = p.sampleRateHz * frac;
        double halfU = U / 2.0;
        double g = std::clamp(p.dcGuardHz, 0.0, halfU * 0.45);
        plan.usableBandwidthHz = U;
        plan.dcGuardHz = g;
        auto windows = genericWindows(halfU, g);

        // Narrow span: offset the LO so the DC region lies outside the range.
        if (g > 0 && p.stepOverrideHz <= 0 && span <= halfU - g) {
            double margin = ((halfU - g) - span) / 2.0;
            double lo = p.range.startHz - g - margin; // range inside the upper clean window
            if (lo - halfU < 0) {
                lo = p.range.stopHz + g + margin;     // range inside the lower clean window
            }
            Segment s;
            s.index = 0;
            s.loHz = lo;
            s.windows = windows;
            plan.segments.push_back(s);
            plan.stepHz = 0;
            plan.dcCoveredByOverlap = true;
            return plan;
        }

        // Span fits into a single capture.
        if (span <= U && p.stepOverrideHz <= 0) {
            Segment s;
            s.index = 0;
            s.loHz = p.range.centerHz();
            s.windows = windows;
            plan.segments.push_back(s);
            plan.stepHz = 0;
            plan.dcCoveredByOverlap = (g <= 0);
            if (g > 0) {
                plan.warnings.push_back("The DC region in the middle of the range is measured by a single capture and is marked as DC.");
            }
            return plan;
        }

        double step;
        int n;
        if (p.stepOverrideHz > 0) {
            step = p.stepOverrideHz;
            if (step > U) {
                step = U;
                plan.warnings.push_back("Frequency step limited to the usable bandwidth (" + formatFrequency(U) + ") to avoid gaps.");
            }
            n = (span <= U) ? 1 : (int)std::ceil((span - U) / step - 1e-9) + 1;
        }
        else {
            double overlap = std::clamp(p.overlapFraction, 0.0, 0.95);
            double maxStep = U * (1.0 - overlap);
            n = (int)std::ceil((span - U) / maxStep - 1e-9) + 1;
            step = (n > 1) ? (span - U) / (double)(n - 1) : 0.0;
        }
        if (n > p.maxSegments) {
            plan.error = "The range needs " + std::to_string(n) + " segments (limit " + std::to_string(p.maxSegments) +
                         "). Reduce the span or use a larger step / sample rate.";
            return plan;
        }

        plan.stepHz = step;
        plan.segments.reserve(n);
        for (int i = 0; i < n; i++) {
            Segment s;
            s.index = i;
            s.loHz = p.range.startHz + halfU + (double)i * step;
            s.windows = windows;
            plan.segments.push_back(s);
        }

        if (p.direction == SweepDirection::DOWN) {
            std::reverse(plan.segments.begin(), plan.segments.end());
            for (int i = 0; i < (int)plan.segments.size(); i++) { plan.segments[i].index = i; }
        }

        CoverageReport cov = analyzeCoverage(plan);
        plan.dcCoveredByOverlap = (cov.maxCleanGapHz <= 0);
        if (g > 0 && !plan.dcCoveredByOverlap) {
            char buf[160];
            snprintf(buf, sizeof(buf), "DC regions are not covered by neighbouring segments; raise the overlap to at least %.0f%% to remove them.",
                     std::ceil(minOverlapForDcCoverage(U, g) * 100.0));
            plan.warnings.push_back(buf);
        }
        if (cov.maxGapHz > 0) {
            plan.warnings.push_back("The plan leaves gaps of up to " + formatFrequency(cov.maxGapHz) + ".");
        }
        return plan;
    }

    SweepPlan planFromNativeInfo(const FrequencyRange& range, double sampleRateHz, double loOffsetHz,
                                 const std::vector<double>& blockFrequenciesHz,
                                 const std::vector<std::pair<double, double>>& validOffsets) {
        SweepPlan plan;
        plan.range = range;
        plan.sampleRateHz = sampleRateHz;
        plan.native = true;
        if (blockFrequenciesHz.empty() || validOffsets.empty() || !(sampleRateHz > 0)) {
            plan.error = "The device did not report a valid sweep plan";
            return plan;
        }
        std::vector<ValidWindow> windows;
        double minOff = 0, maxOff = 0;
        for (auto& v : validOffsets) {
            windows.push_back({ v.first, v.second, PRIORITY_CLEAN });
            minOff = std::min(minOff, v.first);
            maxOff = std::max(maxOff, v.second);
        }
        plan.usableBandwidthHz = maxOff - minOff;
        std::vector<double> freqs = blockFrequenciesHz;
        std::sort(freqs.begin(), freqs.end());
        for (int i = 0; i < (int)freqs.size(); i++) {
            Segment s;
            s.index = i;
            s.loHz = freqs[i] + loOffsetHz;
            s.windows = windows;
            plan.segments.push_back(s);
        }
        plan.stepHz = (freqs.size() > 1) ? (freqs[1] - freqs[0]) : 0.0;
        CoverageReport cov = analyzeCoverage(plan);
        if (cov.maxGapHz > 0) {
            plan.warnings.push_back("The device sweep does not cover " + formatFrequency(cov.maxGapHz) + " of the requested range (device limit).");
        }
        return plan;
    }

    SweepPlan planHackRFNativeSweep(const FrequencyRange& range) {
        namespace hf = sweep::hackrf_format;
        uint16_t startMHz = 0, stopMHz = 0;
        int steps = hf::alignRange(range.startHz, range.stopHz, startMHz, stopMHz);
        if (steps <= 0) {
            SweepPlan plan;
            plan.range = range;
            plan.native = true;
            plan.error = "Range is outside of the HackRF sweep range (0 - 7250 MHz)";
            return plan;
        }
        std::vector<std::pair<double, double>> offs;
        for (auto& v : hf::validOffsets()) { offs.push_back({ v.loHz, v.hiHz }); }
        return planFromNativeInfo(range, hf::SAMPLE_RATE_HZ, hf::OFFSET_HZ, hf::interleavedBlockFrequencies(startMHz, steps), offs);
    }

    CoverageReport analyzeCoverage(const SweepPlan& plan) {
        CoverageReport rep;
        std::vector<Interval> clean, any;
        for (auto& s : plan.segments) {
            for (auto& w : s.windows) {
                Interval iv{ s.loHz + w.loOffsetHz, s.loHz + w.hiOffsetHz };
                any.push_back(iv);
                if (w.priority >= PRIORITY_CLEAN) { clean.push_back(iv); }
            }
        }
        double tol = std::max(1.0, plan.range.spanHz() * 1e-9);
        int gaps;
        mergeCoverage(clean, plan.range.startHz, plan.range.stopHz, tol, rep.coveredHz, rep.maxCleanGapHz, gaps);
        mergeCoverage(any, plan.range.startHz, plan.range.stopHz, tol, rep.coveredAnyHz, rep.maxGapHz, rep.gapCount);
        return rep;
    }
}
