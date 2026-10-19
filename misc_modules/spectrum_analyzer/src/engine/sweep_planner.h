#pragma once
#include "sweep_types.h"

namespace spectrum {
    struct GenericPlanParams {
        FrequencyRange range;
        double sampleRateHz = 0;
        double usableFraction = 0.8;  // part of the sample rate free of filter roll-off
        double overlapFraction = 0.5; // overlap between neighbouring segments (0..0.9)
        double dcGuardHz = 0;         // half width of the DC/LO-leakage region (0 = keep DC bins)
        double stepOverrideHz = 0;    // fixed LO step (0 = derive from usable bandwidth and overlap)
        SweepDirection direction = SweepDirection::UP;
        int maxSegments = 20000;
    };

    // Software stepped sweep plan for devices without a native sweep.
    // Segments are spaced evenly so that their usable windows tile the range.
    // When the span fits into half of the usable band the LO is offset so the
    // DC region falls outside the requested range.
    SweepPlan planGenericSweep(const GenericPlanParams& p);

    // Plan for the HackRF native (firmware) sweep, mirroring hackrf_sweep:
    // 20 Msps, 20 MHz steps, interleaved tunings, LO = block frequency + 7.5 MHz.
    SweepPlan planHackRFNativeSweep(const FrequencyRange& range);

    // Plan built from what a native provider reports when its sweep starts.
    SweepPlan planFromNativeInfo(const FrequencyRange& range, double sampleRateHz, double loOffsetHz,
                                 const std::vector<double>& blockFrequenciesHz,
                                 const std::vector<std::pair<double, double>>& validOffsets);

    // Measures how well the plan's segments cover the plan range.
    CoverageReport analyzeCoverage(const SweepPlan& plan);

    // Overlap needed so that the DC guard regions are covered by clean data
    // from neighbouring segments.
    double minOverlapForDcCoverage(double usableBandwidthHz, double dcGuardHz);

    int nextPow2(int v);
    // FFT size whose bin width is not coarser than the display bin width,
    // clamped to [minSize, maxSize] (powers of two).
    int autoFftSize(double sampleRateHz, double spanHz, int displayBins, int minSize, int maxSize);
}
