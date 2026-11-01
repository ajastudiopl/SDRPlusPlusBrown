#define IMGUI_DEFINE_MATH_OPERATORS
#include "wide_overview.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <gui/gui.h>
#include <gui/tuner.h>
#include <utils/opengl_include_code.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

namespace wideoverview {
    static const int TEX_W = 512;
    static const int TEX_H = 128;

    static std::mutex mtx;
    static std::vector<uint8_t> latest;
    static double latestCenter = 0;
    static double latestSpan = 0;
    static uint64_t latestSeq = 0;
    static std::chrono::steady_clock::time_point lastPush;
    static bool everPushed = false;

    // UI thread only
    static uint64_t shownSeq = 0;
    static std::vector<float> line;                 // smoothed dB per bin
    static std::vector<uint8_t> rows(TEX_W * TEX_H, 0);
    static std::vector<uint32_t> pixels(TEX_W * TEX_H, 0xFF000000);
    static GLuint texture = 0;
    static bool textureDirty = false;
    static int zoomLevel = 0;                       // the view is the span from the server (about three times the passband) / 2^zoomLevel wide
    static double dataSpan = 48000;                 // span of the data on the screen now
    static int viewMode = 0;                        // 0 spectrum and waterfall, 1 spectrum, 2 waterfall
    static float rangeLow = -100, rangeHigh = -40;
    static double dragCenter = 0;
    static std::chrono::steady_clock::time_point lastTune;

    void pushNarrow(double centerFrequency, double span, const uint8_t* bins, int count) {
        std::lock_guard<std::mutex> lck(mtx);
        latest.assign(bins, bins + count);
        latestCenter = centerFrequency;
        latestSpan = span;
        latestSeq++;
        lastPush = std::chrono::steady_clock::now();
        everPushed = true;
    }

    static uint32_t colorFor(float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        static const float stops[5][3] = { { 0, 0, 0.15f }, { 0, 0.2f, 0.8f }, { 0, 0.9f, 0.9f }, { 1, 0.9f, 0 }, { 1, 0.1f, 0 } };
        float x = t * 4.0f;
        int i = std::min((int)x, 3);
        float f = x - i;
        auto c = [&](int k) { return (uint32_t)(255.0f * (stops[i][k] + (stops[i + 1][k] - stops[i][k]) * f)); };
        return 0xFF000000 | (c(2) << 16) | (c(1) << 8) | c(0);
    }

    static double niceStep(double raw) {
        double mag = pow(10.0, floor(log10(raw)));
        double r = raw / mag;
        return (r < 1.5 ? 1 : r < 3.5 ? 2 : r < 7.5 ? 5 : 10) * mag;
    }

    static double viewSpan() { return dataSpan / (double)(1 << zoomLevel); }

    // dB at an offset (Hz) from the centre of the data
    static float dbAt(double offset, double span) {
        int n = (int)line.size();
        double pos = (offset / span + 0.5) * n;
        int a = std::clamp((int)floor(pos), 0, n - 1);
        return line[a];
    }

    // One new spectrum into the smoothed line and the scrolling picture.
    static void consume() {
        std::vector<uint8_t> bins;
        double span;
        {
            std::lock_guard<std::mutex> lck(mtx);
            if (latestSeq == shownSeq || latest.empty()) { return; }
            shownSeq = latestSeq;
            bins = latest;
            span = latestSpan;
        }
        int n = (int)bins.size();
        if ((int)line.size() != n) { line.assign(n, -130.0f); }
        if (span != dataSpan) {
            // another modulation or bandwidth: the old rows are of another scale
            dataSpan = span;
            std::fill(rows.begin(), rows.end(), 0);
        }
        for (int i = 0; i < n; i++) {
            float db = bins[i] / 2.0f - 130.0f;
            line[i] = line[i] * 0.4f + db * 0.6f;
        }
        // scale to what is on the screen: floor from the median, top from the peaks
        std::vector<float> sorted(line);
        std::sort(sorted.begin(), sorted.end());
        float floorDb = sorted[sorted.size() / 2];
        float peakDb = sorted[sorted.size() * 995 / 1000];
        float lo = floorDb - 6, hi = std::max(peakDb + 4, floorDb + 25);
        rangeLow += (lo - rangeLow) * 0.15f;
        rangeHigh += (hi - rangeHigh) * 0.15f;

        // one row of the waterfall over the current view
        memmove(&rows[TEX_W], &rows[0], (size_t)TEX_W * (TEX_H - 1));
        double vs = viewSpan();
        for (int x = 0; x < TEX_W; x++) {
            double off = ((x + 0.5) / TEX_W - 0.5) * vs;
            double pos = (off / span + 0.5) * n;
            int a = std::clamp((int)floor(pos), 0, n - 1);
            int b = std::clamp((int)floor(pos + n * vs / span / TEX_W), a, n - 1);
            float m = -200;
            for (int k = a; k <= b; k++) { m = std::max(m, line[k]); }
            rows[x] = (uint8_t)std::clamp((m + 130.0f) * 2.0f, 1.0f, 255.0f);
        }
        float range = std::max(rangeHigh - rangeLow, 1.0f);
        for (int i = 0; i < TEX_W * TEX_H; i++) {
            float db = rows[i] / 2.0f - 130.0f;
            pixels[i] = rows[i] ? colorFor((db - rangeLow) / range) : 0xFF000000;
        }
        textureDirty = true;
    }

