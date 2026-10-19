#include "analyzer_settings.h"
#include <algorithm>
#include <cmath>

using nlohmann::json;

namespace spectrum {
    namespace {
        template <typename T>
        void read(const json& j, const char* key, T& dst) {
            if (!j.is_object() || !j.contains(key)) { return; }
            try {
                T v = j.at(key).get<T>();
                dst = v;
            }
            catch (...) {}
        }

        template <typename E>
        void readEnum(const json& j, const char* key, E& dst, int maxValue) {
            int v = (int)dst;
            read(j, key, v);
            if (v >= 0 && v <= maxValue) { dst = (E)v; }
        }

        bool isPow2(int v) { return v > 0 && (v & (v - 1)) == 0; }
    }

    json settingsToJson(const AnalyzerSettings& s) {
        json j;
        j["range"]["start"] = s.range.startHz;
        j["range"]["stop"] = s.range.stopHz;

        json& sw = j["sweep"];
        sw["mode"] = (int)s.sweep.mode;
        sw["autoParams"] = s.sweep.autoParams;
        sw["fftSize"] = s.sweep.fftSize;
        sw["window"] = (int)s.sweep.window;
        sw["framesPerCapture"] = s.sweep.framesPerCapture;
        sw["detector"] = (int)s.sweep.detector;
        sw["displayBins"] = s.sweep.displayBins;
        sw["usableFraction"] = s.sweep.usableFraction;
        sw["overlapFraction"] = s.sweep.overlapFraction;
        sw["dcGuardHz"] = s.sweep.dcGuardHz;
        sw["stepHz"] = s.sweep.stepHz;
        sw["dwellMs"] = s.sweep.dwellMs;
        sw["settleMs"] = s.sweep.settleMs;
        sw["direction"] = (int)s.sweep.direction;
        sw["refreshLimitHz"] = s.sweep.refreshLimitHz;
        sw["nativeDwellBlocks"] = s.sweep.nativeDwellBlocks;

        json& d = j["display"];
        d["minDb"] = s.display.minDb;
        d["maxDb"] = s.display.maxDb;
        d["autoScale"] = s.display.autoScale;
        d["showCurrent"] = s.display.showCurrent;
        d["showAverage"] = s.display.showAverage;
        d["showMaxHold"] = s.display.showMaxHold;
        d["showPersistence"] = s.display.showPersistence;
        d["showNoiseFloor"] = s.display.showNoiseFloor;
        d["showGrid"] = s.display.showGrid;
        d["showPartial"] = s.display.showPartial;
        d["showPeaks"] = s.display.showPeaks;
        d["showMarkers"] = s.display.showMarkers;
        d["spectrumHeightFrac"] = s.display.spectrumHeightFrac;
        d["averageCount"] = s.display.proc.averageCount;
        d["peakDecayDbPerSec"] = s.display.proc.peakDecayDbPerSec;
        d["persistence"] = s.display.proc.persistence;
        d["noisePercentile"] = s.display.proc.noisePercentile;

        json& w = j["waterfall"];
        w["palette"] = s.waterfall.palette;
        w["minDb"] = s.waterfall.minDb;
        w["maxDb"] = s.waterfall.maxDb;
        w["brightness"] = s.waterfall.brightness;
        w["contrast"] = s.waterfall.contrast;
        w["historyRows"] = s.waterfall.historyRows;
        w["sweepsPerLine"] = s.waterfall.sweepsPerLine;
        w["autoLevels"] = s.waterfall.autoLevels;
        w["autoRangeDb"] = s.waterfall.autoRangeDb;

        j["gains"] = json::object();
        for (auto& [k, v] : s.gains) { j["gains"][k] = v; }

        json& p = j["peaks"];
        p["enabled"] = s.peaksEnabled;
        p["onAverage"] = s.peaksOnAverage;
        p["minLevelDb"] = s.peaks.minLevelDb;
        p["aboveNoiseDb"] = s.peaks.aboveNoiseDb;
        p["minSpacingHz"] = s.peaks.minSpacingHz;
        p["minWidthHz"] = s.peaks.minWidthHz;
        p["maxPeaks"] = s.peaks.maxPeaks;
        return j;
    }

