#include "wide_overview.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <gui/gui.h>
#include <gui/tuner.h>
#include <utils/opengl_include_code.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>
#include <algorithm>

namespace wideoverview {
    static const int TEX_W = 1024;
    static const int TEX_H = 256;

    static std::mutex mtx;
    static std::vector<uint8_t> latest;
    static double latestCenter = 0;
    static double latestSpan = 0;
    static uint64_t latestSeq = 0;
    static std::chrono::steady_clock::time_point lastPush;
    static bool everPushed = false;

    // UI thread only
    static uint64_t shownSeq = 0;
    static double shownCenter = 0;
    static double shownSpan = 0;
    static std::vector<float> line(TEX_W, 0.0f);
    static std::vector<uint8_t> rows(TEX_W * TEX_H, 0);
    static std::vector<uint32_t> pixels(TEX_W * TEX_H, 0xFF000000);
    static GLuint texture = 0;
    static bool textureDirty = false;
    static std::chrono::steady_clock::time_point lastTune;

    static std::atomic<int> demodMode{ 0 };
    static std::atomic<double> demodBandwidth{ 12500 };
    static std::atomic<double> demodOffset{ 0 };
    static std::atomic<bool> audioSession{ false };

    void setDemod(int mode, double bandwidth, double offset) {
        demodMode = mode;
        demodBandwidth = bandwidth;
        demodOffset = offset;
    }

    void getDemod(int* mode, double* bandwidth, double* offset) {
        *mode = demodMode;
        *bandwidth = demodBandwidth;
        *offset = demodOffset;
    }

    void setAudioSession(bool active) {
        audioSession = active;
    }

    bool isAudioSession() {
        return audioSession;
    }

    // Puts the server's coarse spectrum into the waterfall as if it came from the IQ front end.
    static void pushToWaterfall(const uint8_t* bins, int count) {
        auto& wf = gui::waterfall;
        // getFFTBuffer() leaves the waterfall locked until pushFFT(), so the size read here cannot change any more
        float* line = wf.getFFTBuffer();
        if (!line) { return; }
        int size = wf.getRawFFTSize();
        if (size <= 0) { size = 0; }
        for (int i = 0; i < size; i++) {
            float pos = (float)i * (count - 1) / (size > 1 ? size - 1 : 1);
            int a = (int)pos;
            int b = std::min(a + 1, count - 1);
            float f = pos - a;
            float v = bins[a] * (1 - f) + bins[b] * f;
            // the server sends dB relative to full scale; the noise floor then sits around -60, so move it where the front end puts it
            line[i] = v / 2.0f - 130.0f - 38.0f;
        }
        wf.pushFFT();
    }

    void push(double centerFrequency, double span, const uint8_t* bins, int count) {
        if (audioSession) {
            pushToWaterfall(bins, count);
            return;
        }
        std::lock_guard<std::mutex> lck(mtx);
        latest.assign(bins, bins + count);
        latestCenter = centerFrequency;
        latestSpan = span;
        latestSeq++;
        lastPush = std::chrono::steady_clock::now();
        everPushed = true;
    }

