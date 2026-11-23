#pragma once
#include <string>
#include <vector>

// Tier 4, input reachability (spec/44_8).  The check runs as a
// WHOLE-PROGRAM pass like Tier 3 — a prefab is declared in one file
// and often operated in another, so a per-module check would be a
// false positive.  That is why these structures are in a header of their own: the
// context (per module) AND the compiler (whole-program) need them, and
// context.h cannot be included in compiler.h (cycle).
namespace aiml {
namespace prefab {

struct ChildMeta {
    std::string loc;      // "file:line:column: " of the CHILD (spec/49 F49-100)
    std::string name;
    std::string type;
    float pos_x = 0, pos_y = 0, width = 0, height = 0;
    bool visible = true;
};

struct Meta {
    std::string loc;      // location of the prefab declaration
    float width = 0, height = 0;
    bool inputTransparent = false;
    bool draggable = false;
    std::vector<ChildMeta> children;
};

}  // namespace prefab
}  // namespace aiml
