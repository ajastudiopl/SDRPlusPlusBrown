#include <gui/dialogs/loading_screen.h>
#include <gui/main_window.h>
#include <imgui.h>
#include <gui/icons.h>
#include <gui/style.h>
#include <credits.h>
#include <gui/gui.h>
#include <backend.h>

namespace LoadingScreen {
    ImVec2 imageSize(128.0f, 128.0f);

    void init() {
        imageSize = ImVec2(128.0f * style::uiScale, 128.0f * style::uiScale);
    }

    void show(std::string msg) {
        backend::beginFrame();

        // Black splash background
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImGui::Begin("Main", NULL, WINDOW_FLAGS);


        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 20.0f));
        ImGui::OpenPopup("Credits");
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::BeginPopupModal("Credits", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground);

        ImGui::PushFont(style::hugeFont);
        ImGui::TextUnformatted("SDR++");
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::Image(icons::LOGO, imageSize);
        // hugeFont only has glyphs for "SDR+ ", so the suffix uses bigFont
        ImGui::PushFont(style::bigFont);
        ImGui::TextUnformatted("by AJA");
        ImGui::PopFont();

        ImVec2 origPos = ImGui::GetCursorPos();
        ImGui::SetCursorPosY(origPos.y + 50);
        ImGui::Text("%s", msg.c_str());
        ImGui::SetCursorPos(origPos);

        ImGui::EndPopup();
        ImGui::PopStyleVar(1);
        ImGui::PopStyleColor(1);

        ImGui::End();
        ImGui::PopStyleColor(1);

        backend::render(false);
    }
}
