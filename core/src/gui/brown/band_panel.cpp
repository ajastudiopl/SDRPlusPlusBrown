#define IMGUI_DEFINE_MATH_OPERATORS
#include "band_panel.h"
#include "../../../misc_modules/frequency_manager/src/frequency_manager.h"
#include <core.h>
#include <config.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <algorithm>

static const char* modeName(int mode) {
    static const char* names[] = { "NFM", "WFM", "AM", "DSB", "USB", "CW", "LSB", "RAW" };
    return (mode >= 0 && mode < 8) ? names[mode] : "?";
}

const std::vector<BandPanel::Band>& BandPanel::bands() {
    // the modulation is the one normally used on the band: SSB is LSB below 10 MHz and USB above
    static const std::vector<Band> list = {
        { "FM", 87.5e6, 108e6, 100.0e6, 1 },
        { "AIR", 118e6, 137e6, 121.5e6, 2 },
        { "CB", 26.9e6, 27.5e6, 27.205e6, 2 },
        { "PMR", 446e6, 446.2e6, 446.00625e6, 0 },
        { "2 m", 144e6, 146e6, 145.5e6, 0 },
        { "70 cm", 430e6, 440e6, 433.5e6, 0 },
        { "6 m", 50e6, 52e6, 50.2e6, 4 },
        { "10 m", 28e6, 29.7e6, 28.4e6, 4 },
        { "12 m", 24.89e6, 24.99e6, 24.95e6, 4 },
        { "15 m", 21e6, 21.45e6, 21.2e6, 4 },
        { "17 m", 18.068e6, 18.168e6, 18.12e6, 4 },
        { "20 m", 14e6, 14.35e6, 14.2e6, 4 },
        { "30 m", 10.1e6, 10.15e6, 10.12e6, 4 },
        { "40 m", 7e6, 7.2e6, 7.1e6, 6 },
        { "60 m", 5.351e6, 5.367e6, 5.358e6, 4 },
        { "80 m", 3.5e6, 3.8e6, 3.7e6, 6 },
        { "160 m", 1.81e6, 2e6, 1.9e6, 6 },
        { "MW", 0.531e6, 1.602e6, 1.0e6, 2 },
    };
    return list;
}

int BandPanel::bandOf(double frequency) {
    auto& b = bands();
    for (int i = 0; i < (int)b.size(); i++) {
        if (frequency >= b[i].low && frequency <= b[i].high) { return i; }
    }
    return -1;
}

void BandPanel::loadBands() {
    loaded = true;
    lastOnBand.assign(bands().size(), Last{ 0, 0 });
    core::configManager.acquire();
    json conf = core::configManager.conf.contains("bandPanel") ? core::configManager.conf["bandPanel"] : json::object();
    core::configManager.release(false);
    if (conf.contains("last") && conf["last"].is_object()) {
        auto& b = bands();
        for (int i = 0; i < (int)b.size(); i++) {
            if (conf["last"].contains(b[i].label)) {
                lastOnBand[i].frequency = conf["last"][b[i].label].value("f", 0.0);
                lastOnBand[i].mode = conf["last"][b[i].label].value("m", 0);
            }
        }
    }
}

void BandPanel::saveBands() {
    json conf = json::object();
    conf["last"] = json::object();
    auto& b = bands();
    for (int i = 0; i < (int)b.size(); i++) {
        if (lastOnBand[i].frequency > 0) {
            conf["last"][b[i].label] = { { "f", lastOnBand[i].frequency }, { "m", lastOnBand[i].mode } };
        }
    }
    core::configManager.acquire();
    core::configManager.conf["bandPanel"] = conf;
    core::configManager.release(true);
}

void BandPanel::selectBand(int index, const Callbacks& cb) {
    auto& b = bands();
    // remember where we were on the band we leave
    double now = cb.getFrequency();
    int from = bandOf(now);
    if (from >= 0) { lastOnBand[from] = Last{ now, cb.getMode() }; }
    double f = lastOnBand[index].frequency > 0 ? lastOnBand[index].frequency : b[index].defaultFrequency;
    int m = lastOnBand[index].frequency > 0 ? lastOnBand[index].mode : b[index].defaultMode;
    cb.tuneTo(m, f, 0);
    saveBands();
}

