#pragma once
#include "sweep_types.h"
#include <mutex>

namespace spectrum {
    struct WaterfallRowInfo {
        uint64_t sequence = 0;   // sequence of the last sweep merged into the row
        int64_t timeNs = 0;      // end time of that sweep
        bool complete = true;    // false if any merged sweep was incomplete
        double startHz = 0;
        double stopHz = 0;
    };

    // Ring buffer of waterfall rows. Every row is one or more complete sweep
    // cycles. Levels are stored quantised to 8 bit (QUANT_MIN_DB..QUANT_MAX_DB,
    // ~0.8 dB steps, NaN as QUANT_NAN) so long histories stay small; colour
    // mapping happens at render time. Memory is bounded by width * capacity.
    class WaterfallHistory {
    public:
        static constexpr float QUANT_MIN_DB = -160.0f;
        static constexpr float QUANT_MAX_DB = 40.0f;
        static constexpr uint8_t QUANT_NAN = 255;
        static constexpr size_t DEFAULT_MAX_BYTES = 48u << 20;

        // Clears the history. Capacity is reduced if width * capacity > maxBytes.
        void configure(int width, int capacity, size_t maxBytes = DEFAULT_MAX_BYTES);
        void push(const float* db, int width, const WaterfallRowInfo& info);
        void clear();

        static uint8_t quantize(float db);
        static float dequantize(uint8_t q);

        // All accessors below require holding lock().
        std::unique_lock<std::mutex> lock() const { return std::unique_lock<std::mutex>(mtx); }
        int width() const { return w; }
        int capacity() const { return cap; }
        int rows() const { return count; }
        uint64_t totalPushed() const { return total; }
        uint64_t generation() const { return gen; } // changes on configure()/clear()
        // age 0 = newest row
        const uint8_t* rowByAge(int age, WaterfallRowInfo* info = nullptr) const;
        size_t memoryBytes() const { return data.capacity() + infos.capacity() * sizeof(WaterfallRowInfo); }

    private:
        mutable std::mutex mtx;
        std::vector<uint8_t> data;
        std::vector<WaterfallRowInfo> infos;
        int w = 0;
        int cap = 0;
        int head = 0;   // next write position
        int count = 0;
        uint64_t total = 0;
        uint64_t gen = 0;
    };
}
