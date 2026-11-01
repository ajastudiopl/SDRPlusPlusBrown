#pragma once
#include <functional>
#include <string>
#include <vector>
#include <imgui.h>
#include "../../../misc_modules/frequency_manager/src/frequency_manager.h"

struct TransientBookmarkManager;

// Band buttons and the lists of saved frequencies for the bottom bar of the SSB TRX view. A band button jumps to the last
// frequency used on that band and switches to the modulation that is used there. The saved frequencies are the bookmarks
// of the frequency manager, so a list that is shown here is also drawn on the waterfall.
// Modulations are numbered like in the radio module: 0 NFM, 1 WFM, 2 AM, 3 DSB, 4 USB, 5 CW, 6 LSB, 7 RAW.
struct BandPanel {
    struct Callbacks {
        std::function<double()> getFrequency;       // what the radio is tuned to now, in Hz
        std::function<int()> getMode;
        std::function<double()> getBandwidth;
        std::function<void(int mode, double frequency, double bandwidth)> tuneTo;     // switch the modulation and tune; bandwidth 0 leaves it
        std::function<TransientBookmarkManager*()> bookmarks;
        std::function<void()> openFullMenu;       // the Mode menu, which has all the bands
    };

    void draw(const Callbacks& cb);
    // every band, for the Mode menu
    void drawAllBands(const Callbacks& cb, float width);

private:
    struct Band {
        const char* label;
        double low, high;       // Hz
        double defaultFrequency;
        int defaultMode;
    };
    static const std::vector<Band>& bands();
    int bandOf(double frequency);
    void selectBand(int index, const Callbacks& cb);
    void drawBands(const Callbacks& cb, float width, float height);
    void drawMarkers(const Callbacks& cb, float width, float height);
    void drawEditPopup(const Callbacks& cb);
    void loadBands();
    void saveBands();

    bool loaded = false;
    int bandLayer = 0;      // the panel shows nine buttons at a time, one of them switches to the other nine
    struct Last { double frequency; int mode; };
    std::vector<Last> lastOnBand;

    // markers
    std::string selectedGroup;
    std::string selectedMarker;
    struct Rect { ImVec2 min, max; };
    std::vector<std::pair<std::string, Rect>> chipRects;       // where the group rows are on screen, for dropping
    struct HandleRect { std::string group, name; Rect rect; };
    std::vector<HandleRect> handleRects;                       // where the drag handles of the rows are
    std::string selectedMarkerGroup;                           // the list of the chosen marker; the view of all groups mixes them
    char groupFilter[48] = {};
    // what is shown is refreshed a few times a second, not every frame
    struct Item { std::string group; BookmarkInfo info; };
    std::vector<Item> items;
    std::vector<std::string> cachedGroups;
    double cacheTime = -1;
    void refreshCache(TransientBookmarkManager* tbm, bool force);
    struct Drag {
        bool active = false;
        std::string name;
        std::string fromGroup;
        int finger = -1;
        ImVec2 pos;
    } drag;
    bool prevFingerDown[10] = {};
    // edit dialog
    bool openEdit = false;
    std::string editOldName, editGroup;
    char editName[96] = {};
    double editFrequencyKHz = 0, editBandwidthKHz = 0;
    int editMode = 0;
    int editGroupIndex = 0;
    bool openGroupDialog = false;
    bool groupDialogRename = false;
    char groupName[64] = {};
};
