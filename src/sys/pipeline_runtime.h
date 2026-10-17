#ifndef AIML_PIPELINE_RUNTIME_H
#define AIML_PIPELINE_RUNTIME_H

#include <cstdint>

// spec/62 §13.5: pipeline / stage / step als zusammensetzbare Werte.
//
// Der RUMPF eines Schritts ist ein Code-Bundle (F62-85) — also uebersetzter,
// geprueefter Quelltext mit Inhaltsadresse.  Zusammengesetzt wird nur die
// STRUKTUR: welche Stufen, welche Achsen, welche Bindungen.  Deshalb bleiben
// Reinheitspruefung (am Rumpf) und inhaltsabgeleiteter Schluessel erhalten.
extern "C" {

void*   aiml_step_new(void);
void    aiml_step_set_name(void* step, const char* name);
void    aiml_step_set_body(void* step, void* code);       // Code*
void    aiml_step_set_cache(void* step, int32_t cls);
void    aiml_step_use_arg(void* step, const char* axis);
// spec/62 §9: die Sonde.  Sie laeuft VOR der Schluesselbildung, JEDES Mal,
// und ihr Ergebnis geht in den Schluessel — so wird aus einem beweglichen
// Namen (Branch, Paketversion) eine feste Adresse.
typedef const char* (*AimlResolveFn)(void);
void    aiml_step_set_resolve(void* step, void* fn);
// spec/62 §5.6: Reichweite eines Fehlschlags, Zeitgrenze, Versuche.  Keines
// der drei ist Schluesselmaterial (F62-91) — alle beschreiben den FEHLSCHLAG,
// aufgezeichnet wird nur ein gelungener Versuch.
void    aiml_step_set_on_fail(void* step, int32_t reach);   // 0=lane 1=pipeline 2=continue
void    aiml_step_set_timeout(void* step, int32_t ms);      // 0 = keine Grenze
void    aiml_step_set_retry(void* step, int32_t tries);
int32_t aiml_step_status(void* step);
// spec/62 §5.9: deklariertes Tor (F62-54).  Kein Schluesselmaterial (F62-96).
void    aiml_step_set_pause_before(void* step, int32_t on);
// spec/62 §5.5: `per: lane` (0, Vorgabe) oder `per: once` (1).  Kein
// Schluesselmaterial, begrenzt aber was im Schluessel stehen darf (F62-115).
void    aiml_step_set_per(void* step, int32_t per);
// spec/62 §5.1/F62-9: nur fruehere Schritte.  Schluesselmaterial ueber
// input_keys (F62-114) — Reihenfolge und Fehlerreichweite, kein Datenfluss
// (F62-113).
void    aiml_step_add_need(void* step, const char* name);

// spec/62 §5.7 / F62-90: der Rumpf setzt seinen Ausgang selbst.  Wirkung nur
// bei laufendem Schritt (thread-lokal, Lanes duerfen in Threads laufen).
void    aiml_step_status_set(int32_t status);
void    aiml_step_fail_lane(const char* msg);
void    aiml_step_fail_pipeline(const char* msg);
// spec/62 §13.5e: wie ein Rumpf seine Lane sieht.  Aus demselben thread-
// lokalen Platz wie der Status — ein Bundle traegt keinen Zustand des
// Aufrufers (F62-89).  Rueckgaben sind refcounted aiml-Strings.
char*   aiml_step_arg(const char* axis);
char*   aiml_step_workspace(void);
char*   aiml_step_host(const char* name);
// spec/62 §13.5h: Ergebniskanal.  Veroeffentlichen und lesen; gelesen werden
// darf nur ein DEKLARIERTES need (F62-119).
void    aiml_step_set_result(const char* value);
char*   aiml_step_need_result(const char* name);

void*   aiml_stage_new(void);
void    aiml_stage_set_name(void* stage, const char* name);
void    aiml_stage_set_runs_on(void* stage, const char* label);
void    aiml_stage_add_step(void* stage, void* step);

void*   aiml_pipeline_new(void);
void    aiml_pipeline_set_name(void* pipe, const char* name);
void    aiml_pipeline_add_param(void* pipe, const char* axis, const void* values); // list<string>
void    aiml_pipeline_add_stage(void* pipe, void* stage);
// spec/62 §4.2: Teilbindung als Text, "achse=wert[,achse=wert...]".
// Textform, weil eine Teilbindung dasselbe Format hat wie die `bindings` des
// Beobachters (§13.1) — ein Format fuer denselben Begriff, statt zweier.
void    aiml_pipeline_add_exclude(void* pipe, const char* partial);
int32_t aiml_pipeline_lane_count(void* pipe);
// Beobachter: wird je (Schritt, Lane) gerufen.  Die Felder entsprechen dem
// StepRun-Struct der aiml-Seite (spec/62 §13.1) — Reihenfolge MUSS passen.
struct AimlStepRun {
    const char* name;
    const char* bindings;    // "arch=arm64,mode=debug"
    const char* step_key;
    int32_t     lane;
    int32_t     reused;      // 1 = Treffer, Rumpf nicht gelaufen
    int32_t     exit_code;
};
typedef void (*AimlStepObserver)(struct AimlStepRun*);

int32_t aiml_pipeline_run(void* pipe);
int32_t aiml_pipeline_run_observed(void* pipe, void* observer);
int32_t aiml_pipeline_reused_count(void* pipe);
int32_t aiml_pipeline_ran_count(void* pipe);
// Versuche einschliesslich der fehlgeschlagenen (§5.6): nur so ist `retry`
// ueberhaupt messbar — ran_count zaehlt gelungene Ausfuehrungen.
int32_t aiml_pipeline_attempt_count(void* pipe);
int32_t aiml_pipeline_status(void* pipe);
// spec/62 §5.9: Anhalten am Schritt-Rand, Fortsetzen aus dem Cache (F62-95).
void    aiml_pipeline_request_pause(void* pipe);
void    aiml_pipeline_release(void* pipe, const char* step_name);
void    aiml_pipeline_resume(void* pipe);
int32_t aiml_pipeline_lifecycle(void* pipe);   // 0 pending 1 running 2 paused 3 done
// spec/62 §6: gleichzeitige Lanes; 0 = auto.  Kein Schluesselmaterial (F62-98).
void    aiml_pipeline_set_parallel(void* pipe, int32_t n);
// spec/62 §4.3: ein Arbeitsbereich je (Lane, Stufe).  Kein Schluesselmaterial
// (F62-107) — sonst ueberlebte kein Treffer einen Umzug des Projekts.
void    aiml_pipeline_set_workspace_root(void* pipe, const char* root);

} // extern "C"
#endif
