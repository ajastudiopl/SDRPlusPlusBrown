#include <gui/menus/theme.h>
#include <gui/gui.h>
#include <core.h>
#include <gui/style.h>
#include <fstream>
#include <cstring>
#include <imgui.h>

namespace thememenu {
    int themeId;
    std::vector<std::string> themeNames;
    std::string themeNamesTxt;

    // The style is stored unscaled: init() applies ScaleAllSizes(uiScale) after loading.
    // imgui_style_v1.bin stored already-scaled sizes, which grew on every start, so only
    // its colours are still used.
    void saveStyle() {
        std::string path = std::string(core::getRoot()) + "/imgui_style_v2.bin";
        std::ofstream file(path, std::ios::binary);
        if (file) {
            ImGuiStyle style = ImGui::GetStyle();
            if (style::uiScale > 0) { style.ScaleAllSizes(1.0f / style::uiScale); }
            file.write((char*)&style, sizeof(ImGuiStyle));
        }
    }

    void loadStyle() {
        std::string root = core::getRoot();
        std::ifstream file(root + "/imgui_style_v2.bin", std::ios::binary);
        if (file) {
            ImGuiStyle style;
            if (file.read((char*)&style, sizeof(ImGuiStyle))) { ImGui::GetStyle() = style; }
            return;
        }
        std::ifstream oldFile(root + "/imgui_style_v1.bin", std::ios::binary);
        if (oldFile) {
            ImGuiStyle style;
            if (oldFile.read((char*)&style, sizeof(ImGuiStyle))) {
                memcpy(ImGui::GetStyle().Colors, style.Colors, sizeof(style.Colors));
            }
        }
    }

    void init(std::string resDir) {
        // TODO: Not hardcode theme directory
        gui::themeManager.loadThemesFromDir(resDir + "/themes/");

        // Select Dark theme by default
        themeNames = gui::themeManager.getThemeNames();
        themeNames.push_back("ImGUI Dark");
        themeNames.push_back("ImGUI Light");
        themeNames.push_back("ImGUI Classic");
        auto it = std::find(themeNames.begin(), themeNames.end(), "Dark");
        themeId = std::distance(themeNames.begin(), it);

        // Load saved theme from config if exists
        core::configManager.acquire();
        if (core::configManager.conf.contains("theme")) {
            std::string savedTheme = core::configManager.conf["theme"];
            auto it2 = std::find(themeNames.begin(), themeNames.end(), savedTheme);
            if (it2 != themeNames.end()) {
                themeId = std::distance(themeNames.begin(), it2);
            }
        }
        core::configManager.release();

        // Load saved style if exists
        loadStyle();

        // Apply scaling
        ImGui::GetStyle().ScaleAllSizes(style::uiScale);

        themeNamesTxt = "";
        for (auto name : themeNames) {
            themeNamesTxt += name;
            themeNamesTxt += '\0';
        }
    }

    void applyTheme() {
        if (themeNames[themeId] == "ImGUI Dark") {
            ImGui::StyleColorsDark();
        }
        else if (themeNames[themeId] == "ImGUI Light") {
            ImGui::StyleColorsLight();
        }
        else if (themeNames[themeId] == "ImGUI Classic") {
            ImGui::StyleColorsClassic();
        }
        else {
            gui::themeManager.applyTheme(themeNames[themeId]);
        }
        core::configManager.acquire();
        core::configManager.conf["theme"] = themeNames[themeId];
        core::configManager.release(true);
    }

    void draw(void* ctx) {
        float menuWidth = ImGui::GetContentRegionAvail().x;
        ImGui::LeftLabel("Theme");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        ImGuiStyle styleBefore = ImGui::GetStyle();
        if (ImGui::Combo("##theme_select_combo", &themeId, themeNamesTxt.c_str())) {
            applyTheme();
            ImGuiStyle styleAfter = ImGui::GetStyle();
            if (memcmp(&styleBefore, &styleAfter, sizeof(ImGuiStyle)) != 0) {
                saveStyle();
            }
        }
    }
}