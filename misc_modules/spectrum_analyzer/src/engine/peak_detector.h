#pragma once
#include "sweep_types.h"

namespace spectrum {
    struct PeakDetectorConfig {
        float minLevelDb = -90.0f;        // absolute threshold
        float aboveNoiseDb = 10.0f;       // threshold relative to the noise floor estimate
        double minSpacingHz = 25e3;       // weaker peaks closer than this to a stronger one are dropped
        double minWidthHz = 0.0;          // narrower signals are ignored
        int maxPeaks = 64;
    };

    // Finds signals as runs of bins above max(minLevel, noise + aboveNoise).
    // Each run yields one peak at its strongest bin; the run length is the
    // signal width. Results are sorted by frequency.
    std::vector<DetectedPeak> detectPeaks(const std::vector<float>& db, double startHz, double binWidthHz,
                                          float noiseFloorDb, const PeakDetectorConfig& cfg);
}
