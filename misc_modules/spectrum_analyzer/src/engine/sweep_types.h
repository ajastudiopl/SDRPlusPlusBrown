#pragma once
// Spectrum Analyzer engine: common data types.
// Everything in src/engine is free of SDR++ core dependencies so it can be
// unit tested on its own (see misc_modules/spectrum_analyzer/tests).

#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace spectrum {
    typedef std::complex<float> cf32;

    enum class SweepMode {
        AUTO = 0,    // native if the device supports it, otherwise generic
        NATIVE = 1,  // hardware sweep (HackRF)
        GENERIC = 2  // software stepped sweep through the receiver source
    };

    enum class SweepDirection {
        UP = 0,
        DOWN = 1
    };

    enum class Detector {
        PEAK = 0,    // max of all FFT bins falling into a display bin (never hides narrow signals)
        AVERAGE = 1  // mean power of all FFT bins falling into a display bin
    };

    enum class WindowType {
        RECTANGULAR = 0,
        HANN = 1,
        BLACKMAN = 2,
        NUTTALL = 3,
        BLACKMAN_HARRIS = 4
    };

    enum class SweepState {
        IDLE = 0,
        STARTING,
        SWEEPING,
        PAUSED,
        STOPPING,
        FAULT,
        DEVICE_DISCONNECTED
    };

    const char* sweepStateName(SweepState s);

    inline float nanValue() { return std::numeric_limits<float>::quiet_NaN(); }

    struct FrequencyRange {
        double startHz = 88e6;
        double stopHz = 108e6;

        double centerHz() const { return (startHz + stopHz) / 2.0; }
        double spanHz() const { return stopHz - startHz; }
        static FrequencyRange fromCenterSpan(double centerHz, double spanHz) {
            FrequencyRange r;
            r.startHz = centerHz - spanHz / 2.0;
            r.stopHz = centerHz + spanHz / 2.0;
            return r;
        }
    };

    struct RangeLimits {
        double minHz = 0;
        double maxHz = 0;
        double minSpanHz = 1e3;
        double maxSpanHz = 0; // 0 = no limit
    };

    // Returns an empty string when the range is valid, otherwise a message.
    std::string validateRange(const FrequencyRange& range, const RangeLimits& limits);

    // Part of a capture used for the wideband spectrum, as offsets from the LO.
    // Higher priority wins when several segments cover the same display bin;
    // equal priorities are combined with the selected detector.
    struct ValidWindow {
        double loOffsetHz;
        double hiOffsetHz;
        uint8_t priority;
    };

    enum : uint8_t {
        PRIORITY_DC = 1,    // bins next to the LO (DC spike / LO leakage)
        PRIORITY_CLEAN = 2
    };

    struct Segment {
        int index = 0;      // acquisition order inside one sweep
        double loHz = 0;    // tuned frequency
        std::vector<ValidWindow> windows;
    };

    struct SweepPlan {
        FrequencyRange range;       // range shown to the user
        double sampleRateHz = 0;
        double usableBandwidthHz = 0;
        double stepHz = 0;          // LO step between consecutive segments
        double dcGuardHz = 0;
        bool dcCoveredByOverlap = true;
        bool native = false;
        std::vector<Segment> segments;
        std::vector<std::string> warnings;
        std::string error;

        bool valid() const { return error.empty() && !segments.empty(); }
    };

    struct CoverageReport {
        double coveredHz = 0;        // covered by clean (priority 2) data
        double coveredAnyHz = 0;     // covered by any data
        double maxCleanGapHz = 0;
        double maxGapHz = 0;
        int gapCount = 0;
    };

    // A finished (or interrupted) sweep assembled on the display grid.
    struct SweepFrame {
        uint64_t sequence = 0;
        double startHz = 0;
        double stopHz = 0;
        std::vector<float> db;      // NaN where no data was measured
        bool complete = false;      // all segments of the plan were measured
        bool interrupted = false;   // sweep aborted (stop/pause/restart)
        int segmentsExpected = 0;
        int segmentsReceived = 0;
        int64_t startTimeNs = 0;
        int64_t endTimeNs = 0;

        int bins() const { return (int)db.size(); }
        double binWidthHz() const { return db.empty() ? 0.0 : (stopHz - startHz) / (double)db.size(); }
        double binCenterHz(int i) const { return startHz + (i + 0.5) * binWidthHz(); }
        double durationSec() const { return (double)(endTimeNs - startTimeNs) * 1e-9; }
    };

    struct DetectedPeak {
        double freqHz = 0;
        float levelDb = 0;
        float aboveNoiseDb = 0;
        double widthHz = 0;
        int bin = 0;
    };

    struct Marker {
        std::string name;
        double freqHz = 0;
    };

    int64_t nowNs();
    std::string formatFrequency(double hz, int decimals = -1);
}
