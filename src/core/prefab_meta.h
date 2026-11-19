#pragma once
#include <string>
#include <vector>

// Tier 4, Eingabe-Erreichbarkeit (spec/44_8).  Die Pruefung laeuft als
// GANZPROGRAMM-Durchgang wie Tier 3 — ein Prefab wird in einer Datei deklariert
// und oft in einer anderen bedient, eine Pruefung je Modul waere also
// falsch-positiv.  Darum stehen diese Strukturen in einem eigenen Kopf: der
// Kontext (je Modul) UND der Compiler (ganzprogrammweit) brauchen sie, und
// context.h ist in compiler.h nicht einbindbar (Zyklus).
namespace aiml {
namespace prefab {

struct ChildMeta {
    std::string loc;      // "datei:zeile:spalte: " des KINDES (spec/49 F49-100)
    std::string name;
    std::string type;
    float pos_x = 0, pos_y = 0, width = 0, height = 0;
    bool visible = true;
};

struct Meta {
    std::string loc;      // Fundstelle der Prefab-Deklaration
    float width = 0, height = 0;
    bool inputTransparent = false;
    bool draggable = false;
    std::vector<ChildMeta> children;
};

}  // namespace prefab
}  // namespace aiml