    void reset() {
        std::lock_guard<std::mutex> lck(mtx);
        everPushed = false;
        latest.clear();
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

    // Takes the newest spectrum (if any) into the scrolling picture.
    static void consume() {
        std::vector<uint8_t> bins;
        double center, span;
        {
            std::lock_guard<std::mutex> lck(mtx);
            if (latestSeq == shownSeq || latest.empty()) { return; }
            shownSeq = latestSeq;
            bins = latest;
            center = latestCenter;
            span = latestSpan;
        }
        if (center != shownCenter || span != shownSpan) {
            std::fill(rows.begin(), rows.end(), 0);
            std::fill(line.begin(), line.end(), 0.0f);
            shownCenter = center;
            shownSpan = span;
        }
        memmove(&rows[TEX_W], &rows[0], (size_t)TEX_W * (TEX_H - 1));
        int n = (int)bins.size();
        for (int x = 0; x < TEX_W; x++) {
            int a = x * n / TEX_W;
            int b = std::max(a + 1, (x + 1) * n / TEX_W);
            uint8_t m = 0;
            for (int k = a; k < b && k < n; k++) { m = std::max(m, bins[k]); }
            rows[x] = m;
            line[x] = line[x] * 0.5f + m * 0.5f;
        }
        float lo = gui::waterfall.getWaterfallMin();
        float hi = gui::waterfall.getWaterfallMax();
        if (hi - lo < 1) { hi = lo + 1; }
        for (int i = 0; i < TEX_W * TEX_H; i++) {
            float db = rows[i] / 2.0f - 130.0f;
            pixels[i] = rows[i] ? colorFor((db - lo) / (hi - lo)) : 0xFF000000;
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

    static double niceStep(double raw) {
        double mag = pow(10.0, floor(log10(raw)));
        double r = raw / mag;
        return (r < 1.5 ? 1 : r < 3.5 ? 2 : r < 7.5 ? 5 : 10) * mag;
    }

    void drawOverlay() {
        if (audioSession) { return; }
        {
            std::lock_guard<std::mutex> lck(mtx);
            if (!everPushed || std::chrono::steady_clock::now() - lastPush > std::chrono::seconds(3)) { return; }
        }
        auto& wf = gui::waterfall;
        float w = wf.fftAreaMax.x - wf.fftAreaMin.x;
        float totalH = wf.wfMax.y - wf.fftAreaMin.y;
        if (w < 100 || totalH < 100) { return; }

        consume();
        if (textureDirty || !texture) { uploadTexture(); }
        if (shownSpan <= 0) { return; }

        ImVec2 pos = wf.fftAreaMin;
        ImVec2 size(w, totalH * 0.34f);
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);
        ImGui::SetNextWindowBgAlpha(0.9f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("##wide_overview", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
        ImGui::PopStyleVar(2);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 a = ImGui::GetWindowPos();
        ImVec2 b(a.x + size.x, a.y + size.y);
        float lineH = size.y * 0.42f;
        float axisH = ImGui::GetFontSize() + 4;
        ImVec2 lineMin(a.x, a.y), lineMax(b.x, a.y + lineH);
        ImVec2 wfMin(a.x, lineMax.y + axisH), wfMax(b.x, b.y);
        double fLow = shownCenter - shownSpan / 2;

        // spectrum line
        float lo = wf.getFFTMin(), hi = wf.getFFTMax();
        if (hi - lo < 1) { hi = lo + 1; }
        dl->AddRectFilled(lineMin, lineMax, IM_COL32(0, 0, 0, 255));
        float prevX = lineMin.x, prevY = lineMax.y;
        for (int x = 0; x < TEX_W; x++) {
            float db = line[x] / 2.0f - 130.0f;
            float t = std::clamp((db - lo) / (hi - lo), 0.0f, 1.0f);
            float px = lineMin.x + (x + 0.5f) * (lineMax.x - lineMin.x) / TEX_W;
            float py = lineMax.y - t * (lineMax.y - lineMin.y);
            if (x > 0) { dl->AddLine(ImVec2(prevX, prevY), ImVec2(px, py), IM_COL32(120, 220, 255, 255), 1.0f); }
            prevX = px;
            prevY = py;
        }

        // axis
        dl->AddRectFilled(ImVec2(a.x, lineMax.y), ImVec2(b.x, wfMin.y), IM_COL32(20, 20, 20, 255));
        double step = niceStep(shownSpan / 6.0);
        for (double f = ceil(fLow / step) * step; f < fLow + shownSpan; f += step) {
            float x = a.x + (float)((f - fLow) / shownSpan) * size.x;
            char txt[32];
            snprintf(txt, sizeof(txt), step >= 1e6 ? "%.0f" : step >= 1e5 ? "%.1f" : "%.2f", f / 1e6);
            dl->AddLine(ImVec2(x, lineMax.y), ImVec2(x, lineMax.y + 4), IM_COL32(200, 200, 200, 255));
            dl->AddLine(ImVec2(x, lineMin.y), ImVec2(x, lineMax.y), IM_COL32(255, 255, 255, 30));
            dl->AddText(ImVec2(x + 3, lineMax.y + 2), IM_COL32(220, 220, 220, 255), txt);
        }

        // waterfall
        dl->AddImage((ImTextureID)(intptr_t)texture, wfMin, wfMax);

        // the tuned station and its channel
        double station = wf.getCenterFrequency();
        auto vit = wf.vfos.find(wf.selectedVFO);
        if (vit != wf.vfos.end() && vit->second) { station += vit->second->generalOffset; }
        float sx = a.x + (float)((station - fLow) / shownSpan) * size.x;
        float cw = std::max(2.0f, (float)(std::min<double>(demodBandwidth, 200000.0) / shownSpan) * size.x);
        if (sx >= a.x && sx <= b.x) {
            dl->AddRectFilled(ImVec2(sx - cw / 2, a.y), ImVec2(sx + cw / 2, b.y), IM_COL32(255, 160, 0, 60));
            dl->AddLine(ImVec2(sx, a.y), ImVec2(sx, b.y), IM_COL32(255, 160, 0, 255), 2.0f);
        }
        char info[64];
        snprintf(info, sizeof(info), "%.3f MHz", station / 1e6);
        dl->AddText(ImVec2(a.x + 6, a.y + 2), IM_COL32(255, 200, 80, 255), info);

        // tap or drag = tune
        ImGui::SetCursorScreenPos(a);
        ImGui::InvisibleButton("##wide_overview_hit", size);
        if (ImGui::IsItemActive()) {
            auto now = std::chrono::steady_clock::now();
            if (ImGui::IsItemActivated() || now - lastTune > std::chrono::milliseconds(60)) {
                float x = std::clamp(ImGui::GetIO().MousePos.x - a.x, 0.0f, size.x);
                double f = fLow + (double)x / size.x * shownSpan;
                f = std::round(f / 1000.0) * 1000.0;
                if (!wf.selectedVFO.empty()) { tuner::tune(tuner::TUNER_MODE_CENTER, wf.selectedVFO, f); }
                lastTune = now;
            }
        }
        ImGui::End();
    }
}
