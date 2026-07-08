#pragma once
#include <optional>
#include <string>
namespace rtg {
// Reference song-FORM steering (analysis-derived). When present, the arrangement
// builder uses these section lengths (in BARS) + drop count instead of its
// style/energy menus + hill-climb. 0 fields => keep the engine's own value.
// Populated by tools/earview/structure.py --struct-profile-out. Mirrors
// DrumProfile: setActive() before generation, active() read-only during.
struct StructureProfile {
    bool present   = false;
    int  introBars = 0;   // 0 = unset
    int  buildBars = 0;
    int  dropBars  = 0;   // applied to every drop
    int  breakBars = 0;   // applied to every break
    int  outroBars = 0;
    int  dropCount = 0;   // 0 = keep params.dropCount
    static void setActive(std::optional<StructureProfile> p);
    static const StructureProfile& active();      // returns a present=false default when unset
    bool loadFromFile(const std::string& path);   // tolerant flat-JSON; false only on I/O failure
};
} // namespace rtg
