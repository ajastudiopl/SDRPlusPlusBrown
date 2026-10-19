#include "display_processor.h"
#include <algorithm>
#include <cmath>

namespace spectrum {
    void DisplayProcessor::reset(int bins) {
        cur.assign(bins, nanValue());
        avgLin.assign(bins, nanValue());
        avgDb.assign(bins, nanValue());
        maxH.assign(bins, nanValue());
        persist.clear();
        noiseFloor = nanValue();
        peak = nanValue();
        pushed = 0;
        avgFrames = 0;
        lastTimeNs = 0;
    }

    void DisplayProcessor::setSettings(const DisplaySettings& s) {
        bool avgChanged = s.averageCount != cfg.averageCount;
        cfg = s;
        cfg.averageCount = std::clamp(cfg.averageCount, 1, 1000);
        cfg.persistence = std::clamp(cfg.persistence, 0, 32);
        cfg.noisePercentile = std::clamp(cfg.noisePercentile, 0.01f, 0.9f);
        cfg.peakDecayDbPerSec = std::max(cfg.peakDecayDbPerSec, 0.0f);
        while ((int)persist.size() > cfg.persistence) { persist.pop_front(); }
        if (avgChanged) { avgFrames = std::min(avgFrames, cfg.averageCount - 1); }
    }

    void DisplayProcessor::resetAverage() {
        std::fill(avgLin.begin(), avgLin.end(), nanValue());
        std::fill(avgDb.begin(), avgDb.end(), nanValue());
        avgFrames = 0;
    }

    void DisplayProcessor::resetMaxHold() {
        std::fill(maxH.begin(), maxH.end(), nanValue());
    }

    float DisplayProcessor::percentile(const std::vector<float>& v, float p, std::vector<float>& tmp) {
        tmp.clear();
        for (float x : v) {
            if (std::isfinite(x)) { tmp.push_back(x); }
        }
        if (tmp.empty()) { return nanValue(); }
        size_t k = (size_t)std::floor(p * (float)(tmp.size() - 1));
        std::nth_element(tmp.begin(), tmp.begin() + k, tmp.end());
        return tmp[k];
    }

    void DisplayProcessor::push(const SweepFrame& f) {
        const int n = (int)f.db.size();
        if (n != (int)cur.size()) { reset(n); }

        if (cfg.persistence > 0 && pushed > 0) {
            persist.push_back(cur);
            while ((int)persist.size() > cfg.persistence) { persist.pop_front(); }
        }
        cur = f.db;

        // Exponential average in linear power; NaN never overwrites valid data.
        int k = std::min(avgFrames + 1, cfg.averageCount);
        float alpha = 1.0f / (float)k;
        for (int i = 0; i < n; i++) {
            float x = f.db[i];
            if (!std::isfinite(x)) { continue; }
            float lin = powf(10.0f, x / 10.0f);
            if (!std::isfinite(avgLin[i])) { avgLin[i] = lin; }
            else { avgLin[i] += (lin - avgLin[i]) * alpha; }
            avgDb[i] = 10.0f * log10f(std::max(avgLin[i], 1e-30f));
        }
        avgFrames = std::min(avgFrames + 1, cfg.averageCount);

        // Max hold with optional decay (dB per second of measurement time).
        float decay = 0.0f;
        if (cfg.peakDecayDbPerSec > 0.0f && lastTimeNs != 0 && f.endTimeNs > lastTimeNs) {
            decay = cfg.peakDecayDbPerSec * (float)((double)(f.endTimeNs - lastTimeNs) * 1e-9);
        }
        for (int i = 0; i < n; i++) {
            float x = f.db[i];
            float m = maxH[i];
            if (std::isfinite(m)) { m -= decay; }
            if (std::isfinite(x) && (!std::isfinite(m) || x > m)) { m = x; }
            maxH[i] = m;
        }
        lastTimeNs = f.endTimeNs;

        noiseFloor = percentile(cur, cfg.noisePercentile, scratch);
        peak = nanValue();
        for (float x : cur) {
            if (std::isfinite(x) && (!std::isfinite(peak) || x > peak)) { peak = x; }
        }
        pushed++;
    }
}
