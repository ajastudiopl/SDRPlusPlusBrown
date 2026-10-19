#pragma once
#include "sweep_types.h"
#include <random>

namespace spectrum {
    // Synthetic RF scene used by the unit tests and by the clearly labelled
    // demo mode. It is NOT a measurement of anything.
    struct SyntheticEmitter {
        double freqHz = 0;
        float levelDbfs = -40.0f;   // total power of the emitter
        double bandwidthHz = 0;     // 0 = CW carrier, otherwise a comb of carriers spread over the bandwidth
        double hopSpanHz = 0;       // frequency hopping: span ...
        double hopPeriodSec = 0;    // ... and dwell per hop (0 = no hopping)
        double dutyCycle = 1.0;     // fraction of time the emitter is on (bursty signals)
        double burstPeriodSec = 1.0;
    };

    class SyntheticSource {
    public:
        explicit SyntheticSource(uint32_t seed = 12345) : rng(seed) {}

        void setNoiseDbfs(float dbfsPerSample) { noiseDbfs = dbfsPerSample; }
        void setDcSpikeDbfs(float dbfs) { dcDbfs = dbfs; }       // simulated LO leakage, NaN = none
        void addEmitter(const SyntheticEmitter& e) { emitters.push_back(e); }
        void clear() { emitters.clear(); }
        const std::vector<SyntheticEmitter>& getEmitters() const { return emitters; }

        // Generates `n` complex samples as seen by a receiver tuned to loHz
        // with sample rate fs at time `timeSec`.
        void generate(double loHz, double fs, int n, double timeSec, cf32* out);

        // A demo scene with broadcast FM, airband, PMR/amateur style signals.
        static SyntheticSource demoScene();

    private:
        std::mt19937 rng;
        float noiseDbfs = -95.0f;
        float dcDbfs = nanValue();
        std::vector<SyntheticEmitter> emitters;
    };
}
