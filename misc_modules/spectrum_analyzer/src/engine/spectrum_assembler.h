#pragma once
#include "segment_processor.h"

namespace spectrum {
    // Stitches the bins of all segments of one sweep into a single wideband
    // spectrum on a fixed display grid [startHz, stopHz) with `bins` bins.
    //
    // - Every FFT bin is mapped by its real frequency, so overlapping segments
    //   and different LO grids line up exactly.
    // - Where segments overlap, data with higher priority wins (clean data
    //   beats bins next to the LO); equal priorities are combined with the
    //   detector (peak = max, average = mean of linear power).
    // - Display bins never reached by any segment stay NaN (shown as gaps).
    // No smoothing is applied.
    class SpectrumAssembler {
    public:
        void configure(double startHz, double stopHz, int bins, Detector detector, int expectedSegments);
        void beginSweep(int64_t timeNs);
        void addChunk(const BinChunk& chunk);
        void markSegmentReceived(int segmentIndex);

        // Produces the frame and resets for the next sweep. A frame is complete
        // only if every planned segment delivered data and it was not interrupted.
        void finishSweep(SweepFrame& out, bool interrupted, int64_t timeNs);
        void abortSweep();

        // Partial data of the sweep in progress (NaN where not measured yet).
        void snapshot(std::vector<float>& outDb) const;

        bool sweepActive() const { return active; }
        int segmentsReceived() const { return received; }
        int expectedSegments() const { return (int)segReceived.size(); }
        int bins() const { return (int)acc.size(); }
        double startHz() const { return start; }
        double stopHz() const { return stop; }
        double binWidthHz() const { return binWidth; }
        uint64_t nextSequence() const { return sequence; }

    private:
        inline void combine(int j, float p, uint8_t pr);
        void reset();

        double start = 0, stop = 0, binWidth = 0;
        Detector detector = Detector::PEAK;
        std::vector<float> acc;
        std::vector<uint32_t> cnt;
        std::vector<uint8_t> prio;
        std::vector<uint8_t> segReceived;
        int received = 0;
        bool active = false;
        int64_t sweepStartNs = 0;
        uint64_t sequence = 1;
    };
}
