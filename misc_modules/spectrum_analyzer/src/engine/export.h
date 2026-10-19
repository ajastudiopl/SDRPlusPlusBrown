#pragma once
#include "sweep_engine.h"
#include <json.hpp>

namespace spectrum {
    struct MeasurementMetadata {
        std::string application = "SDR++ by AJA - Spectrum Analyzer";
        std::string device;            // human readable device label
        std::string deviceId;          // serial / source name
        std::string backend;           // "HackRF native sweep", "Generic stepped sweep", "Demo (synthetic)"
        bool synthetic = false;        // true for demo data - never a real measurement
        double sampleRateHz = 0;
        int fftSize = 0;
        std::string window;
        std::string detector;
        double rbwHz = 0;
        double fftBinHz = 0;
        int segments = 0;
        double stepHz = 0;
        double usableBandwidthHz = 0;
        std::string gains;             // human readable gain summary
        int64_t unixTimeMs = 0;
    };

    // SDR Patrol integration (data model only): one detected signal.
    struct SignalReport {
        double freqHz = 0;
        float levelDb = 0;
        float aboveNoiseDb = 0;
        double widthHz = 0;
        int64_t timestampUnixMs = 0;
        double rangeStartHz = 0;
        double rangeStopHz = 0;
        std::string device;
        std::string backend;
        bool synthetic = false;
        double rbwHz = 0;
        int fftSize = 0;
        std::string detector;
        double sampleRateHz = 0;
        std::string markerName;        // name of a user marker near the signal, if any
    };

    inline constexpr const char* SIGNALS_SCHEMA = "sdrpp-aja.spectrum-analyzer.signals/1";
    inline constexpr const char* SNAPSHOT_SCHEMA = "sdrpp-aja.spectrum-analyzer.snapshot/1";

    std::string isoTimeUtc(int64_t unixMs);
    int64_t unixTimeMsNow();

    std::vector<SignalReport> buildSignalReports(const std::vector<DetectedPeak>& peaks, const std::vector<Marker>& markers,
                                                 const MeasurementMetadata& meta, double rangeStartHz, double rangeStopHz,
                                                 double markerToleranceHz);
    nlohmann::json signalReportToJson(const SignalReport& r);
    nlohmann::json signalsDocument(const std::vector<SignalReport>& reports, const MeasurementMetadata& meta);

    // Snapshot metadata document (range, sweep parameters, stats, markers, signals).
    nlohmann::json snapshotDocument(const MeasurementMetadata& meta, const EngineSnapshot& snap, const std::vector<Marker>& markers,
                                    const std::vector<SignalReport>& signals, const std::string& csvFile, const std::string& pngFile);

    // CSV with the measured values of every bin of the wideband grid (not screen pixels).
    bool writeSpectrumCsv(const std::string& path, const MeasurementMetadata& meta, const EngineSnapshot& snap, std::string& error);
    bool writeTextFile(const std::string& path, const std::string& text, std::string& error);

    // Minimal PNG encoder (RGBA8, uncompressed deflate) - no external dependency.
    bool writePngRgba(const std::string& path, int width, int height, const uint8_t* rgba, std::string& error);
    std::vector<uint8_t> encodePngRgba(int width, int height, const uint8_t* rgba);

    // Rendering helpers for image export. `lut` maps quantised waterfall levels to RGBA.
    std::vector<uint8_t> renderWaterfallImage(const WaterfallHistory& history, const uint32_t lut[256], int& width, int& height);
    std::vector<uint8_t> renderSpectrumImage(const EngineSnapshot& snap, float minDb, float maxDb, int width, int height);
}