static std::string formatFrequency(double f) {
    char buf[32];
    long long hz = (long long)llround(f);
    snprintf(buf, sizeof buf, "%lld.%03lld.%03lld", hz / 1000000, (hz / 1000) % 1000, hz % 1000);
    return buf;
}

extern bool ImGuiTouchScrollSuppress;

void BandPanel::drawBands(const Callbacks& cb, float width, float height) {
    auto& b = bands();
    auto indexOf = [&](const char* label) {
        for (int i = 0; i < (int)b.size(); i++) { if (!strcmp(b[i].label, label)) { return i; } }
        return -1;
    };
    // the most used ones first; the other nine are one button away. Every band is in the Mode menu.
    struct Cell { const char* label; int action; };      // action: 0 band, 1 other nine, 2 first nine, 3 Mode menu, 4 nothing
    static const Cell layers[2][9] = {
        { { "80 m", 0 }, { "40 m", 0 }, { "30 m", 0 }, { "20 m", 0 }, { "17 m", 0 }, { "15 m", 0 }, { "12 m", 0 }, { "10 m", 0 }, { "More", 1 } },
        { { "160 m", 0 }, { "60 m", 0 }, { "6 m", 0 }, { "2 m", 0 }, { "70 cm", 0 }, { "MW", 0 }, { "All...", 3 }, { "", 4 }, { "Back", 2 } },
    };
    const int columns = 3, rows = 3;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    float bw = (width - gap * (columns - 1)) / columns;
    float bh = (height - gap * (rows - 1)) / rows;
    int current = bandOf(cb.getFrequency());
    for (int i = 0; i < 9; i++) {
        const Cell& cell = layers[bandLayer][i];
        if (i % columns != 0) { ImGui::SameLine(); }
        ImGui::PushID(i);
        if (cell.action == 4) {
            ImGui::Dummy(ImVec2(bw, bh));
        } else if (cell.action == 0) {
            int index = indexOf(cell.label);
            bool isCurrent = index >= 0 && index == current;
            if (isCurrent) { ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)); }
            if (ImGui::Button(cell.label, ImVec2(bw, bh)) && index >= 0) { selectBand(index, cb); }
            if (isCurrent) { ImGui::PopStyleColor(); }
        } else {
            // the buttons that change what the others do look different
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
            if (ImGui::Button(cell.label, ImVec2(bw, bh))) {
                if (cell.action == 1) { bandLayer = 1; }
                else if (cell.action == 2) { bandLayer = 0; }
                else if (cell.action == 3 && cb.openFullMenu) { cb.openFullMenu(); }
            }
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
    }
}

void BandPanel::drawAllBands(const Callbacks& cb, float width) {
    if (!loaded) { loadBands(); }
    auto& b = bands();
    int current = bandOf(cb.getFrequency());
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float h = ImGui::GetFontSize() * 2.6f;
    float x = 0;
    for (int i = 0; i < (int)b.size(); i++) {
        float w = std::max(h * 1.6f, ImGui::CalcTextSize(b[i].label).x + ImGui::GetFontSize() * 1.6f);
        if (x > 0 && x + w > width) { x = 0; } else if (x > 0) { ImGui::SameLine(); }
        bool isCurrent = i == current;
        if (isCurrent) { ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)); }
        ImGui::PushID(i);
        if (ImGui::Button(b[i].label, ImVec2(w, h))) { selectBand(i, cb); }
        ImGui::PopID();
        if (isCurrent) { ImGui::PopStyleColor(); }
        x += w + gap;
    }
}

static bool inside(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
    return p.x >= a.x && p.x < b.x && p.y >= a.y && p.y < b.y;
}

