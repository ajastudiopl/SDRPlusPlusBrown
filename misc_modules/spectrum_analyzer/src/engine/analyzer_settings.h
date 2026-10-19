#pragma once
#include "display_processor.h"
#include "peak_detector.h"
#include <json.hpp>
#include <map>

namespace spectrum {
    struct SweepSettings {
        SweepMode mode = SweepMode::AUTO;
        bool autoParams = true;          // derive FFT size / overlap / settling from the device
        int fftSize = 2048;
        WindowType window = WindowType::BLACKMAN_HARRIS;
        int framesPerCapture = 1;        // FFT averaging per segment
        Detector detector = Detector::PEAK;
        int displayBins = 4096;          // "Sweep Resolution" (points of the wideband trace)
        double usableFraction = 0.8;     // generic: usable part of the sample rate
        double overlapFraction = 0.55;   // generic: overlap of neighbouring segments
        double dcGuardHz = 0;            // generic: 0 = auto (a few FFT bins)
        double stepHz = 0;               // generic: 0 = derived from usable bandwidth and overlap
        double dwellMs = 0;              // generic: capture time per segment, 0 = one FFT block set
        double settleMs = 50;            // generic: discard time after each retune
        SweepDirection direction = SweepDirection::UP;
        double refreshLimitHz = 0;       // max sweeps per second, 0 = unlimited
        int nativeDwellBlocks = 1;       // native (HackRF): 16384-byte blocks per tuning
    };

    struct DisplayConfig {
        float minDb = -110.0f;
        float maxDb = -10.0f;
        bool autoScale = false;
        bool showCurrent = true;
        bool showAverage = false;
        bool showMaxHold = true;
        bool showPersistence = false;
        bool showNoiseFloor = true;
        bool showGrid = true;
        bool showPartial = true;         // sweep in progress, drawn dimmed and labelled
        bool showPeaks = true;
        bool showMarkers = true;
        float spectrumHeightFrac = 0.45f;
        DisplaySettings proc;
    };

    struct WaterfallConfig {
        std::string palette = "Turbo";
        float minDb = -110.0f;           // dynamic range
        float maxDb = -30.0f;
        float brightness = 0.0f;         // -1 .. 1
        float contrast = 1.0f;           // 0.2 .. 5
        int historyRows = 400;
        int sweepsPerLine = 1;           // "Waterfall speed": complete sweeps per row
        bool paused = false;             // freeze display (acquisition continues)
    };

    struct AnalyzerSettings {
        FrequencyRange range;
        SweepSettings sweep;
        DisplayConfig display;
        WaterfallConfig waterfall;
        std::map<std::string, double> gains; // device gain id -> value
        PeakDetectorConfig peaks;
        bool peaksEnabled = true;
        bool peaksOnAverage = false;
    };

    nlohmann::json settingsToJson(const AnalyzerSettings& s);
    // Reads every known key that is present and valid; others keep their value.
    void settingsFromJson(const nlohmann::json& j, AnalyzerSettings& s);
    // Sanitises values loaded from config / presets.
    void clampSettings(AnalyzerSettings& s);

    struct Preset {
        std::string name;
        AnalyzerSettings settings;
        bool builtIn = false;
    };
    std::vector<Preset> builtInPresets(const AnalyzerSettings& base);

    nlohmann::json markersToJson(const std::vector<Marker>& m);
    std::vector<Marker> markersFromJson(const nlohmann::json& j);
}
