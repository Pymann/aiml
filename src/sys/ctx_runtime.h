// spec/29_2 §14a — `ctx`: der Kontext als eigener Namensraum.
//
// Ein Kontext ist eine lebende Compiler-Instanz: man gibt ihr Bundles, sie
// behaelt, was sie uebersetzt hat, und spaetere Bundles sehen es.  Der Grund
// ist die Grundlast — ein frischer Kontext kostet ~96 ms, unabhaengig davon,
// wie wenig er tut (§15).
//
// WICHTIG: das ist KEINE Sicherheitsgrenze (F29_2-…e).  Prozess, Adressraum
// und dynamische Symboltabelle sind geteilt; fuer fremden Code bleibt die
// Grenze ein eigener Prozess (spec/65 F65-15).
#ifndef AIML_CTX_RUNTIME_H
#define AIML_CTX_RUNTIME_H

#include <cstdint>

extern "C" {

// Einen Kontext oeffnen.  Rueckgabe ist der Griff; nullptr = ging nicht.
void*   aiml_ctx_new(void);

// Freigeben.  Danach sind alle Adressen aus diesem Kontext ungueltig.
void    aiml_ctx_free(void* handle);

// Die Compiler-Instanz hinter einem Griff.  Der Griff selbst ist ein Eintrag
// der Tabelle, NICHT der Compiler — wer das verwechselt, ruft Methoden auf
// einem fremden Objekt (gemessen: Absturz im ersten Lauf).
void*   aiml_ctx_compiler(void* handle);

// Ein Bundle im Kontext ausfuehren (implementiert in exec_impl.cpp, wo der
// Weg vom Code-Objekt zum Quelltext schon steht).
int32_t aiml_ctx_exec_code(void* handle, void* code_obj);
// Uebersetzen und einhaengen, OHNE zu laufen (F29_2-…n).
int32_t aiml_ctx_load_code(void* handle, void* code_obj);

// Nach einem Lauf: die Typen des Bundles in die Tabelle des Kontexts
// schreiben.  `exec_impl.cpp` ruft es direkt nach `execInContext` — ohne das
// kennt `aiml_ctx_symbol` nur, was das LETZTE Bundle uebersetzt hat.
void    aiml_ctx_record_symbols(void* handle);

// Die Namen, die dieser Kontext mit genau diesem Typ bzw. dieser Signatur
// traegt (F29_2-…i), und die Meldungen des letzten Laufs (F29_2-…l).  Beide
// fuellen eine aiml-Liste, die der Aufrufer angelegt hat.
int32_t aiml_ctx_names_fill(void* handle, const char* want_type, void* list_ptr);
int32_t aiml_ctx_errors_fill(void* handle, void* list_ptr);

// Ein Symbol abgreifen — MIT genanntem Typ (F29_2-…d).
//   0 = gefunden, `out_addr` traegt Adresse bzw. Wert
//   1 = Name unbekannt            → SymbolMissingException
//   2 = Name da, Typ passt nicht  → SymbolTypeException
// `out_msg` zeigt danach auf die fertige Begruendung (statischer Puffer,
// gueltig bis zum naechsten Aufruf): sie nennt BEIDE Typen, erwartet und
// gefunden — ein „Typfehler" ohne die zwei Namen zwingt zum Suchen.
int32_t aiml_ctx_symbol(void* handle, const char* name, const char* want_type,
                        int64_t* out_addr, const char** out_msg);

}  // extern "C"
#endif