    void settingsFromJson(const json& j, AnalyzerSettings& s) {
        if (!j.is_object()) { return; }
        if (j.contains("range")) {
            read(j["range"], "start", s.range.startHz);
            read(j["range"], "stop", s.range.stopHz);
        }
        if (j.contains("sweep")) {
            const json& sw = j["sweep"];
            readEnum(sw, "mode", s.sweep.mode, 2);
            read(sw, "autoParams", s.sweep.autoParams);
            read(sw, "fftSize", s.sweep.fftSize);
            readEnum(sw, "window", s.sweep.window, 4);
            read(sw, "framesPerCapture", s.sweep.framesPerCapture);
            readEnum(sw, "detector", s.sweep.detector, 1);
            read(sw, "displayBins", s.sweep.displayBins);
            read(sw, "usableFraction", s.sweep.usableFraction);
            read(sw, "overlapFraction", s.sweep.overlapFraction);
            read(sw, "dcGuardHz", s.sweep.dcGuardHz);
            read(sw, "stepHz", s.sweep.stepHz);
            read(sw, "dwellMs", s.sweep.dwellMs);
            read(sw, "settleMs", s.sweep.settleMs);
            readEnum(sw, "direction", s.sweep.direction, 1);
            read(sw, "refreshLimitHz", s.sweep.refreshLimitHz);
            read(sw, "nativeDwellBlocks", s.sweep.nativeDwellBlocks);
        }
        if (j.contains("display")) {
            const json& d = j["display"];
            read(d, "minDb", s.display.minDb);
            read(d, "maxDb", s.display.maxDb);
            read(d, "autoScale", s.display.autoScale);
            read(d, "showCurrent", s.display.showCurrent);
            read(d, "showAverage", s.display.showAverage);
            read(d, "showMaxHold", s.display.showMaxHold);
            read(d, "showPersistence", s.display.showPersistence);
            read(d, "showNoiseFloor", s.display.showNoiseFloor);
            read(d, "showGrid", s.display.showGrid);
            read(d, "showPartial", s.display.showPartial);
            read(d, "showPeaks", s.display.showPeaks);
            read(d, "showMarkers", s.display.showMarkers);
            read(d, "spectrumHeightFrac", s.display.spectrumHeightFrac);
            read(d, "averageCount", s.display.proc.averageCount);
            read(d, "peakDecayDbPerSec", s.display.proc.peakDecayDbPerSec);
            read(d, "persistence", s.display.proc.persistence);
            read(d, "noisePercentile", s.display.proc.noisePercentile);
        }
        if (j.contains("waterfall")) {
            const json& w = j["waterfall"];
            read(w, "palette", s.waterfall.palette);
            read(w, "minDb", s.waterfall.minDb);
            read(w, "maxDb", s.waterfall.maxDb);
            read(w, "brightness", s.waterfall.brightness);
            read(w, "contrast", s.waterfall.contrast);
            read(w, "historyRows", s.waterfall.historyRows);
            read(w, "sweepsPerLine", s.waterfall.sweepsPerLine);
            read(w, "autoLevels", s.waterfall.autoLevels);
            read(w, "autoRangeDb", s.waterfall.autoRangeDb);
        }
        if (j.contains("gains") && j["gains"].is_object()) {
            for (auto& [k, v] : j["gains"].items()) {
                if (v.is_number()) { s.gains[k] = v.get<double>(); }
            }
        }
        if (j.contains("peaks")) {
            const json& p = j["peaks"];
            read(p, "enabled", s.peaksEnabled);
            read(p, "onAverage", s.peaksOnAverage);
            read(p, "minLevelDb", s.peaks.minLevelDb);
            read(p, "aboveNoiseDb", s.peaks.aboveNoiseDb);
            read(p, "minSpacingHz", s.peaks.minSpacingHz);
            read(p, "minWidthHz", s.peaks.minWidthHz);
            read(p, "maxPeaks", s.peaks.maxPeaks);
        }
        clampSettings(s);
    }

