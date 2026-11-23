/* AIML — Vocab (i18n) Runtime
 *
 * Zero-friction translation feature.  Two JSON assets carry the data:
 *
 *   languages.json  — index → language code:
 *       {"0": "en", "1": "de", "2": "fr"}
 *
 *   vocab.json      — source string → list of translations; the index in the
 *                     list corresponds to the language index above:
 *       {"Save Card": ["Save Card", "Karte speichern", "Sauvegarder"],
 *        "Menu":      ["Menu",      "Menü",             "Menu"]}
 *
 * Missing-translation policy: silent fallback to the source string
 * (which stays English if no translation exists).
 *
 * Interception points:
 *   1. push_text_batch() calls aiml_vocab_lookup() as a preamble — covers
 *      all prefab YAML labels and set_text constants.
 *   2. Functions with `decorator: [vocab]` wrap every `return <string>`
 *      in an aiml_vocab_lookup call via a compiler wrap — for
 *      formatter fragments that must be translated before concatenation.
 *
 * All strings that aiml_vocab_lookup returns belong to the runtime;
 * callers must NOT modify or free them.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Loads both JSON streams (zero-terminated).  Return: 0 = OK,
 * -1 = languages JSON invalid, -2 = vocab JSON invalid.  A second
 * call replaces the previous data (hot-reload friendly).            */
int32_t     aiml_vocab_load(const char* languages_json, const char* vocab_json);

/* Sets the active language to the given index.  An index outside
 * [0, aiml_vocab_language_count) is clamped to 0.  No-op if
 * no JSON has been loaded yet.                                       */
void        aiml_vocab_set_language(int32_t idx);

/* Returns the current language index (0 = default = source language). */
int32_t     aiml_vocab_current_language(void);

/* Number of loaded languages (for UI dropdowns).                      */
int32_t     aiml_vocab_language_count(void);

/* Language code for the given index (e.g. "de").  NULL if the
 * index is out of range or nothing has been loaded yet.               */
const char* aiml_vocab_language_code(int32_t idx);

/* The central lookup.  Return: the translation if in the vocab, otherwise
 * the source argument unchanged (silent fallback).  Never NULL as long as
 * `source` is not NULL.                                               */
const char* aiml_vocab_lookup(const char* source);

#ifdef __cplusplus
}
#endif
