#pragma once
#include "sweep_types.h"

namespace spectrum {
    struct PeakDetectorConfig {
        float minLevelDb = -90.0f;        // absolute threshold
        float aboveNoiseDb = 12.0f;       // threshold relative to the noise floor estimate
        double minSpacingHz = 25e3;       // weaker peaks closer than this to a stronger one are dropped
        double minWidthHz = 0.0;          // narrower signals are ignored
        int maxPeaks = 64;
        int confirmHits = 2;              // sweeps a signal must appear in before it is shown (one missed sweep is tolerated)
        double holdSeconds = 5.0;         // a signal stays visible this long after it was last seen
        int maxShown = 5;                 // only the strongest signals are shown (with hysteresis, so the set stays stable)
    };

    // Finds signals as runs of bins above max(minLevel, noise + aboveNoise).
    // Each run yields one peak at its strongest bin; the run length is the
    // signal width. Results are sorted by frequency.
    std::vector<DetectedPeak> detectPeaks(const std::vector<float>& db, double startHz, double binWidthHz,
                                          float noiseFloorDb, const PeakDetectorConfig& cfg);

    // A signal followed over several sweeps.
    struct TrackedSignal {
        DetectedPeak peak;               // latest detection (level / width / frequency)
        float maxDb = 0;                 // strongest level seen (raw)
        bool shown = false;              // currently among the strongest signals
        bool everShown = false;
        int hits = 0;                    // sweeps it was detected in
        int misses = 0;                  // sweeps in a row it was not detected in
        int64_t firstNs = 0;
        int64_t lastNs = 0;
        bool confirmed = false;
    };

    // Turns the per-sweep detections into stable signals: a signal must repeat
    // to be shown, stays for holdSeconds after it vanished, and is kept in a
    // history list for a while longer.
    class PeakTracker {
    public:
        void clear() { tracks.clear(); }
        void update(const std::vector<DetectedPeak>& detected, int64_t nowNs, double toleranceHz, const PeakDetectorConfig& cfg);
        // Confirmed signals seen within holdSeconds, sorted by frequency.
        std::vector<DetectedPeak> active(int64_t nowNs, const PeakDetectorConfig& cfg) const;
        // Signals that were shown at some point, including the ones that are gone, strongest first.
        std::vector<TrackedSignal> history() const;

        static constexpr double HISTORY_SECONDS = 120.0;
        static constexpr size_t MAX_TRACKS = 200;

    private:
        std::vector<TrackedSignal> tracks;
    };
}
