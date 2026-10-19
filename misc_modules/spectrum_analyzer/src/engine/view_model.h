#pragma once
#include "sweep_types.h"

namespace spectrum {
    enum class FreqUnit {
        KHZ = 0,
        MHZ = 1,
        GHZ = 2
    };
    double unitScale(FreqUnit u);
    const char* unitName(FreqUnit u);
    double toUnit(double hz, FreqUnit u);
    double fromUnit(double v, FreqUnit u);

    // Display window over the measured range. Zoom and pan change only what is
    // displayed, never the swept range.
    class FrequencyView {
    public:
        void setFullRange(double startHz, double stopHz, bool resetView);
        void reset();
        // factor > 1 zooms in around anchorHz
        void zoom(double factor, double anchorHz);
        void pan(double deltaHz);
        void setView(double startHz, double stopHz);

        double viewStart() const { return vStart; }
        double viewStop() const { return vStop; }
        double viewSpan() const { return vStop - vStart; }
        double fullStart() const { return fStart; }
        double fullStop() const { return fStop; }
        bool zoomed() const;

        double freqToX(double hz, double x0, double width) const;
        double xToFreq(double x, double x0, double width) const;
        // Display bins [b0, b1) of a grid with `bins` bins over the full range covering pixel column [x, x+1).
        void columnBins(int column, int columns, int bins, int& b0, int& b1) const;

        double minSpanHz = 100.0;

    private:
        void clampView();
        double fStart = 0, fStop = 1;
        double vStart = 0, vStop = 1;
    };

    // "Nice" grid step (1/2/5 * 10^n) so that span/step is about targetLines.
    double niceStep(double span, int targetLines);

    // Auto scale: noise floor - 10 dB .. peak + 5 dB, rounded to 5 dB, at least 30 dB range.
    void autoScaleRange(float noiseFloorDb, float peakDb, float& minDb, float& maxDb);

    // Index of the marker closest to freqHz within toleranceHz, -1 if none.
    int nearestMarker(const std::vector<Marker>& markers, double freqHz, double toleranceHz);

    // Level of a trace at a frequency (display grid lookup), NaN outside / not measured.
    float traceValueAt(const std::vector<float>& trace, double startHz, double stopHz, double freqHz);
}
