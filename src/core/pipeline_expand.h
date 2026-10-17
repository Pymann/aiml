#ifndef AIML_PIPELINE_EXPAND_H
#define AIML_PIPELINE_EXPAND_H

// spec/62 §13.5f — die deklarierte Pipeline-Form in die zusammengesetzte
// umschreiben, VOR dem Uebersetzen.  Damit gibt es nur einen Ausfuehrer
// (F62-23) und keine zwei, die auseinander driften (§16 Punkt 4).
#include <string>

namespace aiml {

// Enthaelt die Quelle eine `- pipeline:`-Deklaration?  Nur dann wird
// umgeschrieben — jede andere Datei laeuft unveraendert durch den gewohnten
// Weg, damit die Umschreibung keine Datei anfasst, die sie nichts angeht.
bool sourceHasPipelineDecl(const std::string& text);

// Schreibt um.  `out` bekommt den erweiterten Quelltext.  Gibt false zurueck,
// wenn etwas abzulehnen war (die Diagnose steht dann im Log) oder nichts
// umzuschreiben ist.
bool expandPipelineDeclarations(const std::string& text, const std::string& where,
                                std::string& out);

} // namespace aiml
#endif
