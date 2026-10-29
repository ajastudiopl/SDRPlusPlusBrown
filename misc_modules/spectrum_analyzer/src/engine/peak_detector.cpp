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

    void PeakTracker::update(const std::vector<DetectedPeak>& detected, int64_t nowNs, double toleranceHz, const PeakDetectorConfig& cfg) {
        std::vector<bool> seen(tracks.size(), false);
        for (auto& d : detected) {
            int best = -1;
            double bestDist = toleranceHz;
            for (int i = 0; i < (int)tracks.size(); i++) {
                if (seen[i]) { continue; }
                double dist = std::fabs(tracks[i].peak.freqHz - d.freqHz);
                if (dist <= bestDist) {
                    bestDist = dist;
                    best = i;
                }
            }
            if (best < 0) {
                TrackedSignal t;
                t.peak = d;
                t.maxDb = d.levelDb;
                t.hits = 1;
                t.firstNs = t.lastNs = nowNs;
                t.confirmed = cfg.confirmHits <= 1;
                tracks.push_back(t);
                seen.push_back(true);
                continue;
            }
            TrackedSignal& t = tracks[best];
            // Smoothed frequency and level: the label must not jitter from sweep to sweep.
            const double a = 0.25;
            double freq = t.peak.freqHz + a * (d.freqHz - t.peak.freqHz);
            float level = t.peak.levelDb + (float)a * (d.levelDb - t.peak.levelDb);
            t.peak = d;
            t.peak.freqHz = freq;
            t.peak.levelDb = level;
            t.maxDb = std::max(t.maxDb, d.levelDb);
            t.hits++;
            t.misses = 0;
            t.lastNs = nowNs;
            if (t.hits >= cfg.confirmHits) { t.confirmed = true; }
            seen[best] = true;
        }
        // Old ones leave the history.
        const int64_t keepNs = (int64_t)(HISTORY_SECONDS * 1e9);
        std::vector<TrackedSignal> kept;
        kept.reserve(tracks.size());
        for (size_t i = 0; i < tracks.size(); i++) {
            const TrackedSignal& t = tracks[i];
            bool wasSeen = i < seen.size() && seen[i];
            TrackedSignal copy = t;
            if (!wasSeen) { copy.misses++; }
            // A candidate may miss one sweep (signal near the threshold), two means it was noise.
            if (!copy.confirmed && copy.misses > 1) { continue; }
            if (nowNs - copy.lastNs > keepNs) { continue; }
            kept.push_back(copy);
        }
        if (kept.size() > MAX_TRACKS) {
            std::sort(kept.begin(), kept.end(), [](const TrackedSignal& a, const TrackedSignal& b) { return a.lastNs > b.lastNs; });
            kept.resize(MAX_TRACKS);
        }
        tracks.swap(kept);

        // Pick the strongest live signals. A signal that is already shown keeps its
        // place until it falls clearly below the limit (hysteresis).
        const int64_t holdNs = (int64_t)(std::max(0.0, cfg.holdSeconds) * 1e9);
        const int limit = std::max(1, cfg.maxShown);
        std::vector<int> live;
        for (int i = 0; i < (int)tracks.size(); i++) {
            tracks[i].shown = tracks[i].shown && tracks[i].confirmed && nowNs - tracks[i].lastNs <= holdNs;
            if (tracks[i].confirmed && nowNs - tracks[i].lastNs <= holdNs) { live.push_back(i); }
        }
        std::sort(live.begin(), live.end(), [&](int a, int b) { return tracks[a].peak.levelDb > tracks[b].peak.levelDb; });
        int shownCount = 0;
        for (int r = 0; r < (int)live.size(); r++) {
            TrackedSignal& t = tracks[live[r]];
            if (t.shown && (r >= limit + 2 || shownCount >= limit)) { t.shown = false; }
            if (t.shown) { shownCount++; }
        }
        for (int r = 0; r < (int)live.size() && shownCount < limit; r++) {
            TrackedSignal& t = tracks[live[r]];
            if (!t.shown) {
                t.shown = true;
                shownCount++;
            }
        }
        for (auto& t : tracks) {
            if (t.shown) { t.everShown = true; }
        }
    }

    std::vector<DetectedPeak> PeakTracker::active(int64_t nowNs, const PeakDetectorConfig& cfg) const {
        std::vector<DetectedPeak> out;
        const int64_t holdNs = (int64_t)(std::max(0.0, cfg.holdSeconds) * 1e9);
        for (auto& t : tracks) {
            if (t.shown && nowNs - t.lastNs <= holdNs) { out.push_back(t.peak); }
        }
        std::sort(out.begin(), out.end(), [](const DetectedPeak& a, const DetectedPeak& b) { return a.freqHz < b.freqHz; });
        return out;
    }

    std::vector<TrackedSignal> PeakTracker::history() const {
        std::vector<TrackedSignal> out;
        for (auto& t : tracks) {
            if (t.everShown) { out.push_back(t); }
        }
        std::sort(out.begin(), out.end(), [](const TrackedSignal& a, const TrackedSignal& b) { return a.maxDb > b.maxDb; });
        return out;
    }
}