static std::string autoName(double frequency, int band, const std::vector<BookmarkInfo>& existing) {
    char buf[64];
    snprintf(buf, sizeof buf, "%.3f", frequency / 1e6);
    std::string base = buf;
    std::string name = base;
    for (int i = 2;; i++) {
        bool taken = false;
        for (auto& e : existing) { if (e.name == name) { taken = true; } }
        if (!taken) { break; }
        name = base + " " + std::to_string(i);
    }
    return name;
}

void BandPanel::refreshCache(TransientBookmarkManager* tbm, bool force) {
    double now = ImGui::GetTime();
    if (!force && cacheTime >= 0 && now - cacheTime < 0.3) { return; }
    cacheTime = now;
    cachedGroups = tbm->getListNames();
    items.clear();
    if (selectedGroup.empty()) {
        for (auto& g : cachedGroups) {
            for (auto& m : tbm->getListBookmarks(g)) { items.push_back(Item{ g, m }); }
        }
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.info.frequency < b.info.frequency; });
    } else {
        for (auto& m : tbm->getListBookmarks(selectedGroup)) { items.push_back(Item{ selectedGroup, m }); }
    }
}

static bool containsNoCase(const std::string& text, const char* needle) {
    if (!*needle) { return true; }
    std::string a = text, b = needle;
    std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return std::tolower(c); });
    std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return std::tolower(c); });
    return a.find(b) != std::string::npos;
}