    static void uploadTexture() {
        if (!texture) {
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_W, TEX_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        textureDirty = false;
    }

    bool drawNarrowZoom() {
        double center, span;
        {
            std::lock_guard<std::mutex> lck(mtx);
            if (!everPushed || std::chrono::steady_clock::now() - lastPush > std::chrono::seconds(2)) { return false; }
            center = latestCenter;
            span = latestSpan;
        }
        consume();
        if (line.empty() || span <= 0) { return false; }
        if (textureDirty || !texture) { uploadTexture(); }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        const float fs = ImGui::GetFontSize();
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const double vs = viewSpan();

        // buttons: zoom out / in and what is shown
        float bh = fs * 1.9f;
        float bw = fs * 2.2f;
        ImGui::PushID("narrowzoom");
        if (ImGui::Button("<", ImVec2(bw, bh))) { if (zoomLevel > 0) { zoomLevel--; std::fill(rows.begin(), rows.end(), 0); } }
        ImGui::SameLine();
        char label[32];
        snprintf(label, sizeof label, vs >= 100000 ? "%.0f kHz" : "%.1f kHz", vs / 1000.0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        if (ImGui::Button(">", ImVec2(bw, bh))) { if (zoomLevel < 3) { zoomLevel++; std::fill(rows.begin(), rows.end(), 0); } }
        ImGui::SameLine(0, gap);
        const char* names[3] = { "S+W", "SP", "WF" };
        for (int m = 0; m < 3; m++) {
            if (m > 0) { ImGui::SameLine(); }
            bool on = viewMode == m;
            if (on) { ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)); }
            if (ImGui::Button(names[m], ImVec2(fs * 2.6f, bh))) { viewMode = m; }
            if (on) { ImGui::PopStyleColor(); }
        }
        ImGui::PopID();

        // drawing area
        ImVec2 areaMin = ImGui::GetCursorScreenPos();
        areaMin.x = origin.x;
        ImVec2 areaMax(origin.x + avail.x, origin.y + avail.y);
        float axisH = fs * 1.3f;
        float h = areaMax.y - areaMin.y - axisH;
        if (h < 20) { return true; }
        float spH = viewMode == 0 ? h * 0.45f : viewMode == 1 ? h : 0;
        float wfH = viewMode == 0 ? h - spH : viewMode == 2 ? h : 0;
        float x0 = areaMin.x, x1 = areaMax.x, w = x1 - x0;
        ImVec2 spMin(x0, areaMin.y), spMax(x1, areaMin.y + spH);
        ImVec2 axMin(x0, areaMin.y + spH), axMax(x1, areaMin.y + spH + axisH);
        ImVec2 wfMin(x0, axMax.y), wfMax(x1, axMax.y + wfH);

        // spectrum
        if (spH > 0) {
            dl->AddRectFilled(spMin, spMax, IM_COL32(0, 0, 0, 255));
            float range = std::max(rangeHigh - rangeLow, 1.0f);
            std::vector<ImVec2> pts;
            int columns = (int)w;
            for (int px = 0; px <= columns; px += 2) {
                double off = ((double)px / columns - 0.5) * vs;
                float db = dbAt(off, span);
                float t = std::clamp((db - rangeLow) / range, 0.0f, 1.0f);
                pts.push_back(ImVec2(x0 + px, spMax.y - t * (spMax.y - spMin.y - 2)));
            }
            for (size_t i = 0; i + 1 < pts.size(); i++) {
                dl->AddRectFilled(ImVec2(pts[i].x, pts[i].y), ImVec2(pts[i + 1].x, spMax.y), IM_COL32(110, 90, 190, 150));
                dl->AddLine(pts[i], pts[i + 1], IM_COL32(230, 230, 255, 255), 1.5f);
            }
        }
        // waterfall
        if (wfH > 0) {
            dl->AddImage((ImTextureID)(intptr_t)texture, wfMin, wfMax);
        }

        // frequency axis, in Hz from the station like in SDRuno
        dl->AddRectFilled(axMin, axMax, IM_COL32(20, 20, 20, 255));
        double step = niceStep(vs * std::max(fs * 5.0f, 60.0f) / w);
        for (double off = ceil(-vs / 2 / step) * step; off <= vs / 2; off += step) {
            float x = x0 + (float)((off / vs + 0.5) * w);
            char txt[24];
            if (step >= 1000) { snprintf(txt, sizeof txt, "%.0fk", off / 1000.0); } else { snprintf(txt, sizeof txt, "%.0f", off); }
            dl->AddLine(ImVec2(x, axMin.y), ImVec2(x, axMin.y + 4), IM_COL32(200, 200, 200, 255));
            ImVec2 ts = ImGui::CalcTextSize(txt);
            dl->AddText(ImVec2(std::clamp(x - ts.x / 2, x0, x1 - ts.x), axMin.y + 3), IM_COL32(220, 220, 220, 255), txt);
            if (spH > 0) { dl->AddLine(ImVec2(x, spMin.y), ImVec2(x, spMax.y), IM_COL32(255, 255, 255, 25)); }
        }

        // the passband of the demodulator and the tuned frequency
        int mode;
        double bandwidth, offsetUnused;
        getDemod(&mode, &bandwidth, &offsetUnused);
        double lowEdge = -bandwidth / 2, highEdge = bandwidth / 2;
        if (mode == 4) { lowEdge = 0; highEdge = bandwidth; }          // USB
        else if (mode == 6) { lowEdge = -bandwidth; highEdge = 0; }    // LSB
        else if (mode == 1) { lowEdge = -std::min(bandwidth, 180000.0) / 2; highEdge = -lowEdge; }
        auto xOf = [&](double off) { return x0 + (float)((off / vs + 0.5) * w); };
        float xl = std::clamp(xOf(lowEdge), x0, x1), xh = std::clamp(xOf(highEdge), x0, x1);
        float top = areaMin.y, bottom = wfMax.y > axMax.y ? wfMax.y : axMax.y;
        dl->AddRectFilled(ImVec2(xl, top), ImVec2(xh, bottom), IM_COL32(255, 255, 255, 18));
        dl->AddLine(ImVec2(xl, top), ImVec2(xl, bottom), IM_COL32(255, 60, 60, 230), 2.0f);
        dl->AddLine(ImVec2(xh, top), ImVec2(xh, bottom), IM_COL32(255, 60, 60, 230), 2.0f);
        float xc = xOf(0);
        dl->AddLine(ImVec2(xc, top), ImVec2(xc, bottom), IM_COL32(255, 220, 60, 255), 2.0f);

        // tapping or dragging sets the frequency; the picture then moves to be centred on it again, so a drag is
        // measured from where it started
        ImGui::SetCursorScreenPos(ImVec2(x0, areaMin.y));
        ImGui::InvisibleButton("##narrowhit", ImVec2(w, std::max(1.0f, bottom - top)));
        if (ImGui::IsItemClicked(0)) { dragCenter = center; }
        if (ImGui::IsItemActive() && !gui::waterfall.selectedVFO.empty()) {
            auto now = std::chrono::steady_clock::now();
            if (ImGui::IsItemClicked(0) || now - lastTune > std::chrono::milliseconds(40)) {
                float mx = std::clamp(ImGui::GetIO().MousePos.x - x0, 0.0f, w);
                double f = dragCenter + ((double)mx / w - 0.5) * vs;
                f = std::round(f / 10.0) * 10.0;
                tuner::tune(tuner::TUNER_MODE_NORMAL, gui::waterfall.selectedVFO, f);
                lastTune = now;
            }
        }
        return true;
    }
}
