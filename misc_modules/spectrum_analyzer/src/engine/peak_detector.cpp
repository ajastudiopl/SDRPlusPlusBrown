#include "peak_detector.h"
#include <algorithm>
#include <cmath>

namespace spectrum {
    std::vector<DetectedPeak> detectPeaks(const std::vector<float>& db, double startHz, double binWidthHz,
                                          float noiseFloorDb, const PeakDetectorConfig& cfg) {
        std::vector<DetectedPeak> candidates;
        const int n = (int)db.size();
        if (n == 0 || !(binWidthHz > 0)) { return candidates; }

        float threshold = cfg.minLevelDb;
        if (std::isfinite(noiseFloorDb)) { threshold = std::max(threshold, noiseFloorDb + cfg.aboveNoiseDb); }

        int i = 0;
        while (i < n) {
            if (!(std::isfinite(db[i]) && db[i] >= threshold)) {
                i++;
                continue;
            }
            int runStart = i;
            int best = i;
            while (i < n && std::isfinite(db[i]) && db[i] >= threshold) {
                if (db[i] > db[best]) { best = i; }
                i++;
            }
            int runLen = i - runStart;
            double width = (double)runLen * binWidthHz;
            if (width + 1e-9 < cfg.minWidthHz) { continue; }
            DetectedPeak p;
            p.bin = best;
            p.freqHz = startHz + ((double)best + 0.5) * binWidthHz;
            p.levelDb = db[best];
            p.aboveNoiseDb = std::isfinite(noiseFloorDb) ? db[best] - noiseFloorDb : 0.0f;
            p.widthHz = width;
            candidates.push_back(p);
        }

        // Strongest first, enforce minimum spacing.
        std::sort(candidates.begin(), candidates.end(), [](const DetectedPeak& a, const DetectedPeak& b) { return a.levelDb > b.levelDb; });
        std::vector<DetectedPeak> accepted;
        for (auto& c : candidates) {
            if ((int)accepted.size() >= cfg.maxPeaks) { break; }
            bool tooClose = false;
            for (auto& a : accepted) {
                if (std::fabs(a.freqHz - c.freqHz) < cfg.minSpacingHz) {
                    tooClose = true;
                    break;
                }
            }
            if (!tooClose) { accepted.push_back(c); }
        }
        std::sort(accepted.begin(), accepted.end(), [](const DetectedPeak& a, const DetectedPeak& b) { return a.freqHz < b.freqHz; });
        return accepted;
    }
}