    void clampSettings(AnalyzerSettings& s) {
        if (!std::isfinite(s.range.startHz) || s.range.startHz < 0) { s.range.startHz = 88e6; }
        if (!std::isfinite(s.range.stopHz) || s.range.stopHz <= s.range.startHz) { s.range.stopHz = s.range.startHz + 20e6; }
        auto& sw = s.sweep;
        if (!isPow2(sw.fftSize)) { sw.fftSize = 2048; }
        sw.fftSize = std::clamp(sw.fftSize, 64, 65536);
        sw.framesPerCapture = std::clamp(sw.framesPerCapture, 1, 256);
        sw.displayBins = std::clamp(sw.displayBins, 256, 65536);
        sw.usableFraction = std::clamp(sw.usableFraction, 0.1, 1.0);
        sw.overlapFraction = std::clamp(sw.overlapFraction, 0.0, 0.9);
        sw.dcGuardHz = std::max(0.0, sw.dcGuardHz);
        sw.stepHz = std::max(0.0, sw.stepHz);
        sw.dwellMs = std::clamp(sw.dwellMs, 0.0, 10000.0);
        sw.settleMs = std::clamp(sw.settleMs, 0.0, 5000.0);
        sw.refreshLimitHz = std::clamp(sw.refreshLimitHz, 0.0, 1000.0);
        sw.nativeDwellBlocks = std::clamp(sw.nativeDwellBlocks, 1, 16);
        auto& d = s.display;
        if (!(d.maxDb > d.minDb)) {
            d.minDb = -110.0f;
            d.maxDb = -10.0f;
        }
        d.minDb = std::clamp(d.minDb, -200.0f, 50.0f);
        d.maxDb = std::clamp(d.maxDb, d.minDb + 1.0f, 60.0f);
        d.spectrumHeightFrac = std::clamp(d.spectrumHeightFrac, 0.15f, 0.85f);
        d.proc.averageCount = std::clamp(d.proc.averageCount, 1, 1000);
        d.proc.persistence = std::clamp(d.proc.persistence, 0, 32);
        d.proc.peakDecayDbPerSec = std::clamp(d.proc.peakDecayDbPerSec, 0.0f, 200.0f);
        d.proc.noisePercentile = std::clamp(d.proc.noisePercentile, 0.01f, 0.9f);
        auto& w = s.waterfall;
        if (!(w.maxDb > w.minDb)) {
            w.minDb = -110.0f;
            w.maxDb = -30.0f;
        }
        w.brightness = std::clamp(w.brightness, -1.0f, 1.0f);
        w.contrast = std::clamp(w.contrast, 0.2f, 5.0f);
        w.historyRows = std::clamp(w.historyRows, 16, 2048); // texture height limit on mobile GPUs
        w.sweepsPerLine = std::clamp(w.sweepsPerLine, 1, 100);
        w.autoRangeDb = std::clamp(w.autoRangeDb, 10.0f, 150.0f);
        s.peaks.maxPeaks = std::clamp(s.peaks.maxPeaks, 1, 500);
        s.peaks.minSpacingHz = std::max(0.0, s.peaks.minSpacingHz);
        s.peaks.minWidthHz = std::max(0.0, s.peaks.minWidthHz);
    }

    std::vector<Preset> builtInPresets(const AnalyzerSettings& base) {
        struct Def {
            const char* name;
            double start, stop;
            int bins;
        };
        const Def defs[] = {
            { "FM Broadcast (88-108 MHz)", 88e6, 108e6, 4096 },
            { "Airband (118-137 MHz)", 118e6, 137e6, 4096 },
            { "VHF 2 m (144-146 MHz)", 144e6, 146e6, 2048 },
            { "UHF 70 cm (430-440 MHz)", 430e6, 440e6, 4096 },
            { "UHF (400-470 MHz)", 400e6, 470e6, 8192 },
            { "Wideband (1-1000 MHz)", 1e6, 1000e6, 16384 },
        };
        std::vector<Preset> out;
        for (auto& d : defs) {
            Preset p;
            p.name = d.name;
            p.builtIn = true;
            p.settings = base;
            p.settings.range.startHz = d.start;
            p.settings.range.stopHz = d.stop;
            p.settings.sweep.displayBins = d.bins;
            out.push_back(p);
        }
        return out;
    }

    json markersToJson(const std::vector<Marker>& m) {
        json arr = json::array();
        for (auto& mk : m) {
            json e;
            e["name"] = mk.name;
            e["freq"] = mk.freqHz;
            arr.push_back(e);
        }
        return arr;
    }

    std::vector<Marker> markersFromJson(const json& j) {
        std::vector<Marker> out;
        if (!j.is_array()) { return out; }
        for (auto& e : j) {
            Marker m;
            read(e, "name", m.name);
            read(e, "freq", m.freqHz);
            if (std::isfinite(m.freqHz) && m.freqHz >= 0) { out.push_back(m); }
        }
        return out;
    }
}
