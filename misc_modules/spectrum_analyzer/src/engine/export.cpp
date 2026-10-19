#include "export.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>

using nlohmann::json;

namespace spectrum {
    std::string isoTimeUtc(int64_t unixMs) {
        time_t t = (time_t)(unixMs / 1000);
        struct tm tmv;
#ifdef _WIN32
        gmtime_s(&tmv, &t);
#else
        gmtime_r(&t, &tmv);
#endif
        char buf[64];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec, (int)(unixMs % 1000));
        return buf;
    }

    int64_t unixTimeMsNow() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    std::vector<SignalReport> buildSignalReports(const std::vector<DetectedPeak>& peaks, const std::vector<Marker>& markers,
                                                 const MeasurementMetadata& meta, double rangeStartHz, double rangeStopHz,
                                                 double markerToleranceHz) {
        std::vector<SignalReport> out;
        for (auto& p : peaks) {
            SignalReport r;
            r.freqHz = p.freqHz;
            r.levelDb = p.levelDb;
            r.aboveNoiseDb = p.aboveNoiseDb;
            r.widthHz = p.widthHz;
            r.timestampUnixMs = meta.unixTimeMs;
            r.rangeStartHz = rangeStartHz;
            r.rangeStopHz = rangeStopHz;
            r.device = meta.device;
            r.backend = meta.backend;
            r.synthetic = meta.synthetic;
            r.rbwHz = meta.rbwHz;
            r.fftSize = meta.fftSize;
            r.detector = meta.detector;
            r.sampleRateHz = meta.sampleRateHz;
            double best = markerToleranceHz;
            for (auto& m : markers) {
                double d = std::fabs(m.freqHz - p.freqHz);
                if (d <= best) {
                    best = d;
                    r.markerName = m.name;
                }
            }
            out.push_back(r);
        }
        return out;
    }

    json signalReportToJson(const SignalReport& r) {
        json j;
        j["frequency_hz"] = r.freqHz;
        j["level_db"] = r.levelDb;
        j["above_noise_db"] = r.aboveNoiseDb;
        j["width_hz"] = r.widthHz;
        j["timestamp"] = isoTimeUtc(r.timestampUnixMs);
        j["timestamp_unix_ms"] = r.timestampUnixMs;
        j["range"]["start_hz"] = r.rangeStartHz;
        j["range"]["stop_hz"] = r.rangeStopHz;
        j["device"] = r.device;
        j["backend"] = r.backend;
        j["synthetic"] = r.synthetic;
        j["sweep"]["rbw_hz"] = r.rbwHz;
        j["sweep"]["fft_size"] = r.fftSize;
        j["sweep"]["detector"] = r.detector;
        j["sweep"]["sample_rate_hz"] = r.sampleRateHz;
        if (!r.markerName.empty()) { j["marker"] = r.markerName; }
        else { j["marker"] = nullptr; }
        return j;
    }

    namespace {
        json metadataJson(const MeasurementMetadata& m) {
            json j;
            j["application"] = m.application;
            j["device"] = m.device;
            j["device_id"] = m.deviceId;
            j["backend"] = m.backend;
            j["synthetic"] = m.synthetic;
            j["sample_rate_hz"] = m.sampleRateHz;
            j["fft_size"] = m.fftSize;
            j["window"] = m.window;
            j["detector"] = m.detector;
            j["rbw_hz"] = m.rbwHz;
            j["fft_bin_hz"] = m.fftBinHz;
            j["segments_per_sweep"] = m.segments;
            j["step_hz"] = m.stepHz;
            j["usable_bandwidth_hz"] = m.usableBandwidthHz;
            j["gains"] = m.gains;
            j["timestamp"] = isoTimeUtc(m.unixTimeMs);
            j["timestamp_unix_ms"] = m.unixTimeMs;
            return j;
        }
    }

    json signalsDocument(const std::vector<SignalReport>& reports, const MeasurementMetadata& meta) {
        json j;
        j["schema"] = SIGNALS_SCHEMA;
        j["measurement"] = metadataJson(meta);
        j["signals"] = json::array();
        for (auto& r : reports) { j["signals"].push_back(signalReportToJson(r)); }
        return j;
    }

    json snapshotDocument(const MeasurementMetadata& meta, const EngineSnapshot& snap, const std::vector<Marker>& markers,
                          const std::vector<SignalReport>& signals, const std::string& csvFile, const std::string& pngFile) {
        json j;
        j["schema"] = SNAPSHOT_SCHEMA;
        j["measurement"] = metadataJson(meta);
        j["range"]["start_hz"] = snap.startHz;
        j["range"]["stop_hz"] = snap.stopHz;
        j["range"]["bins"] = snap.current.size();
        j["range"]["bin_width_hz"] = snap.current.empty() ? 0.0 : (snap.stopHz - snap.startHz) / (double)snap.current.size();
        j["last_sweep"]["sequence"] = snap.lastSequence;
        j["last_sweep"]["complete"] = snap.lastComplete;
        j["last_sweep"]["segments_expected"] = snap.lastSegmentsExpected;
        j["last_sweep"]["segments_received"] = snap.lastSegmentsReceived;
        j["last_sweep"]["duration_s"] = snap.stats.lastSweepSec;
        j["stats"]["sweeps_completed"] = snap.stats.sweepsCompleted;
        j["stats"]["sweeps_incomplete"] = snap.stats.sweepsIncomplete;
        j["stats"]["sweeps_interrupted"] = snap.stats.sweepsInterrupted;
        j["stats"]["measured_sweeps_per_s"] = snap.stats.sweepsPerSec;
        j["stats"]["captures_dropped"] = snap.stats.capturesDropped;
        j["noise_floor_db"] = std::isfinite(snap.noiseFloorDb) ? json(snap.noiseFloorDb) : json(nullptr);
        j["markers"] = json::array();
        for (auto& m : markers) {
            json e;
            e["name"] = m.name;
            e["frequency_hz"] = m.freqHz;
            j["markers"].push_back(e);
        }
        j["signals"] = json::array();
        for (auto& r : signals) { j["signals"].push_back(signalReportToJson(r)); }
        j["files"]["csv"] = csvFile;
        j["files"]["waterfall_png"] = pngFile;
        return j;
    }

    bool writeTextFile(const std::string& path, const std::string& text, std::string& error) {
        std::ofstream f(path, std::ios::binary);
        if (!f.is_open()) {
            error = "Could not open " + path + " for writing";
            return false;
        }
        f << text;
        if (!f.good()) {
            error = "Write error on " + path;
            return false;
        }
        return true;
    }

    bool writeSpectrumCsv(const std::string& path, const MeasurementMetadata& meta, const EngineSnapshot& snap, std::string& error) {
        if (snap.current.empty()) {
            error = "No measurement to export";
            return false;
        }
        std::ofstream f(path, std::ios::binary);
        if (!f.is_open()) {
            error = "Could not open " + path + " for writing";
            return false;
        }
        const int n = (int)snap.current.size();
        const double bw = (snap.stopHz - snap.startHz) / (double)n;
        char line[256];
        f << "# " << meta.application << "\n";
        f << "# timestamp_utc=" << isoTimeUtc(meta.unixTimeMs) << "\n";
        f << "# device=" << meta.device << "\n";
        f << "# backend=" << meta.backend << (meta.synthetic ? " (SYNTHETIC DATA - NOT A MEASUREMENT)" : "") << "\n";
        snprintf(line, sizeof(line), "# start_hz=%.3f stop_hz=%.3f bins=%d bin_width_hz=%.3f\n", snap.startHz, snap.stopHz, n, bw);
        f << line;
        snprintf(line, sizeof(line), "# sample_rate_hz=%.1f fft_size=%d rbw_hz=%.3f window=%s detector=%s\n", meta.sampleRateHz, meta.fftSize,
                 meta.rbwHz, meta.window.c_str(), meta.detector.c_str());
        f << line;
        snprintf(line, sizeof(line), "# sweep_sequence=%llu complete=%d segments=%d/%d\n", (unsigned long long)snap.lastSequence, snap.lastComplete ? 1 : 0,
                 snap.lastSegmentsReceived, snap.lastSegmentsExpected);
        f << line;
        f << "# gains=" << meta.gains << "\n";
        f << "# levels in dBFS (tone calibrated); empty = not measured\n";
        f << "frequency_hz,current_db,average_db,max_hold_db\n";
        auto val = [](const std::vector<float>& v, int i, char* out, size_t sz) {
            if (i < (int)v.size() && std::isfinite(v[i])) { snprintf(out, sz, "%.2f", v[i]); }
            else { out[0] = 0; }
        };
        char a[32], b[32], c[32];
        for (int i = 0; i < n; i++) {
            val(snap.current, i, a, sizeof(a));
            val(snap.average, i, b, sizeof(b));
            val(snap.maxHold, i, c, sizeof(c));
            snprintf(line, sizeof(line), "%.1f,%s,%s,%s\n", snap.startHz + ((double)i + 0.5) * bw, a, b, c);
            f << line;
        }
        if (!f.good()) {
            error = "Write error on " + path;
            return false;
        }
        return true;
    }

    // ---------------- PNG ----------------
    namespace {
        uint32_t crcTable[256];
        bool crcInit = false;

        void initCrc() {
            for (uint32_t n = 0; n < 256; n++) {
                uint32_t c = n;
                for (int k = 0; k < 8; k++) { c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1); }
                crcTable[n] = c;
            }
            crcInit = true;
        }

        uint32_t crc32(const uint8_t* d, size_t n, uint32_t c = 0xFFFFFFFFu) {
            for (size_t i = 0; i < n; i++) { c = crcTable[(c ^ d[i]) & 0xFF] ^ (c >> 8); }
            return c;
        }

        void put32(std::vector<uint8_t>& v, uint32_t x) {
            v.push_back((x >> 24) & 0xFF);
            v.push_back((x >> 16) & 0xFF);
            v.push_back((x >> 8) & 0xFF);
            v.push_back(x & 0xFF);
        }

        void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
            put32(out, (uint32_t)data.size());
            size_t start = out.size();
            out.insert(out.end(), type, type + 4);
            out.insert(out.end(), data.begin(), data.end());
            uint32_t c = crc32(&out[start], out.size() - start) ^ 0xFFFFFFFFu;
            put32(out, c);
        }
    }

    std::vector<uint8_t> encodePngRgba(int width, int height, const uint8_t* rgba) {
        if (!crcInit) { initCrc(); }
        std::vector<uint8_t> out = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
        std::vector<uint8_t> ihdr;
        put32(ihdr, (uint32_t)width);
        put32(ihdr, (uint32_t)height);
        ihdr.push_back(8); // bit depth
        ihdr.push_back(6); // RGBA
        ihdr.push_back(0);
        ihdr.push_back(0);
        ihdr.push_back(0);
        chunk(out, "IHDR", ihdr);

        // Raw scanlines with filter byte 0, wrapped in zlib "stored" blocks.
        const size_t rowBytes = (size_t)width * 4;
        std::vector<uint8_t> raw;
        raw.reserve((rowBytes + 1) * height);
        for (int y = 0; y < height; y++) {
            raw.push_back(0);
            raw.insert(raw.end(), rgba + y * rowBytes, rgba + (y + 1) * rowBytes);
        }
        std::vector<uint8_t> z = { 0x78, 0x01 };
        size_t pos = 0;
        do {
            size_t len = std::min<size_t>(65535, raw.size() - pos);
            bool final = (pos + len) >= raw.size();
            z.push_back(final ? 1 : 0);
            z.push_back(len & 0xFF);
            z.push_back((len >> 8) & 0xFF);
            z.push_back((~len) & 0xFF);
            z.push_back(((~len) >> 8) & 0xFF);
            z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + len);
            pos += len;
        } while (pos < raw.size());
        uint32_t a = 1, b = 0;
        for (uint8_t v : raw) {
            a = (a + v) % 65521;
            b = (b + a) % 65521;
        }
        put32(z, (b << 16) | a);
        chunk(out, "IDAT", z);
        chunk(out, "IEND", {});
        return out;
    }

    bool writePngRgba(const std::string& path, int width, int height, const uint8_t* rgba, std::string& error) {
        if (width <= 0 || height <= 0) {
            error = "Empty image";
            return false;
        }
        std::vector<uint8_t> png = encodePngRgba(width, height, rgba);
        std::ofstream f(path, std::ios::binary);
        if (!f.is_open()) {
            error = "Could not open " + path + " for writing";
            return false;
        }
        f.write((const char*)png.data(), (std::streamsize)png.size());
        if (!f.good()) {
            error = "Write error on " + path;
            return false;
        }
        return true;
    }

    std::vector<uint8_t> renderWaterfallImage(const WaterfallHistory& history, const uint32_t lut[256], int& width, int& height) {
        auto lck = history.lock();
        int srcW = history.width();
        int rows = history.rows();
        width = std::min(srcW, 4096);
        height = rows;
        std::vector<uint8_t> img((size_t)width * std::max(height, 0) * 4, 0);
        for (int y = 0; y < rows; y++) {
            const uint8_t* row = history.rowByAge(y);
            for (int x = 0; x < width; x++) {
                int b0 = (int)((int64_t)x * srcW / width);
                int b1 = std::max(b0 + 1, (int)((int64_t)(x + 1) * srcW / width));
                uint8_t q = WaterfallHistory::QUANT_NAN;
                for (int b = b0; b < b1 && b < srcW; b++) {
                    if (row[b] != WaterfallHistory::QUANT_NAN && (q == WaterfallHistory::QUANT_NAN || row[b] > q)) { q = row[b]; }
                }
                uint32_t c = lut[q];
                uint8_t* p = &img[((size_t)y * width + x) * 4];
                p[0] = c & 0xFF;
                p[1] = (c >> 8) & 0xFF;
                p[2] = (c >> 16) & 0xFF;
                p[3] = 255;
            }
        }
        return img;
    }

    std::vector<uint8_t> renderSpectrumImage(const EngineSnapshot& snap, float minDb, float maxDb, int width, int height) {
        std::vector<uint8_t> img((size_t)width * height * 4, 0);
        auto put = [&](int x, int y, uint32_t c) {
            if (x < 0 || y < 0 || x >= width || y >= height) { return; }
            uint8_t* p = &img[((size_t)y * width + x) * 4];
            p[0] = c & 0xFF;
            p[1] = (c >> 8) & 0xFF;
            p[2] = (c >> 16) & 0xFF;
            p[3] = 255;
        };
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) { put(x, y, 0x101010); }
        }
        // 10 dB grid
        for (float db = std::ceil(minDb / 10.0f) * 10.0f; db <= maxDb; db += 10.0f) {
            int y = (int)((maxDb - db) / (maxDb - minDb) * (float)(height - 1));
            for (int x = 0; x < width; x++) { put(x, y, 0x383838); }
        }
        for (int i = 1; i < 10; i++) {
            int x = i * width / 10;
            for (int y = 0; y < height; y++) { put(x, y, 0x383838); }
        }
        auto trace = [&](const std::vector<float>& v, uint32_t color) {
            const int n = (int)v.size();
            if (n == 0) { return; }
            int prevY = -1;
            for (int x = 0; x < width; x++) {
                int b0 = (int)((int64_t)x * n / width);
                int b1 = std::max(b0 + 1, (int)((int64_t)(x + 1) * n / width));
                float m = nanValue();
                for (int b = b0; b < b1 && b < n; b++) {
                    if (std::isfinite(v[b]) && (!std::isfinite(m) || v[b] > m)) { m = v[b]; }
                }
                if (!std::isfinite(m)) {
                    prevY = -1;
                    continue;
                }
                float t = std::clamp((maxDb - m) / (maxDb - minDb), 0.0f, 1.0f);
                int y = (int)(t * (float)(height - 1));
                int y0 = (prevY < 0) ? y : std::min(prevY, y);
                int y1 = (prevY < 0) ? y : std::max(prevY, y);
                for (int yy = y0; yy <= y1; yy++) { put(x, yy, color); }
                prevY = y;
            }
        };
        trace(snap.maxHold, 0x3030E0);  // red (ABGR little endian -> R in low byte)
        trace(snap.average, 0xE0E030);  // cyan
        trace(snap.current, 0x30E0F0);  // yellow
        return img;
    }
}
