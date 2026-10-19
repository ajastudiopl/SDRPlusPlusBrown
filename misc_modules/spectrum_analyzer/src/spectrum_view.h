#pragma once
#include "engine/analyzer_settings.h"
#include "engine/sweep_engine.h"
#include "engine/view_model.h"
#include <imgui.h>
#include <utils/opengl_include_code.h>

namespace spectrum {
    // Requests produced by mouse / touch interaction with the plots.
    struct ViewEvents {
        bool tune = false;
        double tuneHz = 0;
        bool addMarker = false;
        double markerHz = 0;
        bool setStart = false;
        bool setStop = false;
        double rangeHz = 0;
        bool removeMarker = false;
        int markerIndex = -1;
    };

    struct OverlayInfo {
        bool synthetic = false;      // demo data
        bool sweeping = false;
        bool paused = false;
        bool displayPaused = false;
        std::string headline;        // e.g. state / warnings
        uint32_t headlineColor = 0xFFFFFFFF;
    };

    // ImGui rendering of the wideband spectrum and waterfall. The waterfall is
    // a GPU texture used as a ring buffer: each finished sweep uploads one row,
    // the full texture is rebuilt only when the view, size or colours change.
    class SpectrumView {
    public:
        ~SpectrumView();

        void setPalette(const float* rgb, int count);
        void invalidateWaterfall() { wfDirty = true; }
        // Frees the GPU texture; call on the UI thread while the GL context exists.
        void releaseGl();

        void drawSpectrum(ImVec2 size, const EngineSnapshot& snap, DisplayConfig& cfg, FrequencyView& view,
                          const std::vector<Marker>& markers, const OverlayInfo& overlay, ViewEvents& ev);
        void drawWaterfall(ImVec2 size, WaterfallHistory& hist, const WaterfallConfig& cfg, FrequencyView& view,
                           const OverlayInfo& overlay, ViewEvents& ev);

        // Colour table (RGBA as uint32, R in the low byte) for quantised levels.
        const uint32_t* colorTable(const WaterfallConfig& cfg);

        // Last hovered frequency (for status display), NaN when not hovering.
        double hoverHz = std::numeric_limits<double>::quiet_NaN();
        // Frequency selected by a tap / click without dragging (touch screens).
        bool hasTap = false;
        double tapHz = 0;

    private:
        bool handleInteraction(const char* id, ImVec2 p0, ImVec2 size, FrequencyView& view, ViewEvents& ev,
                               const std::vector<Marker>* markers);
        void drawFreqGrid(ImDrawList* dl, ImVec2 p0, ImVec2 size, FrequencyView& view, bool labels);
        void rebuildLut(const WaterfallConfig& cfg);
        void uploadRows(WaterfallHistory& hist, int width, bool full);
        void rowPixels(const uint8_t* row, int srcBins, uint32_t* out);

        // palette
        std::vector<float> palette;
        uint32_t lut[256];
        bool lutValid = false;
        WaterfallConfig lutCfg;

        // waterfall texture
        GLuint tex = 0;
        int texW = 0;
        int texH = 0;
        uint64_t texGeneration = ~0ull;
        uint64_t texUploaded = 0;
        double texViewStart = 0;
        double texViewStop = 0;
        bool wfDirty = true;
        std::vector<int> colB0, colB1;
        std::vector<uint32_t> rowBuf;
        std::vector<uint32_t> fullBuf;

        // interaction
        bool dragging = false;
        bool pressDragged = false;
        float dragLastX = 0;
        double contextHz = 0;
        int contextMarker = -1;
        std::vector<ImVec2> pts;
    };
}
