#pragma once

#include <string>
#include "config.h"

struct FrequencyBookmark {
    double frequency;
    double bandwidth;
    int modeIndex;
    bool selected;
    std::string vfoName; // VFO/radio this bookmark belongs to; empty = legacy (apply to currently selected VFO)
};

struct WaterfallBookmark {
    std::string listName;
    std::string bookmarkName;
    std::string extraInfo;
    bool worked;
    FrequencyBookmark bookmark;
    long long notValidAfter;
};

// One saved frequency of a list, as the touch panel sees it. The modulation is the radio module's number
// (0 NFM, 1 WFM, 2 AM, 3 DSB, 4 USB, 5 CW, 6 LSB, 7 RAW).
struct BookmarkInfo {
    std::string name;
    double frequency = 0;
    double bandwidth = 0;
    int demodId = 0;
};

struct TransientBookmarkManager {
    std::vector<WaterfallBookmark> transientBookmarks;

    virtual void refreshWaterfallBookmarks(bool lockConfig = true) = 0;
    virtual const char *getModesList() = 0;

    // Lists (groups) of bookmarks. A list that is shown has its bookmarks drawn on the waterfall.
    virtual std::vector<std::string> getListNames() = 0;
    virtual bool isListShown(const std::string& list) = 0;
    virtual void setListShown(const std::string& list, bool shown) = 0;
    virtual bool addList(const std::string& list) = 0;
    virtual bool renameList(const std::string& from, const std::string& to) = 0;
    virtual void removeList(const std::string& list) = 0;
    virtual std::vector<BookmarkInfo> getListBookmarks(const std::string& list) = 0;
    // Adds a bookmark, or replaces the one called oldName (the name may change); returns false if the name is taken.
    virtual bool putBookmark(const std::string& list, const std::string& oldName, const BookmarkInfo& bookmark) = 0;
    virtual void removeBookmark(const std::string& list, const std::string& name) = 0;
    virtual bool moveBookmark(const std::string& from, const std::string& to, const std::string& name) = 0;
};

void applyBookmark(FrequencyBookmark bm, std::string vfoName);
ConfigManager &getFrequencyManagerConfig();