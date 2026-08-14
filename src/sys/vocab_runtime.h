/* AIML — Vocab (i18n) Runtime
 *
 * Zero-friction Übersetzungs-Feature.  Zwei JSON-Assets tragen die Daten:
 *
 *   languages.json  — Index → Sprach-Code:
 *       {"0": "en", "1": "de", "2": "fr"}
 *
 *   vocab.json      — Source-String → Liste von Übersetzungen; Index in der
 *                     Liste korrespondiert mit dem Sprach-Index oben:
 *       {"Save Card": ["Save Card", "Karte speichern", "Sauvegarder"],
 *        "Menu":      ["Menu",      "Menü",             "Menu"]}
 *
 * Missing-Translation-Policy: silent fallback auf den Source-String
 * (der bleibt english, wenn keine Übersetzung existiert).
 *
 * Interception-Punkte:
 *   1. push_text_batch() ruft aiml_vocab_lookup() als Präambel auf — deckt
 *      alle Prefab-YAML-Labels und set_text-Konstanten ab.
 *   2. Funktionen mit `decorator: [vocab]` wickeln jeden `return <string>`
 *      per Compiler-Wrap in einen aiml_vocab_lookup-Aufruf ein — für
 *      Formatter-Fragmente, die vor Konkatenation übersetzt werden müssen.
 *
 * Alle Strings, die aiml_vocab_lookup zurückgibt, gehören dem Runtime;
 * Aufrufer dürfen sie NICHT modifizieren oder freigeben.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Lädt beide JSON-Streams (Zero-terminated).  Rückgabe: 0 = OK,
 * -1 = languages-JSON invalid, -2 = vocab-JSON invalid.  Ein zweiter
 * Aufruf ersetzt die vorherigen Daten (Hot-Reload-freundlich).       */
int32_t     aiml_vocab_load(const char* languages_json, const char* vocab_json);

/* Setzt die aktive Sprache auf den gegebenen Index.  Index außerhalb
 * [0, aiml_vocab_language_count) wird geclampt auf 0.  Kein-op wenn
 * noch keine JSON geladen ist.                                       */
void        aiml_vocab_set_language(int32_t idx);

/* Gibt den aktuellen Sprach-Index zurück (0 = default = Source-Sprache). */
int32_t     aiml_vocab_current_language(void);

/* Anzahl geladener Sprachen (für UI-Dropdowns).                       */
int32_t     aiml_vocab_language_count(void);

/* Sprach-Code für den gegebenen Index (z.B. "de").  NULL falls
 * Index out-of-range oder noch nichts geladen wurde.                  */
const char* aiml_vocab_language_code(int32_t idx);

/* Der zentrale Lookup.  Rückgabe: Übersetzung wenn im vocab, sonst
 * das Source-Argument unverändert (silent fallback).  Nie NULL solange
 * `source` nicht NULL ist.                                            */
const char* aiml_vocab_lookup(const char* source);

#ifdef __cplusplus
}
#endif
