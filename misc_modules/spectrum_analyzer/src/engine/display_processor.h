#pragma once
#include "sweep_types.h"
#include <deque>

namespace spectrum {
    struct DisplaySettings {
        int averageCount = 8;            // sweeps in the exponential average (1 = off)
        float peakDecayDbPerSec = 0.0f;  // max-hold decay, 0 = hold forever
        int persistence = 0;             // number of previous sweeps kept for the persistence view
        float noisePercentile = 0.20f;   // percentile of the spectrum used as noise floor estimate
    };

    // Derived traces computed from finished sweeps: current, average, max hold,
    // persistence history and a noise floor estimate. Memory is bounded by
    // bins * (3 + persistence).
    class DisplayProcessor {
    public:
        void reset(int bins);
        void setSettings(const DisplaySettings& s);
        const DisplaySettings& settings() const { return cfg; }

        // Interrupted frames should not be pushed.
        void push(const SweepFrame& frame);

        void resetAverage();
        void resetMaxHold();

        const std::vector<float>& current() const { return cur; }
        const std::vector<float>& average() const { return avgDb; }
        const std::vector<float>& maxHold() const { return maxH; }
        const std::deque<std::vector<float>>& persistenceTraces() const { return persist; }
        float noiseFloorDb() const { return noiseFloor; }
        float peakDb() const { return peak; }
        int framesPushed() const { return pushed; }

        static float percentile(const std::vector<float>& v, float p, std::vector<float>& scratch);

    private:
        DisplaySettings cfg;
        std::vector<float> cur;
        std::vector<float> avgLin;
        std::vector<float> avgDb;
        std::vector<float> maxH;
        std::deque<std::vector<float>> persist;
        std::vector<float> scratch;
        float noiseFloor = nanValue();
        float peak = nanValue();
        int pushed = 0;
        int avgFrames = 0;
        int64_t lastTimeNs = 0;
    };
}
