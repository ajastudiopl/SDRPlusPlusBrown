#pragma once
#include <string>
#include <cstdio>
#include <algorithm>
#include <json.hpp>

#if defined(__ANDROID__)
#include "../../../core/backends/android/android_backend.h"
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IOS
std::string iosHttpGet(const std::string& url);   // NSURLSession, in the iOS backend
#endif
#endif

namespace markerbackup {
    // A Google Drive share link ("file/d/<id>/view" or "open?id=<id>") becomes the direct download address
    inline std::string normalizeShareUrl(std::string url) {
        auto trim = [](std::string& s) {
            while (!s.empty() && isspace((unsigned char)s.back())) { s.pop_back(); }
            size_t i = 0;
            while (i < s.size() && isspace((unsigned char)s[i])) { i++; }
            s.erase(0, i);
        };
        trim(url);
        if (url.find("drive.google.com") != std::string::npos || url.find("docs.google.com") != std::string::npos) {
            std::string id;
            size_t p = url.find("/d/");
            if (p != std::string::npos) {
                p += 3;
                size_t e = url.find_first_of("/?&#", p);
                id = url.substr(p, e == std::string::npos ? std::string::npos : e - p);
            } else if ((p = url.find("id=")) != std::string::npos) {
                p += 3;
                size_t e = url.find_first_of("&#", p);
                id = url.substr(p, e == std::string::npos ? std::string::npos : e - p);
            }
            if (!id.empty()) { return "https://drive.google.com/uc?export=download&id=" + id; }
        }
        return url;
    }

    // Plain HTTP(S) GET of a small text file; returns false and a message in `text` on failure
    inline bool fetchText(const std::string& url, std::string& text) {
        if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) { text = "The link has to start with http:// or https://"; return false; }
#if defined(__ANDROID__)
        text = ::backend::httpGet(url);
        if (text.rfind("#ERROR", 0) == 0) { return false; }
        return true;
#elif defined(__APPLE__) && TARGET_OS_IOS
        text = iosHttpGet(url);
        if (text.rfind("#ERROR", 0) == 0) { return false; }
        return true;
#elif defined(_WIN32)
        text = "Importing from a link is not available on Windows yet, import from the file instead";
        return false;
#else
        if (url.find('\'') != std::string::npos) { text = "Invalid character in the link"; return false; }
        std::string cmd = "curl -sL --max-time 20 --max-filesize 5000000 '" + url + "' 2>&1";
        FILE* p = popen(cmd.c_str(), "r");
        if (!p) { text = "curl is not available"; return false; }
        std::string out;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, p)) > 0) { out.append(buf, n); if (out.size() > 5000000) { break; } }
        int rc = pclose(p);
        if (rc != 0 || out.empty()) { text = out.empty() ? "No answer from the server" : out.substr(0, 200); return false; }
        text = out;
        return true;
#endif
    }

    // Checks the document and returns the lists it holds; empty string = ok, otherwise the reason
    inline std::string parse(const std::string& text, nlohmann::json& lists) {
        nlohmann::json doc;
        try { doc = nlohmann::json::parse(text); }
        catch (...) { return "The text is not a valid markers file (JSON expected)"; }
        if (!doc.is_object() || !doc.contains("lists") || !doc["lists"].is_object()) { return "No marker lists found in the file"; }
        for (auto& [listName, list] : doc["lists"].items()) {
            if (!list.is_object() || !list.contains("bookmarks") || !list["bookmarks"].is_object()) { return "List '" + listName + "' has no bookmarks section"; }
            for (auto& [bmName, bm] : list["bookmarks"].items()) {
                if (!bm.is_object() || !bm.contains("frequency") || !bm["frequency"].is_number()) { return "Marker '" + bmName + "' has no frequency"; }
            }
        }
        lists = doc["lists"];
        return "";
    }
}
