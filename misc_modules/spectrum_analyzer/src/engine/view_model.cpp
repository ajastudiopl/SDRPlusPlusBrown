#include "view_model.h"
#include <algorithm>
#include <cmath>

namespace spectrum {
    double unitScale(FreqUnit u) {
        switch (u) {
        case FreqUnit::KHZ: return 1e3;
        case FreqUnit::MHZ: return 1e6;
        case FreqUnit::GHZ: return 1e9;
        }
        return 1e6;
    }

    const char* unitName(FreqUnit u) {
        switch (u) {
        case FreqUnit::KHZ: return "kHz";
        case FreqUnit::MHZ: return "MHz";
        case FreqUnit::GHZ: return "GHz";
        }
        return "MHz";
    }

    double toUnit(double hz, FreqUnit u) { return hz / unitScale(u); }
    double fromUnit(double v, FreqUnit u) { return v * unitScale(u); }

    void FrequencyView::setFullRange(double startHz, double stopHz, bool resetView) {
        if (!(stopHz > startHz)) { return; }
        bool changed = (startHz != fStart) || (stopHz != fStop);
        fStart = startHz;
        fStop = stopHz;
        if (resetView || changed) { reset(); }
        else { clampView(); }
    }

    void FrequencyView::setNavigationLimits(double minHz, double maxHz) {
        if (maxHz > minHz) {
            navMin = minHz;
            navMax = maxHz;
        }
        else {
            navMin = navMax = 0;
        }
        clampView();
    }

    double FrequencyView::navStart() const { return (navMax > navMin) ? std::min(navMin, fStart) : fStart; }
    double FrequencyView::navStop() const { return (navMax > navMin) ? std::max(navMax, fStop) : fStop; }

    void FrequencyView::reset() {
        vStart = fStart;
        vStop = fStop;
    }

    bool FrequencyView::zoomed() const {
        return vStart > fStart || vStop < fStop;
    }

    void FrequencyView::clampView() {
        // The view may leave the measured range (to pick a new one) but not the navigation limits.
        double a = navStart(), b = navStop();
        double fullSpan = b - a;
        double span = std::clamp(vStop - vStart, std::min(minSpanHz, fullSpan), fullSpan);
        double s = std::clamp(vStart, a, b - span);
        vStart = s;
        vStop = s + span;
    }

    void FrequencyView::zoom(double factor, double anchorHz) {
        if (!(factor > 0)) { return; }
        double span = vStop - vStart;
        double newSpan = span / factor;
        double rel = (span > 0) ? (anchorHz - vStart) / span : 0.5;
        rel = std::clamp(rel, 0.0, 1.0);
        double fullSpan = navStop() - navStart();
        newSpan = std::clamp(newSpan, std::min(minSpanHz, fullSpan), fullSpan);
        vStart = anchorHz - rel * newSpan;
        vStop = vStart + newSpan;
        clampView();
    }

    void FrequencyView::pan(double deltaHz) {
        vStart += deltaHz;
        vStop += deltaHz;
        clampView();
    }

    void FrequencyView::setView(double startHz, double stopHz) {
        if (!(stopHz > startHz)) { return; }
        vStart = startHz;
        vStop = stopHz;
        clampView();
    }

    double FrequencyView::freqToX(double hz, double x0, double width) const {
        return x0 + (hz - vStart) / (vStop - vStart) * width;
    }

    double FrequencyView::xToFreq(double x, double x0, double width) const {
        if (width <= 0) { return vStart; }
        return vStart + (x - x0) / width * (vStop - vStart);
    }

    void FrequencyView::columnBins(int column, int columns, int bins, int& b0, int& b1) const {
        double fullSpan = fStop - fStart;
        double binW = fullSpan / (double)bins;
        double fa = vStart + (vStop - vStart) * (double)column / (double)columns;
        double fb = vStart + (vStop - vStart) * (double)(column + 1) / (double)columns;
        if (fb <= fStart || fa >= fStop) {
            // Column outside the measured range: no data.
            b0 = b1 = 0;
            return;
        }
        b0 = (int)std::floor((fa - fStart) / binW);
        b1 = (int)std::ceil((fb - fStart) / binW);
        b0 = std::clamp(b0, 0, bins);
        b1 = std::clamp(b1, 0, bins);
        if (b1 <= b0 && b0 < bins) { b1 = b0 + 1; }
    }

    double niceStep(double span, int targetLines) {
        if (!(span > 0) || targetLines <= 0) { return 1.0; }
        double raw = span / (double)targetLines;
        double mag = std::pow(10.0, std::floor(std::log10(raw)));
        double norm = raw / mag;
        double step;
        if (norm < 1.5) { step = 1; }
        else if (norm < 3.5) { step = 2; }
        else if (norm < 7.5) { step = 5; }
        else { step = 10; }
        return step * mag;
    }

    void autoScaleRange(float noiseFloorDb, float peakDb, float& minDb, float& maxDb) {
        if (!std::isfinite(noiseFloorDb) || !std::isfinite(peakDb)) { return; }
        float lo = std::floor((noiseFloorDb - 10.0f) / 5.0f) * 5.0f;
        float hi = std::ceil((peakDb + 5.0f) / 5.0f) * 5.0f;
        if (hi - lo < 30.0f) { hi = lo + 30.0f; }
        minDb = lo;
        maxDb = hi;
    }

    int nearestMarker(const std::vector<Marker>& markers, double freqHz, double toleranceHz) {
        int best = -1;
        double bestD = toleranceHz;
        for (int i = 0; i < (int)markers.size(); i++) {
            double d = std::fabs(markers[i].freqHz - freqHz);
            if (d <= bestD) {
                bestD = d;
                best = i;
            }
        }
        return best;
    }

    float traceValueAt(const std::vector<float>& trace, double startHz, double stopHz, double freqHz) {
        if (trace.empty() || !(stopHz > startHz) || freqHz < startHz || freqHz >= stopHz) { return nanValue(); }
        int i = (int)((freqHz - startHz) / (stopHz - startHz) * (double)trace.size());
        i = std::clamp(i, 0, (int)trace.size() - 1);
        return trace[i];
    }

    namespace {
        // 10x faster for every 60 px (unscaled) from the press point; at most 1 GHz/px.
        const double DRAG_DECADE_PX = 60.0;
        const double DRAG_MAX_PX = 360.0;
    }

    double dragSpeedHzPerPx(double distancePx) {
        double d = std::min(std::fabs(distancePx), DRAG_MAX_PX);
        return 1e3 * std::pow(10.0, d / DRAG_DECADE_PX);
    }

    double dragOffsetHz(double distancePx) {
        double d = std::min(std::fabs(distancePx), DRAG_MAX_PX);
        double k = DRAG_DECADE_PX / std::log(10.0);
        double off = 1e3 * k * (std::pow(10.0, d / DRAG_DECADE_PX) - 1.0);
        return distancePx < 0 ? -off : off;
    }

    double dragSnapHz(double hz, double speedHzPerPx) {
        if (!(speedHzPerPx > 0)) { return hz; }
        double step = std::pow(10.0, std::floor(std::log10(speedHzPerPx)));
        return std::round(hz / step) * step;
    }
}
