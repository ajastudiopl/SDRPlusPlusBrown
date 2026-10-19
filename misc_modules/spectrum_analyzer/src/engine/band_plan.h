#pragma once
#include <vector>

namespace spectrum {
    // Frequently used ranges for the "Bands" quick list. Amateur bands follow
    // the IARU Region 1 / Polish allocations; the list is a convenience, not a
    // regulatory reference.
    struct BandEntry {
        const char* group;
        const char* name;
        double startHz;
        double stopHz;
    };

    const std::vector<BandEntry>& bandPlan();
    // Group names in display order.
    std::vector<const char*> bandGroups();
}