void BandPanel::drawMarkers(const Callbacks& cb, float width, float height) {
    auto tbm = cb.bookmarks ? cb.bookmarks() : nullptr;
    if (!tbm) {
        ImGui::TextDisabled("Frequency manager is not available.");
        return;
    }
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float unit = ImGui::GetFontSize() * 2.0f;     // a finger sized row
    if (cacheTime < 0) { refreshCache(tbm, true); }
    if (cachedGroups.empty()) { tbm->addList("General"); refreshCache(tbm, true); }
    if (!selectedGroup.empty() && std::find(cachedGroups.begin(), cachedGroups.end(), selectedGroup) == cachedGroups.end()) {
        selectedGroup.clear();      // the group is gone: all of them
        selectedMarker.clear();
        refreshCache(tbm, true);
    }
    refreshCache(tbm, false);
    const bool all = selectedGroup.empty();

    ImGuiIO& io = ImGui::GetIO();
    // where the pointer is: a finger that went down on a drag handle, or the mouse
    bool anyFingerDown = false;
    int newFinger = -1;
    for (int i = 0; i < 10; i++) {
        if (io.FingerDown[i]) {
            anyFingerDown = true;
            if (!prevFingerDown[i] && newFinger < 0) { newFinger = i; }
        }
    }
    bool pointerDown = false;
    ImVec2 pointerPos;
    if (drag.active && drag.finger >= 0) {
        pointerDown = io.FingerDown[drag.finger];
        pointerPos = io.FingerPos[drag.finger];
    } else if (!anyFingerDown && ImGui::IsMouseDown(0)) {
        pointerDown = true;
        pointerPos = ImGui::GetMousePos();
    }
    if (drag.active && drag.finger < 0 && !anyFingerDown) {
        pointerDown = ImGui::IsMouseDown(0);
        pointerPos = ImGui::GetMousePos();
    }
    if (!drag.active) {
        // a drag starts on the handle of a row
        for (auto& h : handleRects) {
            bool started = false;
            int finger = -1;
            if (newFinger >= 0 && inside(io.FingerPos[newFinger], h.rect.min, h.rect.max)) { started = true; finger = newFinger; }
            else if (!anyFingerDown && ImGui::IsMouseClicked(0) && inside(ImGui::GetMousePos(), h.rect.min, h.rect.max)) { started = true; }
            if (started) {
                drag.active = true;
                drag.name = h.name;
                drag.fromGroup = h.group;
                drag.finger = finger;
                drag.pos = finger >= 0 ? io.FingerPos[finger] : ImGui::GetMousePos();
                pointerDown = true;
                pointerPos = drag.pos;
                break;
            }
        }
    }
    for (int i = 0; i < 10; i++) { prevFingerDown[i] = io.FingerDown[i]; }
    if (drag.active) {
        drag.pos = pointerDown ? pointerPos : drag.pos;
        ImGuiTouchScrollSuppress = true;
    }
    std::string dropTarget;
    if (drag.active) {
        for (auto& c : chipRects) {
            if (inside(drag.pos, c.second.min, c.second.max)) { dropTarget = c.first; }
        }
    }
    if (drag.active && !pointerDown) {
        // dropped on a group: moved when the markers of one group are shown, added to it (a copy) from the view of all
        if (!dropTarget.empty() && dropTarget != drag.fromGroup) {
            if (all) {
                for (auto& it : items) {
                    if (it.group == drag.fromGroup && it.info.name == drag.name) {
                        BookmarkInfo copy = it.info;
                        auto existing = tbm->getListBookmarks(dropTarget);
                        std::string base = copy.name;
                        for (int n = 2;; n++) {
                            bool taken = false;
                            for (auto& e : existing) { if (e.name == copy.name) { taken = true; } }
                            if (!taken) { break; }
                            copy.name = base + " " + std::to_string(n);
                        }
                        tbm->putBookmark(dropTarget, "", copy);
                        break;
                    }
                }
            } else {
                tbm->moveBookmark(drag.fromGroup, dropTarget, drag.name);
                selectedMarker.clear();
            }
            refreshCache(tbm, true);
        }
        drag.active = false;
        drag.finger = -1;
        ImGuiTouchScrollSuppress = false;
    }

    chipRects.clear();
    handleRects.clear();

    // ---- groups: a list with a search field, the group of all the markers on top
    const float leftWidth = width * 0.36f;
    ImGui::BeginChild("##groupcolumn", ImVec2(leftWidth, height), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetNextItemWidth(leftWidth);
    ImGui::InputTextWithHint("##groupfilter", "Search groups", groupFilter, sizeof groupFilter);
    ImGui::BeginChild("##grouplist", ImVec2(leftWidth, ImGui::GetContentRegionAvail().y), true, 0);
    ImDrawList* gdl = ImGui::GetWindowDrawList();
    auto drawGroupRow = [&](const std::string& id, const std::string& label, bool isAll, bool isAdd) {
        bool selected = !isAdd && ((isAll && selectedGroup.empty()) || (!isAll && id == selectedGroup));
        ImVec2 start = ImGui::GetCursorScreenPos();
        float rowWidth = ImGui::GetContentRegionAvail().x;
        ImGui::PushID(id.c_str());
        bool showPressed = false, rowPressed = false;
        float boxWidth = (isAll || isAdd) ? 0 : unit;
        if (boxWidth > 0) {
            ImGui::InvisibleButton("##show", ImVec2(boxWidth, unit));
            showPressed = ImGui::IsItemClicked(0);
            ImGui::SameLine(0, 0);
        }
        rowPressed = ImGui::Selectable("##row", false, 0, ImVec2(rowWidth - boxWidth, unit));
        ImGui::PopID();
        ImVec2 end(start.x + rowWidth, start.y + unit);
        bool droppingHere = drag.active && !isAll && !isAdd && dropTarget == id;
        ImU32 bg = droppingHere ? IM_COL32(60, 160, 80, 255) : selected ? IM_COL32(30, 130, 255, 255) : IM_COL32(45, 55, 80, 255);
        if (ImGuiFieldRadioSkin) {
            ImU32 key = droppingHere ? IM_COL32(60, 160, 80, 255) : selected ? IM_COL32(255, 106, 26, 255) : ImGui::GetColorU32(ImGuiCol_Button);
            ImGui::RenderSkinButton(start, end, key, selected, 3.0f);
        } else {
            gdl->AddRectFilled(start, end, bg, 5.0f);
            if (selected) { gdl->AddRect(start, end, IM_COL32(255, 255, 255, 255), 5.0f, 0, 2.5f); }
        }
        if (boxWidth > 0) {
            float m = unit * 0.28f;
            ImVec2 b0(start.x + m, start.y + m), b1(start.x + unit - m, start.y + unit - m);
            gdl->AddRect(b0, b1, IM_COL32(255, 255, 255, 220), 3.0f, 0, 2.0f);
            if (tbm->isListShown(id)) { gdl->AddRectFilled(ImVec2(b0.x + 4, b0.y + 4), ImVec2(b1.x - 4, b1.y - 4), IM_COL32(255, 255, 255, 240), 2.0f); }
        }
        ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        gdl->AddText(ImVec2(start.x + boxWidth + gap, start.y + (unit - ts.y) / 2), IM_COL32(255, 255, 255, 255), label.c_str());
        if (!isAll && !isAdd) { chipRects.push_back({ id, Rect{ start, end } }); }
        if (showPressed) { tbm->setListShown(id, !tbm->isListShown(id)); }
        return rowPressed;
    };
    if (drawGroupRow("", "All", true, false)) {
        selectedGroup.clear();
        selectedMarker.clear();
        refreshCache(tbm, true);
    }
    for (auto& g : cachedGroups) {
        if (!containsNoCase(g, groupFilter)) { continue; }
        if (drawGroupRow(g, g, false, false)) {
            if (g == selectedGroup) {
                // a second tap on the chosen group opens its settings
                openGroupDialog = true;
                groupDialogRename = true;
                snprintf(groupName, sizeof groupName, "%s", g.c_str());
            }
            selectedGroup = g;
            selectedMarker.clear();
            refreshCache(tbm, true);
        }
    }
    if (drawGroupRow("#new-group", "+ New group", false, true)) {
        openGroupDialog = true;
        groupDialogRename = false;
        groupName[0] = 0;
    }
    ImGui::EndChild();
    ImGui::EndChild();

    // ---- markers
    ImGui::SameLine();
    const float rightWidth = width - leftWidth - gap;
    ImGui::BeginChild("##markercolumn", ImVec2(rightWidth, height), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    float buttonsHeight = unit;
    float listHeight = height - buttonsHeight - gap;
    ImGui::BeginChild("##markerlist", ImVec2(rightWidth, listHeight), true, 0);
    double now = cb.getFrequency();
    for (auto& it : items) {
        auto& m = it.info;
        ImGui::PushID((it.group + "/" + m.name).c_str());
        bool selected = m.name == selectedMarker && it.group == selectedMarkerGroup;
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float rowWidth = ImGui::GetContentRegionAvail().x;
        float handleWidth = unit * 1.1f;
        if (ImGui::Selectable("##row", selected, 0, ImVec2(rowWidth - handleWidth, unit))) {
            selectedMarker = m.name;
            selectedMarkerGroup = it.group;
            cb.tuneTo(m.demodId, m.frequency, m.bandwidth);
        }
        ImGui::SameLine(0, 0);
        ImGui::InvisibleButton("##handle", ImVec2(handleWidth, unit));
        ImVec2 h0(pos.x + rowWidth - handleWidth, pos.y), h1(pos.x + rowWidth, pos.y + unit);
        handleRects.push_back(HandleRect{ it.group, m.name, Rect{ h0, h1 } });
        float ty = pos.y + (unit - ImGui::GetFontSize()) / 2;
        bool atMarker = fabs(m.frequency - now) < 1.0;
        ImU32 col = atMarker ? IM_COL32(120, 255, 160, 255) : IM_COL32(235, 235, 235, 255);
        ImDrawList* ldl = ImGui::GetWindowDrawList();
        ldl->AddText(ImVec2(pos.x + gap, ty), col, formatFrequency(m.frequency).c_str());
        ldl->AddText(ImVec2(pos.x + rowWidth * 0.36f, ty), col, modeName(m.demodId));
        ldl->AddText(ImVec2(pos.x + rowWidth * 0.52f, ty), col, m.name.c_str());
        if (all) {
            // in the view of all the groups the group of the marker is shown, small and dim
            ImVec2 gs = ImGui::CalcTextSize(it.group.c_str());
            ldl->AddText(ImVec2(pos.x + rowWidth - handleWidth - gs.x - 4, ty), IM_COL32(150, 160, 185, 255), it.group.c_str());
        }
        float cx = (h0.x + h1.x) / 2, cy = (h0.y + h1.y) / 2;
        for (int k = -1; k <= 1; k++) {
            ldl->AddRectFilled(ImVec2(cx - unit * 0.28f, cy + k * unit * 0.2f - 2), ImVec2(cx + unit * 0.28f, cy + k * unit * 0.2f + 2), IM_COL32(200, 200, 200, 255), 2.0f);
        }
        ImGui::PopID();
    }
    if (items.empty()) {
        ImGui::TextDisabled(all ? "Nothing saved yet." : "Empty. Tune and press Save.");
    }
    ImGui::EndChild();

    // ---- actions
    float bw = (rightWidth - gap * 3) / 4;
    bool haveSelection = !selectedMarker.empty() && !selectedMarkerGroup.empty();
    if (ImGui::Button("Save", ImVec2(bw, buttonsHeight))) {
        // into the chosen group; the view of all the groups saves into General
        std::string target = all ? "General" : selectedGroup;
        if (all && std::find(cachedGroups.begin(), cachedGroups.end(), target) == cachedGroups.end()) { tbm->addList(target); }
        BookmarkInfo info;
        info.frequency = std::round(cb.getFrequency());
        info.demodId = cb.getMode();
        info.bandwidth = cb.getBandwidth();
        info.name = autoName(info.frequency, bandOf(info.frequency), tbm->getListBookmarks(target));
        if (tbm->putBookmark(target, "", info)) {
            selectedMarker = info.name;
            selectedMarkerGroup = target;
        }
        refreshCache(tbm, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Update", ImVec2(bw, buttonsHeight)) && haveSelection) {
        BookmarkInfo info;
        info.name = selectedMarker;
        info.frequency = std::round(cb.getFrequency());
        info.demodId = cb.getMode();
        info.bandwidth = cb.getBandwidth();
        tbm->putBookmark(selectedMarkerGroup, selectedMarker, info);
        refreshCache(tbm, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Edit", ImVec2(bw, buttonsHeight)) && haveSelection) {
        for (auto& it : items) {
            if (it.info.name == selectedMarker && it.group == selectedMarkerGroup) {
                openEdit = true;
                editOldName = it.info.name;
                editGroup = it.group;
                snprintf(editName, sizeof editName, "%s", it.info.name.c_str());
                editFrequencyKHz = it.info.frequency / 1000;
                editBandwidthKHz = it.info.bandwidth / 1000;
                editMode = it.info.demodId;
                editGroupIndex = (int)(std::find(cachedGroups.begin(), cachedGroups.end(), it.group) - cachedGroups.begin());
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete", ImVec2(bw, buttonsHeight)) && haveSelection) {
        tbm->removeBookmark(selectedMarkerGroup, selectedMarker);
        selectedMarker.clear();
        refreshCache(tbm, true);
    }
    ImGui::EndChild();

    // the item being dragged follows the pointer
    if (drag.active) {
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        ImVec2 ts = ImGui::CalcTextSize(drag.name.c_str());
        ImVec2 a(drag.pos.x - ts.x / 2 - 10, drag.pos.y - unit - ts.y / 2 - 6), b(drag.pos.x + ts.x / 2 + 10, drag.pos.y - unit + ts.y / 2 + 6);
        fg->AddRectFilled(a, b, ImGuiFieldRadioSkin ? IM_COL32(255, 106, 26, 235) : IM_COL32(40, 120, 220, 230), 6.0f);
        fg->AddText(ImVec2(a.x + 10, a.y + 6), IM_COL32(255, 255, 255, 255), drag.name.c_str());
    }
}

void BandPanel::drawEditPopup(const Callbacks& cb) {
    auto tbm = cb.bookmarks ? cb.bookmarks() : nullptr;
    if (!tbm) { return; }
    if (openEdit) { ImGui::OpenPopup("Edit marker"); openEdit = false; }
    if (openGroupDialog) { ImGui::OpenPopup("Group"); openGroupDialog = false; }

    ImVec2 screen = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(screen.x / 2, screen.y * 0.35f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Edit marker", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto groups = tbm->getListNames();
        const float w = std::min(screen.x * 0.6f, ImGui::GetFontSize() * 30);
        ImGui::SetNextItemWidth(w);
        ImGui::InputText("Name", editName, sizeof editName);
        ImGui::SetNextItemWidth(w);
        ImGui::InputDouble("Frequency (kHz)", &editFrequencyKHz, 0, 0, "%.3f");
        ImGui::SetNextItemWidth(w);
        ImGui::InputDouble("Bandwidth (kHz)", &editBandwidthKHz, 0, 0, "%.3f");
        ImGui::SetNextItemWidth(w);
        static const char* modes = "NFM\0WFM\0AM\0DSB\0USB\0CW\0LSB\0RAW\0";
        ImGui::Combo("Mode", &editMode, modes);
        std::string groupList;
        for (auto& g : groups) { groupList += g; groupList += '\0'; }
        ImGui::SetNextItemWidth(w);
        ImGui::Combo("Group", &editGroupIndex, groupList.c_str());
        const float bh = ImGui::GetFontSize() * 2.2f;
        if (ImGui::Button("OK", ImVec2(w / 3 - 4, bh))) {
            BookmarkInfo info;
            info.name = editName;
            info.frequency = editFrequencyKHz * 1000;
            info.bandwidth = editBandwidthKHz * 1000;
            info.demodId = editMode;
            if (tbm->putBookmark(editGroup, editOldName, info)) {
                std::string target = editGroupIndex >= 0 && editGroupIndex < (int)groups.size() ? groups[editGroupIndex] : editGroup;
                if (target != editGroup) { tbm->moveBookmark(editGroup, target, info.name); }
                selectedMarker = info.name;
                selectedMarkerGroup = target;
                cacheTime = -1;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(w / 3 - 4, bh))) { ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Delete", ImVec2(w / 3 - 4, bh))) {
            tbm->removeBookmark(editGroup, editOldName);
            selectedMarker.clear();
            cacheTime = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(ImVec2(screen.x / 2, screen.y * 0.35f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Group", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const float w = std::min(screen.x * 0.5f, ImGui::GetFontSize() * 24);
        ImGui::TextUnformatted(groupDialogRename ? "Rename or delete the group" : "New group");
        ImGui::SetNextItemWidth(w);
        ImGui::InputText("Name", groupName, sizeof groupName);
        const float bh = ImGui::GetFontSize() * 2.2f;
        float bw = groupDialogRename ? w / 3 - 4 : w / 2 - 4;
        if (ImGui::Button(groupDialogRename ? "Rename" : "Create", ImVec2(bw, bh))) {
            bool ok = groupDialogRename ? tbm->renameList(selectedGroup, groupName) : tbm->addList(groupName);
            if (ok) {
                selectedGroup = groupName;
                selectedMarker.clear();
                cacheTime = -1;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(bw, bh))) { ImGui::CloseCurrentPopup(); }
        if (groupDialogRename) {
            ImGui::SameLine();
            if (ImGui::Button("Delete", ImVec2(bw, bh))) {
                tbm->removeList(selectedGroup);
                selectedGroup.clear();
                selectedMarker.clear();
                cacheTime = -1;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

void BandPanel::draw(const Callbacks& cb) {
    if (!loaded) { loadBands(); }
    auto avail = ImGui::GetContentRegionAvail();
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    float leftWidth = (avail.x - gap) * 0.30f;
    ImGui::BeginChild("##bands", ImVec2(leftWidth, avail.y), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawBands(cb, leftWidth, avail.y - ImGui::GetStyle().WindowPadding.y);
    ImGui::EndChild();
    ImGui::SameLine();
    float rightWidth = avail.x - leftWidth - gap;
    ImGui::BeginChild("##markers", ImVec2(rightWidth, avail.y), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawMarkers(cb, rightWidth, avail.y - ImGui::GetStyle().WindowPadding.y);
    ImGui::EndChild();
    drawEditPopup(cb);
}
