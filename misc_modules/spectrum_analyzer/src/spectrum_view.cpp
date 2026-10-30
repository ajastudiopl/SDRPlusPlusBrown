#include "spectrum_view.h"
#include <backend.h>
#include <gui/style.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace spectrum {
    namespace {
        const ImU32 COL_BG = IM_COL32(12, 14, 18, 255);
        const ImU32 COL_GRID = IM_COL32(70, 74, 84, 150);
        const ImU32 COL_GRID_FAINT = IM_COL32(70, 74, 84, 70);
        const ImU32 COL_TEXT = IM_COL32(190, 192, 200, 255);
        const ImU32 COL_CURRENT = IM_COL32(255, 214, 64, 255);
        const ImU32 COL_AVG = IM_COL32(64, 200, 255, 255);
        const ImU32 COL_MAX = IM_COL32(255, 86, 72, 210);
        const ImU32 COL_PARTIAL = IM_COL32(175, 175, 175, 120);
        const ImU32 COL_NOISE = IM_COL32(80, 220, 120, 190);
        const ImU32 COL_MARKER = IM_COL32(240, 120, 255, 255);
        const ImU32 COL_PEAK = IM_COL32(255, 255, 255, 220);
        const ImU32 COL_CURSOR = IM_COL32(255, 255, 255, 110);
        const ImU32 COL_DEMO = IM_COL32(255, 150, 40, 255);
        const ImU32 COL_WARN = IM_COL32(255, 90, 80, 255);
        const ImU32 COL_NODATA = IM_COL32(28, 24, 30, 255);
        const float WF_MIN_LINE_PX = 2.0f;
        const int MAX_PEAK_LABELS = 20;

        std::string axisLabel(double hz, double step) {
            char buf[48];
            double unit = (std::fabs(hz) >= 1e9 && step >= 1e6) ? 1e9 : ((std::fabs(hz) >= 1e6 || step >= 1e5) ? 1e6 : 1e3);
            int dec = std::max(0, (int)std::ceil(-std::log10(step / unit) - 1e-9));
            dec = std::min(dec, 6);
            snprintf(buf, sizeof(buf), "%.*f", dec, hz / unit);
            return buf;
        }

        const char* axisUnit(double hz, double step) {
            if (std::fabs(hz) >= 1e9 && step >= 1e6) { return "GHz"; }
            if (std::fabs(hz) >= 1e6 || step >= 1e5) { return "MHz"; }
            return "kHz";
        }

        void dashedVLine(ImDrawList* dl, float x, float y0, float y1, ImU32 col, float dash) {
            for (float y = y0; y < y1; y += dash * 2) { dl->AddLine(ImVec2(x, y), ImVec2(x, std::min(y + dash, y1)), col); }
        }

        void dashedHLine(ImDrawList* dl, float y, float x0, float x1, ImU32 col, float dash) {
            for (float x = x0; x < x1; x += dash * 2) { dl->AddLine(ImVec2(x, y), ImVec2(std::min(x + dash, x1), y), col); }
        }

        void labelBox(ImDrawList* dl, ImVec2 pos, const char* text, ImU32 col) {
            ImVec2 ts = ImGui::CalcTextSize(text);
            dl->AddRectFilled(ImVec2(pos.x - 2, pos.y - 1), ImVec2(pos.x + ts.x + 2, pos.y + ts.y + 1), IM_COL32(0, 0, 0, 170), 2.0f);
            dl->AddText(pos, col, text);
        }
    }

    SpectrumView::~SpectrumView() {
        // The texture is not deleted here: module instances can be destroyed
        // after the GL context at application exit.
    }

    void SpectrumView::releaseGl() {
        if (tex) {
            glDeleteTextures(1, &tex);
            tex = 0;
        }
        wfDirty = true;
    }

    void SpectrumView::setPalette(const float* rgb, int count) {
        palette.assign(rgb, rgb + count * 3);
        lutValid = false;
        wfDirty = true;
    }

    void SpectrumView::rebuildLut(const WaterfallConfig& cfg) {
        static const float fallback[] = { 0, 0, 40, 0, 60, 200, 0, 200, 220, 240, 230, 40, 250, 60, 20 };
        const float* pal = palette.empty() ? fallback : palette.data();
        int count = palette.empty() ? 5 : (int)palette.size() / 3;
        float range = std::max(cfg.maxDb - cfg.minDb, 1.0f);
        for (int q = 0; q < 255; q++) {
            float db = WaterfallHistory::dequantize((uint8_t)q);
            float t = (db - cfg.minDb) / range;
            t = (t - 0.5f) * cfg.contrast + 0.5f + cfg.brightness;
            t = std::clamp(t, 0.0f, 1.0f);
            float pos = t * (float)(count - 1);
            int i0 = std::clamp((int)std::floor(pos), 0, count - 1);
            int i1 = std::min(i0 + 1, count - 1);
            float f = pos - (float)i0;
            float r = pal[i0 * 3] * (1 - f) + pal[i1 * 3] * f;
            float g = pal[i0 * 3 + 1] * (1 - f) + pal[i1 * 3 + 1] * f;
            float b = pal[i0 * 3 + 2] * (1 - f) + pal[i1 * 3 + 2] * f;
            lut[q] = IM_COL32((int)r, (int)g, (int)b, 255);
        }
        lut[WaterfallHistory::QUANT_NAN] = COL_NODATA;
        lutCfg = cfg;
        lutValid = true;
        wfDirty = true;
    }

    const uint32_t* SpectrumView::colorTable(const WaterfallConfig& cfg) {
        bool same = lutValid && lutCfg.minDb == cfg.minDb && lutCfg.maxDb == cfg.maxDb && lutCfg.brightness == cfg.brightness &&
                    lutCfg.contrast == cfg.contrast && lutCfg.palette == cfg.palette;
        if (!same) { rebuildLut(cfg); }
        return lut;
    }

    void SpectrumView::drawRangeOverlays(ImDrawList* dl, ImVec2 p0, ImVec2 p1, FrequencyView& view) {
        float w = p1.x - p0.x;
        if (selectMode && selPhase >= 1) {
            double a = std::min(selA, selB), b = std::max(selA, selB);
            float xa = (float)view.freqToX(a, p0.x, w), xb = (float)view.freqToX(b, p0.x, w);
            dl->AddRectFilled(ImVec2(std::max(xa, p0.x), p0.y), ImVec2(std::min(std::max(xb, xa + 2.0f), p1.x), p1.y), IM_COL32(60, 130, 255, 60));
            dl->AddLine(ImVec2(xa, p0.y), ImVec2(xa, p1.y), IM_COL32(90, 160, 255, 255), 2.0f);
            if (selPhase == 2 || b > a) { dl->AddLine(ImVec2(xb, p0.y), ImVec2(xb, p1.y), IM_COL32(90, 160, 255, 255), 2.0f); }
        }
        if (std::isfinite(previewStartHz) && std::isfinite(previewStopHz)) {
            float xa = (float)view.freqToX(previewStartHz, p0.x, w), xb = (float)view.freqToX(previewStopHz, p0.x, w);
            dl->AddRectFilled(ImVec2(std::max(xa, p0.x), p0.y), ImVec2(std::min(xb, p1.x), p1.y), IM_COL32(120, 230, 255, 30));
            dashedVLine(dl, xa, p0.y, p1.y, IM_COL32(120, 230, 255, 230), 5.0f * style::uiScale);
            dashedVLine(dl, xb, p0.y, p1.y, IM_COL32(120, 230, 255, 230), 5.0f * style::uiScale);
        }
    }

    void SpectrumView::drawFreqGrid(ImDrawList* dl, ImVec2 p0, ImVec2 size, FrequencyView& view, bool labels) {
        int lines = std::max(2, (int)(size.x / (110.0f * style::uiScale)));
        double step = niceStep(view.viewSpan(), lines);
        if (!(step > 0)) { return; }
        double first = std::ceil(view.viewStart() / step) * step;
        float lineH = ImGui::GetTextLineHeight();
        int guard = 0;
        for (double f = first; f <= view.viewStop() && guard < 200; f += step, guard++) {
            float x = (float)view.freqToX(f, p0.x, size.x);
            dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p0.y + size.y), labels ? COL_GRID : COL_GRID_FAINT);
            if (labels) {
                std::string l = axisLabel(f, step);
                labelBox(dl, ImVec2(x + 3, p0.y + size.y - lineH - 1), l.c_str(), COL_TEXT);
            }
        }
        if (labels) {
            const char* u = axisUnit(view.viewStart(), step);
            ImVec2 ts = ImGui::CalcTextSize(u);
            labelBox(dl, ImVec2(p0.x + size.x - ts.x - 4, p0.y + size.y - lineH - 1), u, COL_TEXT);
        }
    }

    void SpectrumView::handlePinch(ImVec2 p0, ImVec2 size, FrequencyView& view) {
        ImGuiIO& io = ImGui::GetIO();
        // Two fingers: pinch zooms the range around the point between them
        if (io.FingerDown[0] && io.FingerDown[1]) {
            stopPan();
            ImVec2 a = io.FingerPos[0], b = io.FingerPos[1];
            ImVec2 mid((a.x + b.x) / 2.0f, (a.y + b.y) / 2.0f);
            if (mid.x >= p0.x && mid.x < p0.x + size.x && mid.y >= p0.y && mid.y < p0.y + size.y) {
                float dist = std::hypot(a.x - b.x, a.y - b.y);
                if (pinching && pinchLastDist > 1.0f && dist > 1.0f) {
                    double factor = std::clamp((double)(dist / pinchLastDist), 0.5, 2.0);
                    view.zoom(factor, view.xToFreq(mid.x, p0.x, size.x));
                }
                pinching = true;
                pinchLastDist = dist;
                pressDragged = true;   // not a tap
            }
        }
        else { pinching = false; }
    }

    bool SpectrumView::labelAt(ImVec2 pos, bool hovered, double& hz) const {
        // The tooltip is drawn over everything else, also outside the plot it belongs to.
        if (tooltipShown && pos.x >= tooltipLabel.min.x && pos.x < tooltipLabel.max.x && pos.y >= tooltipLabel.min.y && pos.y < tooltipLabel.max.y) {
            hz = tooltipLabel.hz;
            return true;
        }
        if (!hovered) { return false; }
        // Labels are small, the area that reacts to a finger is a bit larger.
        const float pad = 6.0f * style::uiScale;
        float best = std::numeric_limits<float>::max();
        bool found = false;
        for (auto& l : tuneLabels) {
            if (pos.x < l.min.x - pad || pos.x >= l.max.x + pad || pos.y < l.min.y - pad || pos.y >= l.max.y + pad) { continue; }
            float dx = pos.x - (l.min.x + l.max.x) / 2.0f;
            float dy = pos.y - (l.min.y + l.max.y) / 2.0f;
            float dist = dx * dx + dy * dy;
            if (dist < best) {
                best = dist;
                hz = l.hz;
                found = true;
            }
        }
        return found;
    }

    void SpectrumView::captureTooltip(double hz) {
        if (ImGui::GetFrameCount() <= tooltipHoldFrame) { return; }
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 sz = ImGui::GetWindowSize();
        tooltipLabel = { pos, ImVec2(pos.x + sz.x, pos.y + sz.y), hz };
        tooltipShown = true;
    }

    bool SpectrumView::handleInteraction(const char* id, ImVec2 p0, ImVec2 size, FrequencyView& view, ViewEvents& ev,
                                         const std::vector<Marker>* markers) {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetCursorScreenPos(p0);
        ImGui::InvisibleButton(id, size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        bool hovered = ImGui::IsItemHovered();
        bool active = ImGui::IsItemActive();
        ImGui::SetItemUsingMouseWheel();
        double mouseHz = view.xToFreq(io.MousePos.x, p0.x, size.x);

        // Runs once per frame, in the spectrum pass, before the waterfall looks at the same click.
        if (markers) {
            labelTuned = false;
            if (io.FingerDown[0]) { tooltipHoldFrame = ImGui::GetFrameCount() + 2; }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                labelPressed = !selectMode && labelAt(io.MousePos, hovered, labelPressHz);
            }
            if (labelPressed && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                labelPressed = false;
                float slop = 3.0f * style::uiScale;
                if (io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < slop * slop) {
                    ev.tune = true;
                    ev.tuneHz = labelPressHz;
                    labelTuned = true;
                }
            }
        }

        if (hovered && io.MouseWheel != 0.0f) {
            stopPan();
            view.zoom(std::pow(1.25, (double)io.MouseWheel), mouseHz);
        }
        handlePinch(p0, size, view);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            stopPan();
            pressDragged = false;
            pressInside = true;
            if (selectMode) {
                // A press starts a new selection unless it is the second tap of one.
                selPressStarted = (selPhase != 1);
                if (selPressStarted) {
                    selA = selB = mouseHz;
                    selPhase = 1;
                }
            }
        }
        if (active && !pinching && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f * style::uiScale)) {
            dragging = true;
            pressDragged = true;
            if (selectMode) {
                // Holding the finger near an edge keeps scrolling the view, so the
                // selection can extend beyond what is visible.
                float edge = std::min(60.0f * style::uiScale, size.x * 0.12f);
                float x = io.MousePos.x;
                double depth = 0;
                if (x < p0.x + edge) { depth = -std::min(1.0f, (p0.x + edge - x) / edge); }
                else if (x > p0.x + size.x - edge) { depth = std::min(1.0f, (x - (p0.x + size.x - edge)) / edge); }
                if (depth != 0) { view.pan(depth * view.viewSpan() * 0.8 * (double)io.DeltaTime); }
                selB = view.xToFreq(std::clamp(x, p0.x, p0.x + size.x), p0.x, size.x);
            }
            else {
                // Dragging over a plot picks a frequency precisely (cursor with a readout above the
                // finger); the view is moved with the bar between the plots.
                hasTap = true;
                tapHz = mouseHz;
            }
        }
        if ((hovered || active) && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 3.0f * style::uiScale)) {
            stopPan();
            view.pan(-(double)io.MouseDelta.x / (double)size.x * view.viewSpan());
        }
        if (active && dragging && !pinching && !selectMode) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            float x = std::clamp(io.MousePos.x, p0.x, p0.x + size.x);
            dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p0.y + size.y), IM_COL32(120, 230, 255, 220), style::uiScale);
            std::string l = formatFrequency(mouseHz, 5);
            ImVec2 ts = ImGui::CalcTextSize(l.c_str());
            float lx = std::clamp(x - ts.x / 2.0f, p0.x + 2.0f, p0.x + size.x - ts.x - 2.0f);
            float ly = std::max(io.MousePos.y - 64.0f * style::uiScale, p0.y + 2.0f);
            labelBox(dl, ImVec2(lx, ly), l.c_str(), IM_COL32(255, 255, 255, 255));
        }
        if ((hovered || (pressInside && pressDragged)) && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (selectMode) {
                if (pressDragged) {
                    selB = view.xToFreq(std::clamp(io.MousePos.x, p0.x, p0.x + size.x), p0.x, size.x);
                    selPhase = 2;
                }
                else if (!selPressStarted) {
                    selB = mouseHz;
                    selPhase = 2;
                }
                if (selPhase == 2 && selB < selA) { std::swap(selA, selB); }
                if (selPhase == 2 && selB - selA < 1e3) { selPhase = 1; }
            }
            else if (!pressDragged && !labelTuned) {
                hasTap = true;
                tapHz = mouseHz;
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            dragging = false;
            pressInside = false;
        }
        if (hovered && !selectMode && !labelPressed && !labelTuned && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            ev.tune = true;
            ev.tuneHz = mouseHz;
        }

        std::string popupId = std::string(id) + "_ctx";
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            contextHz = mouseHz;
            contextMarker = -1;
            if (markers) { contextMarker = nearestMarker(*markers, mouseHz, 6.0 * style::uiScale / size.x * view.viewSpan()); }
            ImGui::OpenPopup(popupId.c_str());
        }
        if (ImGui::BeginPopup(popupId.c_str())) {
            ImGui::TextDisabled("%s", formatFrequency(contextHz).c_str());
            ImGui::Separator();
            if (ImGui::MenuItem("Tune receiver here")) {
                ev.tune = true;
                ev.tuneHz = contextHz;
            }
            if (ImGui::MenuItem("Add marker here")) {
                ev.addMarker = true;
                ev.markerHz = contextHz;
            }
            if (contextMarker >= 0 && ImGui::MenuItem("Remove marker")) {
                ev.removeMarker = true;
                ev.markerIndex = contextMarker;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Set sweep start here")) {
                ev.setStart = true;
                ev.rangeHz = contextHz;
            }
            if (ImGui::MenuItem("Set sweep stop here")) {
                ev.setStop = true;
                ev.rangeHz = contextHz;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Zoom in here")) { view.zoom(4.0, contextHz); }
            if (ImGui::MenuItem("Fit full sweep range")) { view.reset(); }
            ImGui::EndPopup();
        }
        return hovered;
    }

    void SpectrumView::drawSpectrum(ImVec2 size, const EngineSnapshot& snap, DisplayConfig& cfg, FrequencyView& view,
                                    const std::vector<Marker>& markers, const OverlayInfo& overlay, ViewEvents& ev) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        size.x = std::max(size.x, 32.0f);
        size.y = std::max(size.y, 32.0f);
        ImVec2 p1(p0.x + size.x, p0.y + size.y);
        float lineH = ImGui::GetTextLineHeight();
        dl->AddRectFilled(p0, p1, COL_BG);

        if (cfg.autoScale && snap.hasFrame) {
            float mn = cfg.minDb, mx = cfg.maxDb;
            autoScaleRange(snap.noiseFloorDb, snap.peakDb, mn, mx);
            cfg.minDb = mn;
            cfg.maxDb = mx;
        }
        const float minDb = cfg.minDb;
        const float maxDb = std::max(cfg.maxDb, cfg.minDb + 1.0f);
        // A status band at the top holds all overlay texts, so they never cover the traces.
        const float bandH = lineH + 6.0f;
        const float plotTop = p0.y + bandH;
        const float plotH = std::max(size.y - bandH, 8.0f);
        auto yOf = [&](float db) {
            float t = (maxDb - db) / (maxDb - minDb);
            return plotTop + std::clamp(t, 0.0f, 1.0f) * plotH;
        };
        dl->AddRectFilled(p0, ImVec2(p1.x, plotTop), IM_COL32(6, 7, 10, 255));

        dl->PushClipRect(p0, p1, true);

        if (cfg.showGrid) {
            double dbStep = niceStep(maxDb - minDb, std::max(2, (int)(plotH / (40.0f * style::uiScale))));
            for (double db = std::ceil(minDb / dbStep) * dbStep; db <= maxDb; db += dbStep) {
                float y = yOf((float)db);
                dl->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), COL_GRID);
                // Labels stay clear of the frequency axis at the bottom
                if (y > p1.y - 2.2f * lineH || y < plotTop + 2) { continue; }
                char buf[16];
                snprintf(buf, sizeof(buf), "%.0f", db);
                labelBox(dl, ImVec2(p0.x + 3, y + 1), buf, COL_TEXT);
            }
            drawFreqGrid(dl, ImVec2(p0.x, plotTop), ImVec2(size.x, plotH), view, false);
        }

        const int width = (int)size.x;
        auto drawTrace = [&](const std::vector<float>& tr, ImU32 col, float thickness) {
            const int bins = (int)tr.size();
            if (bins == 0) { return; }
            pts.clear();
            auto flush = [&]() {
                if (pts.size() >= 2) { dl->AddPolyline(pts.data(), (int)pts.size(), col, 0, thickness); }
                else if (pts.size() == 1) { dl->AddLine(pts[0], ImVec2(pts[0].x + 1, pts[0].y), col, thickness); }
                pts.clear();
            };
            for (int x = 0; x < width; x++) {
                int b0, b1;
                view.columnBins(x, width, bins, b0, b1);
                float m = nanValue();
                for (int b = b0; b < b1; b++) {
                    float v = tr[b];
                    if (std::isfinite(v) && (!std::isfinite(m) || v > m)) { m = v; }
                }
                if (!std::isfinite(m)) {
                    flush();
                    continue;
                }
                pts.push_back(ImVec2(p0.x + (float)x + 0.5f, yOf(m)));
            }
            flush();
        };

        if (cfg.showPersistence) {
            int n = (int)snap.persistence.size();
            for (int i = 0; i < n; i++) {
                int alpha = 25 + (int)(90.0f * (float)(i + 1) / (float)(n + 1));
                drawTrace(snap.persistence[i], IM_COL32(255, 214, 64, alpha), 1.0f);
            }
        }
        if (cfg.showPartial && overlay.sweeping && !snap.partial.empty()) { drawTrace(snap.partial, COL_PARTIAL, 1.0f); }
        if (cfg.showMaxHold) { drawTrace(snap.maxHold, COL_MAX, 1.0f); }
        if (cfg.showAverage) { drawTrace(snap.average, COL_AVG, 1.0f); }
        if (cfg.showCurrent) { drawTrace(snap.current, COL_CURRENT, 1.2f); }

        // Position of the sweep in progress
        if (overlay.sweeping && snap.stats.sweepActive && snap.stats.currentLoHz > 0) {
            float x = (float)view.freqToX(snap.stats.currentLoHz, p0.x, size.x);
            if (x >= p0.x && x <= p1.x) { dashedVLine(dl, x, plotTop, p1.y, COL_PARTIAL, 4.0f * style::uiScale); }
        }

        if (cfg.showNoiseFloor && std::isfinite(snap.noiseFloorDb)) {
            float y = yOf(snap.noiseFloorDb);
            dashedHLine(dl, y, p0.x, p1.x, COL_NOISE, 6.0f * style::uiScale);
            char buf[32];
            snprintf(buf, sizeof(buf), "NF %.1f dB", snap.noiseFloorDb);
            ImVec2 ts = ImGui::CalcTextSize(buf);
            labelBox(dl, ImVec2(p1.x - ts.x - 6, std::clamp(y - ts.y - 3, plotTop + 2, p1.y - 2.2f * lineH)), buf, COL_NOISE);
        }

        // Placed label rectangles, used to keep labels from overlapping.
        std::vector<ImVec4> placed;
        tuneLabels.clear();
        auto overlaps = [&](float x0, float y0, float x1, float y1) {
            float gap = 8.0f * style::uiScale;
            for (auto& r : placed) {
                if (x0 < r.z + gap && x1 + gap > r.x && y0 < r.w && y1 > r.y) { return true; }
            }
            return false;
        };

        if (cfg.showPeaks) {
            float s = 4.0f * style::uiScale;
            std::vector<const DetectedPeak*> visible;
            for (auto& pk : snap.peaks) {
                if (pk.freqHz < view.viewStart() || pk.freqHz > view.viewStop()) { continue; }
                visible.push_back(&pk);
            }
            // Labels only for the strongest signals that have room.
            std::sort(visible.begin(), visible.end(), [](const DetectedPeak* a, const DetectedPeak* b) { return a->levelDb > b->levelDb; });
            int labelled = 0;
            for (auto* pk : visible) {
                if (labelled >= MAX_PEAK_LABELS) { break; }
                float x = (float)view.freqToX(pk->freqHz, p0.x, size.x);
                float y = yOf(pk->levelDb) - 3.0f;
                std::string l = formatFrequency(pk->freqHz, 3);
                ImVec2 ts = ImGui::CalcTextSize(l.c_str());
                float lx = std::clamp(x - ts.x / 2, p0.x + 36.0f * style::uiScale, p1.x - ts.x - 4);
                float ly = std::max(y - 2 * s - ts.y - 2, plotTop + 2);
                if (overlaps(lx - 2, ly - 1, lx + ts.x + 2, ly + ts.y + 1)) { continue; }
                // Only labelled (strongest) peaks get a marker, so the plot stays readable.
                dl->AddTriangleFilled(ImVec2(x, y), ImVec2(x - s, y - 2 * s), ImVec2(x + s, y - 2 * s), COL_PEAK);
                labelBox(dl, ImVec2(lx, ly), l.c_str(), COL_PEAK);
                placed.push_back(ImVec4(lx - 2, ly - 1, lx + ts.x + 2, ly + ts.y + 1));
                tuneLabels.push_back({ ImVec2(lx - 2, ly - 1), ImVec2(lx + ts.x + 2, ly + ts.y + 1), pk->freqHz });
                labelled++;
            }
        }

        if (cfg.showMarkers) {
            for (int i = 0; i < (int)markers.size(); i++) {
                const Marker& m = markers[i];
                if (m.freqHz < view.viewStart() || m.freqHz > view.viewStop()) { continue; }
                float x = (float)view.freqToX(m.freqHz, p0.x, size.x);
                dl->AddLine(ImVec2(x, plotTop), ImVec2(x, p1.y), COL_MARKER, 1.0f);
                float lvl = traceValueAt(snap.current, snap.startHz, snap.stopHz, m.freqHz);
                char buf[160];
                if (std::isfinite(lvl)) {
                    snprintf(buf, sizeof(buf), "%s%s%s %.1f dB", m.name.c_str(), m.name.empty() ? "" : " ", formatFrequency(m.freqHz, 3).c_str(), lvl);
                }
                else {
                    snprintf(buf, sizeof(buf), "%s%s%s", m.name.c_str(), m.name.empty() ? "" : " ", formatFrequency(m.freqHz, 3).c_str());
                }
                ImVec2 ts = ImGui::CalcTextSize(buf);
                float lx = std::min(x + 3, p1.x - ts.x - 4);
                float ly = plotTop + 3;
                while (overlaps(lx - 2, ly - 1, lx + ts.x + 2, ly + ts.y + 1) && ly < p1.y - 3 * lineH) { ly += lineH + 3; }
                labelBox(dl, ImVec2(lx, ly), buf, COL_MARKER);
                placed.push_back(ImVec4(lx - 2, ly - 1, lx + ts.x + 2, ly + ts.y + 1));
                tuneLabels.push_back({ ImVec2(lx - 2, ly - 1), ImVec2(lx + ts.x + 2, ly + ts.y + 1), m.freqHz });
            }
        }

        if (hasTap && tapHz >= view.viewStart() && tapHz <= view.viewStop()) {
            float x = (float)view.freqToX(tapHz, p0.x, size.x);
            dashedVLine(dl, x, plotTop, p1.y, IM_COL32(120, 230, 255, 200), 3.0f * style::uiScale);
        }

        // Status band (one line above the plot)
        float bx = p0.x + 4;
        float by = p0.y + 3;
        auto bandText = [&](const char* t, ImU32 col) {
            dl->AddText(ImVec2(bx, by), col, t);
            bx += ImGui::CalcTextSize(t).x + 14.0f * style::uiScale;
        };
        if (overlay.synthetic) { bandText("DEMO (synthetic, not a measurement)", COL_DEMO); }
        if (!overlay.headline.empty()) { bandText(overlay.headline.c_str(), overlay.headlineColor); }
        if (overlay.paused) { bandText("PAUSED", COL_WARN); }
        if (snap.hasFrame && !snap.lastComplete) {
            char buf[96];
            snprintf(buf, sizeof(buf), "INCOMPLETE SWEEP: %d/%d segments missing", snap.lastSegmentsExpected - snap.lastSegmentsReceived, snap.lastSegmentsExpected);
            bandText(buf, COL_WARN);
        }
        if (overlay.sweeping && !snap.partial.empty() && cfg.showPartial) {
            const char* t = "grey: sweep in progress";
            ImVec2 ts = ImGui::CalcTextSize(t);
            if (p1.x - ts.x - 4 > bx) { dl->AddText(ImVec2(p1.x - ts.x - 4, by), COL_PARTIAL, t); }
        }
        if (!snap.hasFrame) {
            const char* t = overlay.sweeping ? "Waiting for the first complete sweep..." : "No measurement. Press Start.";
            ImVec2 ts = ImGui::CalcTextSize(t);
            dl->AddText(ImVec2(p0.x + (size.x - ts.x) / 2, plotTop + (plotH - ts.y) / 2), COL_TEXT, t);
        }

        drawRangeOverlays(dl, ImVec2(p0.x, plotTop), p1, view);
        bool hovered = handleInteraction("##sa_spectrum", p0, size, view, ev, &markers);
        hoverHz = hovered ? view.xToFreq(ImGui::GetIO().MousePos.x, p0.x, size.x) : std::numeric_limits<double>::quiet_NaN();
        if (ImGui::GetFrameCount() > tooltipHoldFrame) { tooltipShown = false; }
        if (hovered && !dragging) {
            ImVec2 mp = ImGui::GetIO().MousePos;
            dl->AddLine(ImVec2(mp.x, p0.y), ImVec2(mp.x, p1.y), COL_CURSOR);
            dl->AddLine(ImVec2(p0.x, mp.y), ImVec2(p1.x, mp.y), COL_CURSOR);
            double f = hoverHz;
            float cur = traceValueAt(snap.current, snap.startHz, snap.stopHz, f);
            float avg = traceValueAt(snap.average, snap.startHz, snap.stopHz, f);
            float mx = traceValueAt(snap.maxHold, snap.startHz, snap.stopHz, f);
            float cursorDb = maxDb - (mp.y - p0.y) / size.y * (maxDb - minDb);
            ImGui::BeginTooltip();
            ImGui::Text("%s", formatFrequency(f).c_str());
            if (std::isfinite(cur)) { ImGui::Text("Level:   %.1f dB", cur); }
            else { ImGui::TextDisabled("Level:   not measured"); }
            if (cfg.showAverage && std::isfinite(avg)) { ImGui::Text("Average: %.1f dB", avg); }
            if (cfg.showMaxHold && std::isfinite(mx)) { ImGui::Text("Max:     %.1f dB", mx); }
            ImGui::TextDisabled("Cursor:  %.1f dB", cursorDb);
            if (snap.stats.lastFrameTimeNs > 0) {
                ImGui::TextDisabled("Measured %.1f s ago", (double)(nowNs() - snap.stats.lastFrameTimeNs) * 1e-9);
            }
            ImGui::TextDisabled("Tap here or double-click: tune receiver, right-click: menu");
            captureTooltip(f);
            ImGui::EndTooltip();
        }
        dl->PopClipRect();
    }

    void SpectrumView::rowPixels(const uint8_t* row, int srcBins, uint32_t* out) {
        const int w = (int)colB0.size();
        for (int x = 0; x < w; x++) {
            uint8_t q = WaterfallHistory::QUANT_NAN;
            for (int b = colB0[x]; b < colB1[x] && b < srcBins; b++) {
                uint8_t v = row[b];
                if (v != WaterfallHistory::QUANT_NAN && (q == WaterfallHistory::QUANT_NAN || v > q)) { q = v; }
            }
            out[x] = lut[q];
        }
    }

    void SpectrumView::uploadRows(WaterfallHistory& hist, int width, bool full) {
        const int h = hist.capacity();
        const int srcW = hist.width();
        const uint64_t total = hist.totalPushed();
        rowBuf.resize(width);
        if (!full && total - texUploaded > (uint64_t)h) { full = true; }
        if (full) {
            if (!tex) { glGenTextures(1, &tex); }
            fullBuf.assign((size_t)width * h, COL_NODATA);
            int rows = hist.rows();
            for (int age = 0; age < rows; age++) {
                uint64_t seq = total - 1 - (uint64_t)age;
                const uint8_t* r = hist.rowByAge(age);
                if (r) { rowPixels(r, srcW, &fullBuf[(size_t)(seq % (uint64_t)h) * width]); }
            }
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, fullBuf.data());
            texW = width;
            texH = h;
            texUploaded = total;
            return;
        }
        if (total == texUploaded) { return; }
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        for (uint64_t seq = texUploaded; seq < total; seq++) {
            int age = (int)(total - 1 - seq);
            const uint8_t* r = hist.rowByAge(age);
            if (!r) { continue; }
            rowPixels(r, srcW, rowBuf.data());
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, (GLint)(seq % (uint64_t)h), width, 1, GL_RGBA, GL_UNSIGNED_BYTE, rowBuf.data());
        }
        texUploaded = total;
    }

    float SpectrumView::axisStripHeight() const {
        return ImGui::GetTextLineHeight() * 2.7f;
    }

    bool SpectrumView::drawAxisStrip(ImVec2 size, FrequencyView& view, DisplayConfig& cfg, float totalH) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        size.x = std::max(size.x, 32.0f);
        size.y = std::max(size.y, 16.0f);
        ImVec2 p1(p0.x + size.x, p0.y + size.y);
        float lineH = ImGui::GetTextLineHeight();
        float scale = style::uiScale;
        dl->AddRectFilled(p0, p1, IM_COL32(26, 28, 36, 255));
        dl->PushClipRect(p0, p1, true);

        // Band names (the receiver's band plan), upper row
        const float stripH = lineH * 1.25f;
        const float stripTop = p0.y + 2.0f * scale;
        const float stripBottom = stripTop + stripH;
        if (cfg.showBandStrip && bands != nullptr) {
            for (const auto& b : bands->bands) {
                if (b.end < view.viewStart() || b.start > view.viewStop()) { continue; }
                float xa = (float)view.freqToX(std::max(b.start, view.viewStart()), p0.x, size.x);
                float xb = (float)view.freqToX(std::min(b.end, view.viewStop()), p0.x, size.x);
                if (xb - xa < 1.0f) { continue; }
                ImU32 col = IM_COL32(255, 255, 255, 255), colTrans = IM_COL32(255, 255, 255, 80);
                auto ct = bandplan::colorTable.find(b.type);
                if (ct != bandplan::colorTable.end()) {
                    col = ct->second.colorValue;
                    colTrans = ct->second.transColorValue;
                }
                dl->AddRectFilled(ImVec2(xa, stripTop), ImVec2(xb, stripBottom), colTrans);
                if (b.start >= view.viewStart()) { dl->AddLine(ImVec2(xa, stripTop), ImVec2(xa, stripBottom), col, scale); }
                if (b.end <= view.viewStop()) { dl->AddLine(ImVec2(xb, stripTop), ImVec2(xb, stripBottom), col, scale); }
                ImVec2 ts = ImGui::CalcTextSize(b.name.c_str());
                if (ts.x + 4.0f <= xb - xa) {
                    dl->AddText(ImVec2((xa + xb) / 2.0f - ts.x / 2.0f, stripTop + stripH / 2.0f - ts.y / 2.0f), IM_COL32(255, 255, 255, 255), b.name.c_str());
                }
                else if ((b.type == "wifi" || b.type == "mic") && b.name.find(' ') != std::string::npos) {
                    // Too narrow for the full name: just the channel / range letter (second word)
                    size_t sp1 = b.name.find(' ');
                    size_t sp2 = b.name.find(' ', sp1 + 1);
                    std::string shortName = b.name.substr(sp1 + 1, sp2 == std::string::npos ? std::string::npos : sp2 - sp1 - 1);
                    ImVec2 ss = ImGui::CalcTextSize(shortName.c_str());
                    if (ss.x + 2.0f <= xb - xa) {
                        dl->AddText(ImVec2((xa + xb) / 2.0f - ss.x / 2.0f, stripTop + stripH / 2.0f - ss.y / 2.0f), IM_COL32(255, 255, 255, 255), shortName.c_str());
                    }
                }
            }
        }

        // Frequency axis, lower row
        int lines = std::max(2, (int)(size.x / (110.0f * scale)));
        double step = niceStep(view.viewSpan(), lines);
        if (step > 0) {
            double first = std::ceil(view.viewStart() / step) * step;
            int guard = 0;
            for (double f = first; f <= view.viewStop() && guard < 200; f += step, guard++) {
                float x = (float)view.freqToX(f, p0.x, size.x);
                dl->AddLine(ImVec2(x, stripBottom + 2.0f * scale), ImVec2(x, stripBottom + 7.0f * scale), COL_TEXT, scale);
                std::string l = axisLabel(f, step);
                dl->AddText(ImVec2(x + 3.0f * scale, stripBottom + 3.0f * scale), COL_TEXT, l.c_str());
            }
            const char* u = axisUnit(view.viewStart(), step);
            ImVec2 ts = ImGui::CalcTextSize(u);
            labelBox(dl, ImVec2(p0.x + size.x - ts.x - 4, p1.y - lineH - 1), u, COL_TEXT);
        }
        dl->PopClipRect();
        // Frame lines show where the plots end
        dl->AddLine(p0, ImVec2(p1.x, p0.y), IM_COL32(90, 90, 100, 255));
        dl->AddLine(ImVec2(p0.x, p1.y), p1, IM_COL32(90, 90, 100, 255));

        // Dragging moves the view; the plots above and below stay free for picking frequencies
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetCursorScreenPos(p0);
        ImGui::InvisibleButton("##sa_axis", size, ImGuiButtonFlags_MouseButtonLeft);
        bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { stopPan(); }
        ImGui::SetItemUsingMouseWheel();
        if (hovered || ImGui::IsItemActive()) { ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW); }
        if (hovered && io.MouseWheel != 0.0f) {
            stopPan();
            view.zoom(std::pow(1.25, (double)io.MouseWheel), view.xToFreq(io.MousePos.x, p0.x, size.x));
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { stopPan(); }
        bool axisDragging = ImGui::IsItemActive() && !pinching && !gripTouch && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f);
        double panDelta = 0;
        if (axisDragging) { panDelta = -(double)io.MouseDelta.x / (double)size.x; }
        handlePinch(p0, size, view);
        if (pinching) { panDelta = 0; }

        // Grip in the middle: drag it up or down to change the height of the spectrum against the
        // waterfall. Finger drags are followed directly (vertical drags are not sent as mouse drags).
        bool resizeEnded = false;
        const float gripW = 64.0f * scale;
        const ImVec2 g0(p0.x + size.x / 2.0f - gripW / 2.0f, p0.y), g1(g0.x + gripW, p1.y);
        ImGui::SetCursorScreenPos(g0);
        ImGui::InvisibleButton("##sa_grip", ImVec2(gripW, size.y), ImGuiButtonFlags_MouseButtonLeft);
        bool gripActive = ImGui::IsItemActive();
        if (ImGui::IsItemHovered() || gripActive) { ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS); }
        if (ImGui::IsItemActivated()) { gripMoved = 0; stopPan(); }
        if (gripActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            gripMoved += std::fabs(io.MouseDelta.x) + std::fabs(io.MouseDelta.y);
            cfg.spectrumHeightFrac = std::clamp(cfg.spectrumHeightFrac + io.MouseDelta.y / std::max(totalH, 1.0f), 0.15f, 0.85f);
            // Sideways movement over the grip still moves the view
            if (!pinching) { panDelta = -(double)io.MouseDelta.x / (double)size.x; }
            gripShownUntil = ImGui::GetTime() + 3.5;
        }
        if (ImGui::IsItemDeactivated()) {
            resizeEnded = true;
            if (gripMoved < 8.0f * scale) { gripShownUntil = ImGui::GetTime() + 3.5; }   // a tap shows the grip
        }
        const bool fd = io.FingerDown[0];
        const ImVec2 fp = io.FingerPos[0];
        if (fd && !gripPrevDown) {
            gripTouch = fp.x >= g0.x && fp.x <= g1.x && fp.y >= g0.y - 10.0f * scale && fp.y <= g1.y + 10.0f * scale;
            gripLastY = fp.y;
            gripStartY = fp.y;
        }
        gripPrevDown = fd;
        if (gripTouch && fd) {
            cfg.spectrumHeightFrac = std::clamp(cfg.spectrumHeightFrac + (fp.y - gripLastY) / std::max(totalH, 1.0f), 0.15f, 0.85f);
            gripLastY = fp.y;
            gripShownUntil = ImGui::GetTime() + 3.5;
        }
        if (gripTouch && !fd) {
            gripTouch = false;
            resizeEnded = true;
            if (std::fabs(fp.y - gripStartY) < 8.0f * scale) { gripShownUntil = ImGui::GetTime() + 3.5; }   // a tap shows the grip
        }
        if (panDelta != 0) {
            double before = view.viewStart();
            view.pan(panDelta * view.viewSpan());
            double dt = std::clamp((double)io.DeltaTime, 0.001, 0.05);
            panVelocity = (view.viewStart() != before) ? std::clamp(panDelta / dt, -4.0, 4.0) : 0;
            panStopVelocity = std::max(0.02, std::fabs(panVelocity) * 0.02);
        }
        else if (axisDragging || gripActive || gripTouch) {
            panVelocity *= std::exp(-12.0 * std::clamp((double)io.DeltaTime, 0.0, 0.05));
        }
        else if (panVelocity != 0 && !io.MouseDown[0]) {
            double dt = std::clamp((double)io.DeltaTime, 0.0, 0.05);
            double decay = std::exp(-5.0 * dt);
            double before = view.viewStart();
            view.pan(panVelocity * view.viewSpan() * (1.0 - decay) / 5.0);
            panVelocity *= decay;
            if (std::fabs(panVelocity) < panStopVelocity || view.viewStart() == before) { stopPan(); }
        }
        ImDrawList* gdl = ImGui::GetWindowDrawList();
        const bool gripOn = gripActive || gripTouch;
        if (!gripOn && ImGui::GetTime() >= gripShownUntil) { return resizeEnded; }
        float cy = (p0.y + p1.y) / 2.0f, gh = size.y * 0.5f;
        gdl->AddRectFilled(ImVec2(g0.x + 6 * scale, cy - gh / 2.0f), ImVec2(g1.x - 6 * scale, cy + gh / 2.0f), gripOn ? IM_COL32(150, 160, 190, 235) : IM_COL32(70, 74, 88, 220), gh / 2.0f);
        for (int i = -1; i <= 1; i++) {
            gdl->AddLine(ImVec2(p0.x + size.x / 2.0f - 12 * scale, cy + i * 3.0f * scale), ImVec2(p0.x + size.x / 2.0f + 12 * scale, cy + i * 3.0f * scale), IM_COL32(190, 195, 215, 255), scale);
        }
        return resizeEnded;
    }

    void SpectrumView::drawWaterfall(ImVec2 size, WaterfallHistory& hist, const WaterfallConfig& cfg, FrequencyView& view,
                                     const OverlayInfo& overlay, ViewEvents& ev) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        size.x = std::max(size.x, 32.0f);
        size.y = std::max(size.y, 16.0f);
        ImVec2 p1(p0.x + size.x, p0.y + size.y);
        dl->AddRectFilled(p0, p1, COL_NODATA);
        colorTable(cfg);

        // A new GL context (Android recreates it when the window comes back)
        // invalidates our texture name; it may now belong to another texture,
        // e.g. the font atlas, which must never be overwritten.
        uint64_t glGen = backend::glContextGeneration();
        GLuint fontTex = (GLuint)(intptr_t)ImGui::GetIO().Fonts->TexID;
        if (glGen != texGlGeneration || (tex != 0 && tex == fontTex)) {
            tex = 0;
            texGlGeneration = glGen;
            wfDirty = true;
        }

        const int width = std::clamp((int)size.x, 16, 4096);
        int h = 0;
        uint64_t shownTotal = 0;
        {
            auto lck = hist.lock();
            h = hist.capacity();
            bool viewChanged = texViewStart != view.viewStart() || texViewStop != view.viewStop();
            bool full = wfDirty || !tex || width != texW || h != texH || hist.generation() != texGeneration || viewChanged;
            if (full || !cfg.paused) {
                if (full || (int)colB0.size() != width) {
                    colB0.resize(width);
                    colB1.resize(width);
                    for (int x = 0; x < width; x++) { view.columnBins(x, width, std::max(hist.width(), 1), colB0[x], colB1[x]); }
                }
                if (h > 0 && hist.width() > 0) { uploadRows(hist, width, full); }
                texGeneration = hist.generation();
                texViewStart = view.viewStart();
                texViewStop = view.viewStop();
                wfDirty = false;
            }
            shownTotal = texUploaded;

            if (tex && h > 0) {
                // Each sweep gets at least WF_MIN_LINE_PX pixels so new lines are clearly
                // visible; only the newest lines that fit are drawn (newest at the top).
                float rowPx = std::max(size.y / (float)h, WF_MIN_LINE_PX * style::uiScale);
                int fit = std::max(1, (int)std::floor(size.y / rowPx));
                int shown = (int)std::min<uint64_t>(std::min<uint64_t>(shownTotal, (uint64_t)h), (uint64_t)fit);
                int head = (int)(shownTotal % (uint64_t)h);
                int partA = std::min(head, shown);
                if (partA > 0) {
                    dl->AddImage((ImTextureID)(intptr_t)tex, p0, ImVec2(p1.x, p0.y + partA * rowPx), ImVec2(0, (float)head / h), ImVec2(1, (float)(head - partA) / h));
                }
                int partB = shown - partA;
                if (partB > 0) {
                    float y0 = p0.y + partA * rowPx;
                    dl->AddImage((ImTextureID)(intptr_t)tex, ImVec2(p0.x, y0), ImVec2(p1.x, y0 + partB * rowPx), ImVec2(0, 1), ImVec2(1, (float)(h - partB) / h));
                }
                wfRowPx = rowPx;
                wfShown = shown;
                // Rows built from incomplete sweeps get a red mark on the left edge.
                uint64_t lag = hist.totalPushed() - shownTotal;
                for (int age = 0; age < shown; age++) {
                    int histAge = (int)(lag + age);
                    if (histAge >= hist.rows()) { break; }
                    WaterfallRowInfo info;
                    hist.rowByAge(histAge, &info);
                    if (!info.complete) {
                        float y = p0.y + age * rowPx;
                        dl->AddRectFilled(ImVec2(p0.x, y), ImVec2(p0.x + 3 * style::uiScale, y + std::max(rowPx, 1.0f)), COL_WARN);
                    }
                }
            }
        }

        dl->PushClipRect(p0, p1, true);
        drawFreqGrid(dl, p0, size, view, false);
        if (overlay.displayPaused) { labelBox(dl, ImVec2(p0.x + 6, p0.y + 4), "WATERFALL PAUSED", COL_WARN); }
        if (overlay.synthetic) { labelBox(dl, ImVec2(p0.x + 6, p1.y - ImGui::GetTextLineHeight() - 4), "DEMO", COL_DEMO); }

        drawRangeOverlays(dl, p0, p1, view);
        bool hovered = handleInteraction("##sa_waterfall", p0, size, view, ev, nullptr);
        if (hovered && !dragging && h > 0) {
            ImVec2 mp = ImGui::GetIO().MousePos;
            dl->AddLine(ImVec2(mp.x, p0.y), ImVec2(mp.x, p1.y), COL_CURSOR);
            double f = view.xToFreq(mp.x, p0.x, size.x);
            int age = (int)((mp.y - p0.y) / std::max(wfRowPx, 0.01f));
            auto lck = hist.lock();
            uint64_t lag = hist.totalPushed() - shownTotal;
            WaterfallRowInfo info;
            const uint8_t* row = (age < wfShown) ? hist.rowByAge((int)(lag + age), &info) : nullptr;
            ImGui::BeginTooltip();
            ImGui::Text("%s", formatFrequency(f).c_str());
            if (row) {
                int x = std::clamp((int)((mp.x - p0.x) / size.x * (float)width), 0, width - 1);
                uint8_t q = WaterfallHistory::QUANT_NAN;
                for (int b = colB0[x]; b < colB1[x] && b < hist.width(); b++) {
                    if (row[b] != WaterfallHistory::QUANT_NAN && (q == WaterfallHistory::QUANT_NAN || row[b] > q)) { q = row[b]; }
                }
                if (q != WaterfallHistory::QUANT_NAN) { ImGui::Text("Level: %.1f dB", WaterfallHistory::dequantize(q)); }
                else { ImGui::TextDisabled("Level: not measured"); }
                ImGui::TextDisabled("%.1f s ago%s", (double)(nowNs() - info.timeNs) * 1e-9, info.complete ? "" : " (incomplete sweep)");
            }
            ImGui::TextDisabled("Tap here or double-click: tune receiver");
            captureTooltip(f);
            ImGui::EndTooltip();
        }
        dl->PopClipRect();
    }
}
