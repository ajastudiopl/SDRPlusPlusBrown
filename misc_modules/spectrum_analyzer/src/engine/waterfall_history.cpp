#include "waterfall_history.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace spectrum {
    void WaterfallHistory::configure(int width, int capacity, size_t maxBytes) {
        std::lock_guard<std::mutex> lck(mtx);
        w = std::max(width, 1);
        cap = std::max(capacity, 1);
        size_t maxRows = std::max<size_t>(1, maxBytes / (size_t)w);
        if ((size_t)cap > maxRows) { cap = (int)maxRows; }
        data.assign((size_t)w * (size_t)cap, QUANT_NAN);
        data.shrink_to_fit();
        infos.assign(cap, WaterfallRowInfo());
        infos.shrink_to_fit();
        head = 0;
        count = 0;
        total = 0;
        gen++;
    }

    void WaterfallHistory::clear() {
        std::lock_guard<std::mutex> lck(mtx);
        std::fill(data.begin(), data.end(), QUANT_NAN);
        head = 0;
        count = 0;
        gen++;
    }

    uint8_t WaterfallHistory::quantize(float db) {
        if (!std::isfinite(db)) { return QUANT_NAN; }
        float t = (db - QUANT_MIN_DB) / (QUANT_MAX_DB - QUANT_MIN_DB);
        t = std::clamp(t, 0.0f, 1.0f);
        return (uint8_t)std::lround(t * 254.0f);
    }

    float WaterfallHistory::dequantize(uint8_t q) {
        if (q == QUANT_NAN) { return nanValue(); }
        return QUANT_MIN_DB + ((float)q / 254.0f) * (QUANT_MAX_DB - QUANT_MIN_DB);
    }

    void WaterfallHistory::push(const float* db, int width, const WaterfallRowInfo& info) {
        std::lock_guard<std::mutex> lck(mtx);
        if (cap <= 0 || width != w) { return; }
        uint8_t* row = &data[(size_t)head * (size_t)w];
        for (int i = 0; i < w; i++) { row[i] = quantize(db[i]); }
        infos[head] = info;
        head = (head + 1) % cap;
        count = std::min(count + 1, cap);
        total++;
    }

    const uint8_t* WaterfallHistory::rowByAge(int age, WaterfallRowInfo* info) const {
        if (age < 0 || age >= count) { return nullptr; }
        int idx = (head - 1 - age + cap) % cap;
        if (info) { *info = infos[idx]; }
        return &data[(size_t)idx * (size_t)w];
    }
}
