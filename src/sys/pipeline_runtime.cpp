// spec/62 §13.5 — zusammengesetzte Pipelines.
//
// Die Struktur entsteht zur Laufzeit, der RUMPF bleibt ein Code-Bundle: damit
// greifen die Reinheitspruefung (am Rumpf, zur Uebersetzungszeit) und der
// inhaltsabgeleitete Schluessel weiterhin (F62-85).
//
// Zwei Dinge sind hier bewusst streng:
//   * Die Struktur wird VOR dem ersten Schritt vollstaendig geprueft (F62-84).
//     Ein halb gelaufener Lauf aus einer kaputten Struktur hinterliesse
//     Cache-Eintraege fuer eine Pipeline, die es so nie gab.
//   * Der Schluessel enthaelt nur die Achsen, die ein Schritt DEKLARIERT
//     (use_arg, F62-86) — sonst verliert man die Wiederverwendung ueber die
//     nicht benutzten Achsen hinweg.
#include "pipeline_runtime.h"
#include "string_runtime.h"
#include "../../logger.h"
#include "../../exec_impl.h"
#include "../core/path_runtime.h"

#include <algorithm>
#include <atomic>
#include <unistd.h>
#include <chrono>
#include <mutex>
#include <thread>
#include <map>
#include <set>
#include <string>
#include <vector>

// Inhaltsadresse eines Code-Bundles (FNV-1a der chunks), definiert in
// src/core/code_runtime.cpp — hier der Teil des Schritt-Schluessels, der den
// RUMPF erfasst (spec/62 F62-85).
extern "C" const char* aiml_code_id(void* code);
// spec/62 F62-103: wurde der Rumpf mit `pure: true` gebuendelt (also geprueft)?
extern "C" int32_t aiml_code_is_pure(void* code);

namespace {

struct AimlStringList { int32_t size; int32_t capacity; char** data; };

// spec/62 F62-90: was der gerade laufende Schritt ueber sich selbst sagt.
// Thread-lokal, weil Lanes in Threads laufen duerfen (§6) — ein geteilter
// Platz liesse zwei Lanes ihre Ergebnisse gegenseitig ueberschreiben.
struct CurStep {
    bool        active        = false;
    // §13.5e: was dieser Schritt in DIESER Lane sieht.  `declared` ist die
    // Menge aus use_arg — eine nicht deklarierte Achse zu lesen ist ein
    // Fehler und kein leerer Wert (F62-106), sonst entstuende ein blinder
    // Schluessel.
    std::map<std::string, std::string> bindings;
    std::set<std::string>              declared;
    std::map<std::string, std::string> host;
    std::string                        workspace;
    // §13.5h: was dieser Rumpf veroeffentlicht, und was er lesen DARF.
    // `need_results` enthaelt nur die Ergebnisse der DEKLARIERTEN needs —
    // alles andere waere ein blinder Schluessel (F62-119).
    std::string                        result;
    bool                               result_set = false;
    std::map<std::string, std::string> need_results;
    bool        status_set    = false;
    int32_t     status        = -1;
    int         fail_reach    = -1;   // -1 keiner, 0 lane, 1 pipeline
    std::string fail_msg;
};
thread_local CurStep g_cur;

// spec/62 F62-93: die Praezedenzordnung von F62-43 — VORLAEUFIG, bis §16
// Punkt 6 die Bedeutungen festlegt.  Hier steht sie an EINER Stelle, damit
// genau eine Zeile zu aendern ist, wenn sie entschieden wird.  Entschieden
// ist nur: die Ordnung ist total, und aggregiert wird durch Auswahl des
// hoechsten Rangs, nicht durch additives Mischen.
// StepStatus: 0 red, 1 green, 2 blue, 3 yellow, 4 cyan, 5 magenta.
const int kStatusPrecedence[6] = { 5, 0, 1, 3, 2, 4 };

int32_t status_max(int32_t a, int32_t b) {
    if (a < 0) return b;
    if (b < 0) return a;
    if (a > 5 || b > 5) return (a > 5) ? b : a;
    return (kStatusPrecedence[a] >= kStatusPrecedence[b]) ? a : b;
}

struct PStep {
    std::string           name;
    void*                 body = nullptr;   // Code*
    int32_t               cache = 0;        // CacheClass: 0=opaque 1=resolved 2=pure
    std::vector<std::string> args;          // deklarierte Achsen (F62-86)
    void*                 resolve = nullptr;  // Sonde (§9); nur bei cache==resolved
    int32_t               on_fail = 0;        // 0=lane 1=pipeline 2=continue (F62-39)
    int32_t               timeout_ms = 0;     // 0 = keine Grenze (F62-40)
    int32_t               retry = 1;          // Versuche (F62-41)
    int32_t               status = -1;        // StepStatus, gueltig ab done (F62-44)
    int32_t               pause_before = 0;   // deklariertes Tor (F62-54)
    int32_t               per = 0;            // 0 = lane, 1 = once (§5.5)
    std::vector<std::string> needs;            // nur fruehere Schritte (F62-9)
};

struct PStage {
    std::string        name;
    std::string        runs_on;      // `agent:` — WER nimmt den Schritt (F110-127)
    // spec/62 §20.3: hier stehen die AUFGELOESTEN Werte — die Umschreibung hat
    // den Vorrang (Stufe schlaegt Pipeline, F62-151) schon angewandt.  Ein
    // Leser rechnet nichts nach, sonst ist er ein zweiter Parser (§20.1).
    std::string        steplayer;    // `own` | `none` (§18.5, F110-124/125)
    std::string        image;        // WORIN (F62-66)
    std::string        base;         // WORAUF: run:<lauf> | job:<kennung> (F62-144)
    std::string        requires_on;  // "git,gcc>=13" (§18.2, F62-126)
    std::string        shares;       // "name:/ziel:modus,…" (§18.1b, F62-136)
    std::string        command;      // `run:` — Befehl der Scheibe 1
    int32_t            cpu_cores = 0;   // 0 = Vorgabe des Wirts, NICHT unbegrenzt
    int32_t            mem_mb    = 0;
    int32_t            pids_max  = 0;
    int32_t            timeout_ms = 0;  // 0 = Vorgabe des Ausfuehrenden (F62-40)
    std::vector<PStep> steps;
};

// `push:` — wohin ein Abbild geht (§18, F62-69/71).  `from` nennt die STUFE.
struct PPush {
    std::string from;
    std::string to;
    std::string tag;
};

struct PPipeline {
    std::string                                       name;
    std::vector<std::map<std::string, std::string>>   excludes;   // Teilbindungen (§4.2)
    std::vector<std::pair<std::string, std::vector<std::string>>> params;  // Achse -> Werte
    std::vector<PStage>                               stages;
    std::vector<PPush>                                pushes;
    // spec/62 §4.1 (F62-1): der TYP einer Achse.  Er zaehlt im Rumpf (`jobs`
    // ist ein i32, kein Umgebungs-String) und gehoert damit zur Gestalt des
    // Laufs, die ein Wirt liest (§20.4).
    std::map<std::string, std::string>                param_types;
    // Ergebniszaehler des letzten Laufs
    int32_t reused   = 0;
    int32_t ran      = 0;
    int32_t attempts = 0;   // auch die fehlgeschlagenen (§5.6)
    int32_t status   = -1;  // aggregiert ueber die Schritte (F62-43)
    // §5.9: Lebenszyklus getrennt vom Ausgang (F62-44 ergaenzt).
    int32_t lifecycle    = 0;   // 0 pending 1 running 2 paused 3 done
    bool    pause_wanted = false;
    std::set<std::string> released;   // freigegebene Tore (F62-54)
    std::string workspace_root = ".aiml_pipeline";
    int32_t parallel = 1;   // gleichzeitige Lanes (§6); 0 = auto
    // Schritt-Cache: Schluessel -> (Exit-Code, Ergebnis).  Das Ergebnis MUSS
    // mit drin stehen (F62-120/F62-37): sonst waere ein wiederverwendeter
    // Schritt fuer seine eigenen Nachfolger unsichtbar.  Bewusst schlicht;
    // der echte Layer-Cache liegt beim Wirt (cosmos spec/96).
    struct CacheEntry { int32_t rc = 0; std::string result; };
    std::map<std::string, CacheEntry> cache;
};

// Lane = eine vollstaendige Bindung aller Achsen.
using Bindings = std::map<std::string, std::string>;

void expand(const std::vector<std::pair<std::string, std::vector<std::string>>>& params,
            size_t idx, Bindings current, std::vector<Bindings>& out) {
    if (idx >= params.size()) { out.push_back(current); return; }
    for (const auto& v : params[idx].second) {
        current[params[idx].first] = v;
        expand(params, idx + 1, current, out);
    }
}

// Bindungen als lesbarer Text, stabil sortiert (map ist geordnet).  EIN
// Format fuer Lane-Text, Beobachter-`bindings`, Arbeitsbereichsname und den
// Datensatz des Kontrollers (spec/62 F62-152) — zwei Formate fuer denselben
// Begriff heissen, dass irgendwo Anzeigetext geparst wird.
std::string bind_text(const Bindings& b) {
    std::string out;
    for (const auto& kv : b) {
        if (!out.empty()) out += ",";
        out += kv.first + "=" + kv.second;
    }
    return out;
}

// Schluessel eines (Schritt, Lane): Bundle-Adresse + DEKLARIERTE Bindungen,
// laengenpraefigiert wie spec/62 §8 — ein Trennzeichen kann im Wert vorkommen.
std::string step_key(const PStep& st, const Bindings& bind, const std::string& resolved) {
    std::string mat;
    const char* bundle_id = aiml_code_id(st.body);
    std::string bid = bundle_id ? bundle_id : "";
    mat += std::to_string(bid.size()) + ":" + bid;
    mat += std::to_string(st.cache) + ":c";
    for (const auto& axis : st.args) {               // bereits sortiert (std::set-artig)
        auto it = bind.find(axis);
        std::string pair = axis + "=" + (it == bind.end() ? "" : it->second);
        mat += std::to_string(pair.size()) + ":" + pair;
    }
    // Sondenergebnis: leer bei pure/opaque, sonst die aufgeloeste Adresse (§9).
    mat += std::to_string(resolved.size()) + ":" + resolved;
    return mat;
}

} // namespace

extern "C" {

void* aiml_step_new(void) { return new PStep(); }
void  aiml_step_set_name(void* s, const char* n) { if (s && n) static_cast<PStep*>(s)->name = n; }
void  aiml_step_set_body(void* s, void* code)    { if (s) static_cast<PStep*>(s)->body = code; }
void  aiml_step_set_cache(void* s, int32_t c)    { if (s) static_cast<PStep*>(s)->cache = c; }
void  aiml_step_use_arg(void* s, const char* a)  {
    if (!s || !a) return;
    auto* st = static_cast<PStep*>(s);
    // sortiert einfuegen: die Schluesselbildung darf nicht von der
    // Aufrufreihenfolge abhaengen (spec/62 §8).
    std::string axis(a);
    for (const auto& have : st->args) if (have == axis) return;
    st->args.push_back(axis);
    std::sort(st->args.begin(), st->args.end());
}

void aiml_step_set_resolve(void* s, void* fn) { if (s) static_cast<PStep*>(s)->resolve = fn; }

void* aiml_stage_new(void) { return new PStage(); }
void  aiml_stage_set_name(void* g, const char* n)     { if (g && n) static_cast<PStage*>(g)->name = n; }
void  aiml_stage_set_runs_on(void* g, const char* l)  { if (g && l) static_cast<PStage*>(g)->runs_on = l; }
void  aiml_stage_add_step(void* g, void* s) {
    if (!g || !s) return;
    static_cast<PStage*>(g)->steps.push_back(*static_cast<PStep*>(s));
}

void* aiml_pipeline_new(void) { return new PPipeline(); }
void  aiml_pipeline_set_name(void* p, const char* n) { if (p && n) static_cast<PPipeline*>(p)->name = n; }

void aiml_pipeline_add_param(void* p, const char* axis, const char* type, const void* values) {
    if (!p || !axis || !values) return;
    if (type && *type) static_cast<PPipeline*>(p)->param_types[axis] = type;
    const auto* lst = static_cast<const AimlStringList*>(values);
    std::vector<std::string> vals;
    for (int32_t i = 0; i < lst->size; ++i)
        if (lst->data[i]) vals.push_back(lst->data[i]);
    static_cast<PPipeline*>(p)->params.push_back({std::string(axis), vals});
}

// "arch=arm64,compiler=gcc" -> { arch: arm64, compiler: gcc }
static std::map<std::string, std::string> parse_partial(const std::string& txt) {
    std::map<std::string, std::string> out;
    size_t pos = 0;
    while (pos <= txt.size()) {
        size_t comma = txt.find(',', pos);
        std::string item = txt.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        size_t eq = item.find('=');
        if (eq != std::string::npos) out[item.substr(0, eq)] = item.substr(eq + 1);
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return out;
}

void aiml_step_set_on_fail(void* st, int32_t reach) {
    if (st && reach >= 0 && reach <= 2) static_cast<PStep*>(st)->on_fail = reach;
}
void aiml_step_set_timeout(void* st, int32_t ms) {
    if (st && ms >= 0) static_cast<PStep*>(st)->timeout_ms = ms;
}
void aiml_step_set_retry(void* st, int32_t tries) {
    if (st && tries >= 1) static_cast<PStep*>(st)->retry = tries;
}
int32_t aiml_step_status(void* st) { return st ? static_cast<PStep*>(st)->status : -1; }
void aiml_step_set_pause_before(void* st, int32_t on) {
    if (st) static_cast<PStep*>(st)->pause_before = (on != 0) ? 1 : 0;
}
void aiml_step_set_per(void* st, int32_t per) {
    if (st && per >= 0 && per <= 1) static_cast<PStep*>(st)->per = per;
}
void aiml_step_add_need(void* st, const char* name) {
    if (st && name && *name) static_cast<PStep*>(st)->needs.push_back(name);
}

// F62-90: ohne laufenden Schritt ein FEHLER, kein stiller Nulleffekt — sonst
// verschwindet ein `fail_lane` im Nichts, das ausgerechnet dann gerufen wird,
// wenn etwas schiefgeht.
void aiml_step_status_set(int32_t status) {
    if (!g_cur.active) { LOG_ERROR("set_status: kein laufender Schritt"); return; }
    if (status < 0 || status > 5) { LOG_ERROR("set_status: kein StepStatus-Wert"); return; }
    g_cur.status_set = true;
    g_cur.status     = status;
}
void aiml_step_fail_lane(const char* msg) {
    if (!g_cur.active) { LOG_ERROR("fail_lane: kein laufender Schritt"); return; }
    g_cur.fail_reach = 0;
    g_cur.fail_msg   = msg ? msg : "";
}
void aiml_step_fail_pipeline(const char* msg) {
    if (!g_cur.active) { LOG_ERROR("fail_pipeline: kein laufender Schritt"); return; }
    g_cur.fail_reach = 1;
    g_cur.fail_msg   = msg ? msg : "";
}

void aiml_pipeline_add_exclude(void* p, const char* partial) {
    if (!p || !partial) return;
    static_cast<PPipeline*>(p)->excludes.push_back(parse_partial(partial));
}

// matches: trifft die Teilbindung auf diese Lane zu?  Teilbindung heisst:
// nur die genannten Achsen zaehlen, der Rest ist egal (§4.2) — sonst muesste
// man jede Aussage fuer jeden Wert der uebrigen Achsen wiederholen.
static bool excluded(const std::vector<std::map<std::string, std::string>>& excludes,
                     const Bindings& bind) {
    for (const auto& ex : excludes) {
        bool all = true;
        for (const auto& kv : ex) {
            auto it = bind.find(kv.first);
            if (it == bind.end() || it->second != kv.second) { all = false; break; }
        }
        if (all) return true;
    }
    return false;
}

void aiml_pipeline_add_stage(void* p, void* g) {
    if (!p || !g) return;
    static_cast<PPipeline*>(p)->stages.push_back(*static_cast<PStage*>(g));
}

int32_t aiml_pipeline_lane_count(void* p) {
    if (!p) return 0;
    auto* pp = static_cast<PPipeline*>(p);
    std::vector<Bindings> lanes;
    expand(pp->params, 0, Bindings{}, lanes);
    int32_t n = 0;
    for (const auto& b : lanes) if (!excluded(pp->excludes, b)) ++n;
    return n;
}

int32_t aiml_pipeline_reused_count(void* p) { return p ? static_cast<PPipeline*>(p)->reused : 0; }
int32_t aiml_pipeline_ran_count(void* p)    { return p ? static_cast<PPipeline*>(p)->ran    : 0; }
int32_t aiml_pipeline_attempt_count(void* p) { return p ? static_cast<PPipeline*>(p)->attempts : 0; }
int32_t aiml_pipeline_status(void* p)        { return p ? static_cast<PPipeline*>(p)->status   : -1; }

// §5.9: von aussen anhalten.  Wirkt NACH dem laufenden Schritt (F62-55) — ein
// Rumpf hat lebende Kindprozesse, und ihn mitten im Lauf zu stoppen liesse
// einen halb geschriebenen Arbeitsbereich zurueck, der dann gehasht und
// spaeter als Treffer ausgeliefert wuerde.
void aiml_pipeline_request_pause(void* p) {
    if (p) static_cast<PPipeline*>(p)->pause_wanted = true;
}
void aiml_pipeline_release(void* p, const char* name) {
    if (p && name) static_cast<PPipeline*>(p)->released.insert(name);
}
// Fortsetzen braucht keinen eigenen Zustand (F62-56): nur das Kennzeichen
// loeschen.  Was fortgesetzt wird, steht im Cache.
void aiml_pipeline_resume(void* p) {
    if (!p) return;
    auto* pp = static_cast<PPipeline*>(p);
    pp->pause_wanted = false;
    pp->lifecycle    = 0;
}
int32_t aiml_pipeline_lifecycle(void* p) { return p ? static_cast<PPipeline*>(p)->lifecycle : 0; }

// §6 / F62-98: begrenzt gleichzeitige LANES, nicht Prozesse.  Kein
// Schluesselmaterial — zwei Laeufe mit 1 und mit 8 muessen dieselben
// Schluessel ergeben.
void aiml_pipeline_set_parallel(void* p, int32_t n) {
    if (p && n >= 0) static_cast<PPipeline*>(p)->parallel = n;
}

void aiml_pipeline_set_workspace_root(void* p, const char* root) {
    if (p && root && *root) static_cast<PPipeline*>(p)->workspace_root = root;
}

// §13.5e: der Rumpf liest seine Lane.  Ohne laufenden Schritt ein Fehler —
// derselbe Grundsatz wie bei set_status (F62-90): kein stiller Nulleffekt.
char* aiml_step_arg(const char* axis) {
    if (!g_cur.active) { LOG_ERROR("step.arg: kein laufender Schritt"); return aiml_str_alloc(0); }
    std::string name = axis ? axis : "";
    // F62-106: nicht deklariert heisst Fehler, nicht leer.  Ein leerer Wert
    // verstecke genau das Loch, das der Schluessel schliessen soll.
    if (!g_cur.declared.count(name)) {
        LOG_ERROR("step.arg('" + name + "'): diese Achse ist nicht mit use_arg deklariert — "
                  "sie wuerde auf das Ergebnis wirken, ohne im Schluessel zu stehen "
                  "(spec/62 F62-106)");
        g_cur.fail_reach = 0;
        g_cur.fail_msg   = "step.arg('" + name + "') nicht deklariert";
        return aiml_str_alloc(0);
    }
    auto it = g_cur.bindings.find(name);
    const std::string val = (it != g_cur.bindings.end()) ? it->second : std::string();
    char* out = aiml_str_alloc(val.size());
    if (!val.empty()) memcpy(out, val.data(), val.size());
    return out;
}

// §13.5h: das Ergebnis dieses Schritts veroeffentlichen.  Eine Zahl wird
// dezimal geschrieben — aufgezeichnet wird die serialisierte Form (F62-118).
void aiml_step_set_result(const char* value) {
    if (!g_cur.active) { LOG_ERROR("step.set_result: kein laufender Schritt"); return; }
    g_cur.result     = value ? value : "";
    g_cur.result_set = true;
}

// F62-119: nur ein DEKLARIERTES need darf gelesen werden.  Ein fremdes
// Ergebnis wirkte sonst auf den Rumpf, ohne ueber input_keys im Schluessel zu
// stehen — genau der blinde Schluessel, den §1.1 beschreibt.
char* aiml_step_need_result(const char* name) {
    if (!g_cur.active) { LOG_ERROR("step.need_result: kein laufender Schritt"); return aiml_str_alloc(0); }
    std::string key = name ? name : "";
    auto it = g_cur.need_results.find(key);
    if (it == g_cur.need_results.end()) {
        LOG_ERROR("step.need_result('" + key + "'): dieser Schritt steht nicht in den needs — "
                  "sein Ergebnis wuerde wirken, ohne im Schluessel zu stehen "
                  "(spec/62 F62-119)");
        g_cur.fail_reach = 0;
        g_cur.fail_msg   = "step.need_result('" + key + "') nicht in needs";
        return aiml_str_alloc(0);
    }
    char* out = aiml_str_alloc(it->second.size());
    if (!it->second.empty()) memcpy(out, it->second.data(), it->second.size());
    return out;
}

char* aiml_step_workspace(void) {
    if (!g_cur.active) { LOG_ERROR("step.workspace: kein laufender Schritt"); return aiml_str_alloc(0); }
    char* out = aiml_str_alloc(g_cur.workspace.size());
    if (!g_cur.workspace.empty()) memcpy(out, g_cur.workspace.data(), g_cur.workspace.size());
    return out;
}

char* aiml_step_host(const char* name) {
    if (!g_cur.active) { LOG_ERROR("step.host: kein laufender Schritt"); return aiml_str_alloc(0); }
    auto it = g_cur.host.find(name ? name : "");
    const std::string val = (it != g_cur.host.end()) ? it->second : std::string();
    char* out = aiml_str_alloc(val.size());
    if (!val.empty()) memcpy(out, val.data(), val.size());
    return out;
}

int32_t aiml_pipeline_run(void* p) { return aiml_pipeline_run_observed(p, nullptr); }

int32_t aiml_pipeline_run_observed(void* p, void* observer_raw) {
    auto observer = reinterpret_cast<AimlStepObserver>(observer_raw);
    if (!p) return -2;
    auto* pp = static_cast<PPipeline*>(p);
    // spec/62 F62-124a: verteilte Schluessel UEBERSETZEN, aber lokal nicht
    // LAUFEN — hier, vor dem ersten Schritt, mit Begruendung und Weg.
    if (int32_t refused = aiml_pipeline_local_refusal(p)) return refused;
    pp->reused   = 0;
    pp->ran      = 0;
    pp->attempts = 0;
    pp->status   = -1;
    pp->lifecycle = 1;   // running

    // ── F62-84: Struktur VOLLSTAENDIG pruefen, bevor irgendetwas laeuft ──
    std::set<std::string> axes;
    for (const auto& pr : pp->params) axes.insert(pr.first);
    // F62-9: Schrittnamen sind eindeutig, und `needs` nennt nur FRUEHERE
    // Schritte.  Beides zusammen macht den Graphen zum DAG by construction —
    // darum braucht es zur Laufzeit keine Zyklenpruefung.
    {
        std::set<std::string> seen;
        for (const auto& stg : pp->stages) {
            for (const auto& st : stg.steps) {
                for (const auto& need : st.needs) {
                    if (!seen.count(need)) {
                        LOG_ERROR("pipeline '" + pp->name + "': Schritt '" + st.name +
                                  "' braucht '" + need + "' — das ist kein frueher "
                                  "deklarierter Schritt (spec/62 F62-9)");
                        return -1;
                    }
                }
                if (!seen.insert(st.name).second) {
                    LOG_ERROR("pipeline '" + pp->name + "': der Schrittname '" + st.name +
                              "' kommt zweimal vor — dann ist nicht entscheidbar, welchen "
                              "Schritt ein 'needs' meint (spec/62 F62-9)");
                    return -1;
                }
            }
        }
    }
    for (const auto& stg : pp->stages) {
        for (const auto& st : stg.steps) {
            if (!st.body) {
                LOG_ERROR("pipeline '" + pp->name + "': Schritt '" + st.name + "' hat keinen Rumpf");
                return -1;
            }
            // F62-16: 'resolved' ohne Sonde ist sinnlos — der Schluessel haette
            // nichts, was den beweglichen Namen aufloest, und der Schritt traefe
            // auf ewig denselben Eintrag.
            if (st.cache == 1 && !st.resolve) {
                LOG_ERROR("pipeline '" + pp->name + "': Schritt '" + st.name +
                          "' ist 'resolved', hat aber keine Sonde (set_resolve)");
                return -1;
            }
            // F62-16, zweite Haelfte: 'pure' DARF keine Sonde haben.  Eine
            // Sonde loest einen beweglichen Namen auf — ein reiner Schritt hat
            // keinen, er haengt nur an gehashten Eingaben.  Beides zugleich zu
            // erlauben hiesse, zwei verschiedene Begruendungen fuer denselben
            // Schluessel zu akzeptieren, und verdeckt, welche von beiden gilt.
            // F62-103: `pure` ohne geprueften Rumpf ist eine WARNUNG, kein
            // Fehler.  Die Laufzeit kann die Behauptung nicht nachpruefen (der
            // Rumpf ist ein Bundle); einen Lauf abzubrechen, weil eine
            // PRUEFUNG fehlt, machte die Pipeline zur Geisel einer Angabe, die
            // der Autor vielleicht bewusst nicht gemacht hat.
            if (st.cache == 2 && !aiml_code_is_pure(st.body)) {
                LOG_WARNING("pipeline '" + pp->name + "': Schritt '" + st.name +
                            "' ist 'pure', sein Rumpf wurde aber nicht mit "
                            "sys.bundle(..., pure: true) geprueft — 'pure' gilt hier "
                            "unbewiesen (spec/62 F62-103)");
            }
            // F62-115: ein `per: once`-Schritt hat KEINE Lane-Bindungen — es
            // gibt ein Ergebnis fuer die ganze Matrix.  Eine deklarierte Achse
            // waere ein Ergebnis, das von der Reihenfolge der Lanes abhaengt.
            if (st.per == 1 && !st.args.empty()) {
                LOG_ERROR("pipeline '" + pp->name + "': Schritt '" + st.name +
                          "' ist 'per: once' und deklariert trotzdem Achsen — ein "
                          "einmaliger Schritt kann nicht von einer Achse haengen "
                          "(spec/62 F62-115)");
                return -1;
            }
            if (st.cache == 2 && st.resolve) {
                LOG_ERROR("pipeline '" + pp->name + "': Schritt '" + st.name +
                          "' ist 'pure' und hat trotzdem eine Sonde — eine Sonde "
                          "gehoert zu 'resolved'");
                return -1;
            }
            // F62-41: retry mit 'pure' ist ein WIDERSPRUCH.  Ein reiner Schritt
            // haengt nur an gehashten Eingaben, also muss ein zweiter Versuch
            // genauso scheitern — wer ihn verlangt, sagt zwei Dinge auf einmal.
            if (st.retry > 1 && st.cache == 2) {
                LOG_ERROR("pipeline '" + pp->name + "': Schritt '" + st.name +
                          "' ist 'pure' und verlangt retry=" + std::to_string(st.retry) +
                          " — ein reiner Schritt scheitert reproduzierbar");
                return -1;
            }
            for (const auto& a : st.args) {
                if (!axes.count(a)) {
                    LOG_ERROR("pipeline '" + pp->name + "': Schritt '" + st.name +
                              "' benutzt die Achse '" + a + "', die die Pipeline nicht hat");
                    return -1;
                }
            }
        }
    }

    // ── F62-4: jede exclude-Angabe muss eine Achse UND einen Wert der
    //    Domaene nennen.  Ein Tippfehler schloesse sonst still NICHTS aus,
    //    und die Matrix liefe groesser als gedacht, ohne dass es auffaellt.
    for (const auto& ex : pp->excludes) {
        for (const auto& kv : ex) {
            const std::vector<std::string>* dom = nullptr;
            for (const auto& pr : pp->params) if (pr.first == kv.first) { dom = &pr.second; break; }
            if (!dom) {
                LOG_ERROR("pipeline '" + pp->name + "': exclude nennt die Achse '" + kv.first +
                          "', die die Pipeline nicht hat");
                return -1;
            }
            bool found = false;
            for (const auto& v : *dom) if (v == kv.second) { found = true; break; }
            if (!found) {
                LOG_ERROR("pipeline '" + pp->name + "': exclude nennt fuer '" + kv.first +
                          "' den Wert '" + kv.second + "', der nicht in ihrer Domaene steht");
                return -1;
            }
        }
    }

    // ── Lanes aufklappen und ausfuehren ─────────────────────────────────
    std::vector<Bindings> all_lanes;
    expand(pp->params, 0, Bindings{}, all_lanes);
    std::vector<Bindings> lanes;
    for (const auto& b : all_lanes) if (!excluded(pp->excludes, b)) lanes.push_back(b);

    // F62-5: alles ausgeschlossen ist ein Fehler, keine leere Erfolgsmeldung.
    if (lanes.empty()) {
        LOG_ERROR("pipeline '" + pp->name + "': exclude schliesst JEDE Lane aus — nichts zu tun");
        return -1;
    }

    // ── §6: Lanes sind nach Konstruktion parallel ───────────────────────
    //
    // Zwei Schloesser, und die Trennung ist der Punkt:
    //   * `mu` schuetzt Cache, Zaehler, Status — und jeden Aufruf in
    //     BENUTZERCODE (Beobachter, Sonde).  Der ist nicht thread-sicher
    //     geschrieben worden und soll es nicht muessen (F62-100).
    //   * `compile_mu` serialisiert NUR das Uebersetzen eines Rumpfs.  Ob
    //     mehrere isolierte Uebersetzungen gleichzeitig duerfen, ist eine
    //     Frage an LLVM und durch die Messung von F62-14 NICHT beantwortet
    //     (F62-99).  Bis dahin: Lanes parallel, Uebersetzen nicht.
    struct RunState {
        std::mutex mu;
        std::mutex compile_mu;
        bool    stop_all = false;   // on_fail=pipeline oder Pause
        int32_t stop_rc  = 0;
        bool    paused   = false;
        int32_t worst_rc = 0;
        // F62-114: der Schluessel JEDES gelaufenen Schritts, nach (Name, Lane).
        // Lane -1 steht fuer einen `per: once`-Schritt, weil es dort nur einen
        // gibt.  Daraus bildet ein Nachfolger seine `input_keys`.
        std::map<std::pair<std::string, int32_t>, std::string> keyOf;
        // F62-117: Schritte, die uebersprungen werden, weil ein Schritt, den
        // sie (transitiv) brauchen, gescheitert ist.  Uebersprungen ist NICHT
        // gescheitert: kein Ausgang, keine Aufzeichnung.
        std::set<std::string> skipped;
        // §13.5h: das Ergebnis jedes gelaufenen ODER getroffenen Schritts.
        // Bei einem Treffer kommt es aus dem Cache (F62-120) — fuer den
        // Nachfolger ist beides dasselbe, und genau das ist der Punkt.
        std::map<std::pair<std::string, int32_t>, std::string> resultOf;
    };
    RunState state;

    // Ein Durchgang ueber eine AUSWAHL von Schritten.  `lane_idx == -1` heisst
    // `per: once`: ein Durchgang ohne Lane-Bindungen (§5.5).
    using StepRef = std::pair<PStage*, PStep*>;
    auto run_steps = [&](const Bindings& bind, int32_t lane_idx,
                         const std::vector<StepRef>& plan) {
        std::string btxt = bind_text(bind);
        {
            for (auto& ref : plan) {
                PStage& stg = *ref.first;
                PStep&  st  = *ref.second;
                {   // Hat eine andere Lane die Vergabe gestoppt?
                    std::lock_guard<std::mutex> g(state.mu);
                    if (state.stop_all) return;
                }
                // F62-117: braucht dieser Schritt etwas, das gescheitert ist?
                // Dann startet er nicht.  Vor allem anderen, auch vor der
                // Sonde — ein uebersprungener Schritt loest nichts auf.
                {
                    std::lock_guard<std::mutex> g(state.mu);
                    if (state.skipped.count(st.name)) continue;
                    bool blocked = false;
                    for (const auto& need : st.needs)
                        if (state.skipped.count(need)) { blocked = true; break; }
                    if (blocked) {
                        state.skipped.insert(st.name);
                        LOG_WARNING("pipeline '" + pp->name + "': Schritt '" + st.name +
                                    "' uebersprungen — er braucht einen gescheiterten "
                                    "Schritt (spec/62 F62-117)");
                        continue;
                    }
                }
                // §5.9 / F62-54: das deklarierte Tor.  Vor der Sonde, denn ein
                // Tor heisst "hier wird nicht weitergearbeitet" — auch nicht
                // aufgeloest.
                if (st.pause_before && !pp->released.count(st.name)) {
                    std::lock_guard<std::mutex> g(state.mu);
                    LOG_INFO("pipeline '" + pp->name + "': angehalten vor Schritt '" +
                             st.name + "' (pause_before)");
                    state.paused   = true;
                    state.stop_all = true;
                    state.stop_rc  = -6;
                    return;
                }
                // Die Sonde laeuft IMMER — auch wenn der Schritt gleich trifft.
                // Genau das ist ihr Zweck: erst ihr Ergebnis entscheidet, OB er
                // trifft (§9).  Wer sie beim Treffer ueberspringt, hat wieder
                // einen beweglichen Namen im Schluessel.  Benutzercode, also
                // unter `mu`.
                std::string resolved;
                if (st.resolve) {
                    std::lock_guard<std::mutex> g(state.mu);
                    auto fn = reinterpret_cast<AimlResolveFn>(st.resolve);
                    const char* out = fn();
                    if (out) resolved = out;
                }
                // F62-114: die Schluessel der `needs` gehen als `input_keys`
                // in den eigenen Schluessel (§8) — dadurch wirkt eine Aenderung
                // weiter oben transitiv nach unten, und eine Kette von
                // Schritten verhaelt sich wie ein Stapel von Schichten.
                std::string inputKeys;
                if (!st.needs.empty()) {
                    std::vector<std::string> ks;
                    {
                        std::lock_guard<std::mutex> g(state.mu);
                        for (const auto& need : st.needs) {
                            auto it = state.keyOf.find({need, lane_idx});
                            if (it == state.keyOf.end())
                                it = state.keyOf.find({need, -1});
                            ks.push_back(it != state.keyOf.end() ? it->second : std::string());
                        }
                    }
                    std::sort(ks.begin(), ks.end());   // §8: sortiert
                    for (const auto& k : ks)
                        inputKeys += std::to_string(k.size()) + ":" + k;
                }
                std::string key = step_key(st, bind, resolved) + inputKeys;
                {
                    std::lock_guard<std::mutex> g(state.mu);
                    state.keyOf[{st.name, lane_idx}] = key;
                }
                int32_t rc     = 0;
                int32_t reused = 0;
                int     reach  = st.on_fail;

                bool hit = false;
                std::string hitResult;
                if (st.cache != 0) {
                    std::lock_guard<std::mutex> g(state.mu);
                    auto it = pp->cache.find(key);
                    if (it != pp->cache.end()) {
                        hit       = true;
                        rc        = it->second.rc;
                        hitResult = it->second.result;
                    }
                }
                if (hit) {   // opaque trifft nie (§7)
                    reused = 1;
                    std::lock_guard<std::mutex> g(state.mu);
                    ++pp->reused;
                    // F62-120: der Treffer stellt das Ergebnis wieder her,
                    // ohne den Rumpf zu rufen.
                    state.resultOf[{st.name, lane_idx}] = hitResult;
                } else {
                    // F62-41: bis zu `retry` Versuche, Halt beim ersten, der
                    // gelingt.  Gezaehlt werden ALLE Versuche (attempts), denn
                    // sonst waere `retry` nicht beobachtbar.
                    // §4.3 / F62-60: ein Arbeitsbereich je (Lane, Stufe), aus
                    // deren Identitaet gebildet — so koennen gleichzeitige
                    // Lanes einander nicht sehen oder verderben.  Absolut,
                    // damit `cwd` im Rumpf nichts daran aendert (F62-30: es
                    // gibt kein elternseitiges chdir, und das ist der Grund,
                    // warum Lanes ueberhaupt in Threads laufen duerfen).
                    std::string ws_root = pp->workspace_root;
                    if (!ws_root.empty() && ws_root[0] != '/') {
                        char cwdbuf[4096];
                        if (getcwd(cwdbuf, sizeof(cwdbuf)))
                            ws_root = std::string(cwdbuf) + "/" + ws_root;
                    }
                    std::string ws = ws_root + "/" + pp->name + "/" +
                                     (btxt.empty() ? std::string("_") : btxt) + "/" + stg.name;
                    aiml_io_mkdir_all(ws.c_str());

                    for (int32_t attempt = 1; attempt <= st.retry; ++attempt) {
                        g_cur = CurStep{};
                        g_cur.active    = true;
                        g_cur.bindings  = bind;
                        g_cur.workspace = ws;
                        for (const auto& axis : st.args) g_cur.declared.insert(axis);
                        // §13.5h: nur die DEKLARIERTEN needs sind lesbar —
                        // dieselbe Grenze, die der Schluessel zieht (F62-119).
                        {
                            std::lock_guard<std::mutex> g(state.mu);
                            for (const auto& need : st.needs) {
                                auto rit = state.resultOf.find({need, lane_idx});
                                if (rit == state.resultOf.end())
                                    rit = state.resultOf.find({need, -1});
                                g_cur.need_results[need] =
                                    (rit != state.resultOf.end()) ? rit->second : std::string();
                            }
                        }
                        auto t0 = std::chrono::steady_clock::now();
                        {
                            std::lock_guard<std::mutex> g(state.compile_mu);
                            rc = aiml_exec_code(st.body);
                        }
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now() - t0).count();
                        {
                            std::lock_guard<std::mutex> g(state.mu);
                            ++pp->attempts;
                        }
                        // F62-40: die Grenze gilt dem GANZEN Schritt.  Hier
                        // wird sie NACHTRAEGLICH festgestellt, nicht erzwungen:
                        // den Rumpf mitten im Lauf abzubrechen hiesse, JIT-Code
                        // in einem fremden Thread zu toeten.  Das Toeten der
                        // Kinder (process.kill, spec/23 §5.2) gehoert zu dem
                        // Tag, an dem die Grenze erzwungen wird; bis dahin ist
                        // sie eine Feststellung und keine Unterbrechung — ein
                        // Unterschied, der im Log stehen muss, damit niemand
                        // sich auf eine Unterbrechung verlaesst.
                        if (st.timeout_ms > 0 && elapsed > st.timeout_ms) {
                            LOG_ERROR("pipeline '" + pp->name + "': Schritt '" + st.name +
                                      "' ueberschritt das Zeitlimit (" + std::to_string(elapsed) +
                                      " ms > " + std::to_string(st.timeout_ms) +
                                      " ms) — der Lauf wurde NICHT unterbrochen (§5.6, F62-40)");
                            if (rc == 0) rc = -4;
                        }
                        // Was der Rumpf selbst gesagt hat (F62-90).  g_cur ist
                        // thread-lokal, darum sieht jede Lane nur ihr eigenes.
                        if (g_cur.status_set) st.status = g_cur.status;
                        if (g_cur.result_set) {
                            std::lock_guard<std::mutex> g(state.mu);
                            state.resultOf[{st.name, lane_idx}] = g_cur.result;
                        }
                        if (g_cur.fail_reach >= 0) {
                            // F62-46: ausdruecklicher Abbruch schlaegt die
                            // Vorgabe des Schritts — der Rumpf weiss an dieser
                            // Stelle mehr als die Deklaration.
                            reach = (g_cur.fail_reach == 1) ? 1 : 0;
                            if (rc == 0) rc = -5;
                            LOG_ERROR("pipeline '" + pp->name + "': Schritt '" + st.name +
                                      "' brach ab: " + g_cur.fail_msg);
                        }
                        g_cur = CurStep{};
                        if (rc == 0) break;
                        if (attempt < st.retry)
                            LOG_WARNING("pipeline '" + pp->name + "': Schritt '" + st.name +
                                        "' Versuch " + std::to_string(attempt) + " von " +
                                        std::to_string(st.retry) + " endete mit " + std::to_string(rc));
                    }
                    std::lock_guard<std::mutex> g(state.mu);
                    if (rc == 0) ++pp->ran;
                    // §7 / F62-92: NUR ein gelungener Versuch wird aufgezeichnet.
                    // Einen Fehlschlag aufzuzeichnen hiesse, ihn fuer jede
                    // weitere Lane zu wiederholen, ohne es versucht zu haben.
                    if (st.cache != 0 && rc == 0) {
                        PPipeline::CacheEntry entry;
                        entry.rc = rc;
                        auto rit = state.resultOf.find({st.name, lane_idx});
                        if (rit != state.resultOf.end()) entry.result = rit->second;
                        pp->cache[key] = entry;
                    }
                }

                {
                    std::lock_guard<std::mutex> g(state.mu);
                    // Automatische Farbe, wenn der Rumpf keine gesetzt hat.
                    // Welche Farbe was bedeutet, ist offen (§16.6) — darum
                    // steht hier nur die Zuordnung an EINER Stelle: 1 (green)
                    // gelungen, 4 (cyan) getroffen, 0 (red) gescheitert.
                    int32_t st_status = (st.status >= 0) ? st.status
                                                         : ((rc != 0) ? 0 : (reused ? 4 : 1));
                    st.status  = st_status;
                    pp->status = status_max(pp->status, st_status);

                    // Beobachter VOR der Fehlerbehandlung: ein fehlgeschlagener
                    // Schritt ist das, was der Wirt am dringendsten sehen will
                    // (F62-78).  Er bekommt eine Kopie, kein ref<> (F62-80).
                    // Serialisiert (F62-100): die Reihenfolge ist dann nicht
                    // die Quelltextreihenfolge, aber jeder Aufruf ist allein.
                    if (observer) {
                        AimlStepRun ev{};
                        ev.name      = st.name.c_str();
                        ev.bindings  = btxt.c_str();
                        ev.step_key  = key.c_str();
                        ev.lane      = lane_idx;
                        ev.reused    = reused;
                        ev.exit_code = rc;
                        observer(&ev);
                    }

                    // F62-55: eine von aussen gewuenschte Pause wirkt NACH
                    // diesem Schritt — der Arbeitsbereich ist jetzt ein
                    // vollstaendiger Baum, und der Schluessel ist aufgezeichnet.
                    if (pp->pause_wanted && rc == 0) {
                        LOG_INFO("pipeline '" + pp->name + "': angehalten nach Schritt '" +
                                 st.name + "'");
                        state.paused   = true;
                        state.stop_all = true;
                        state.stop_rc  = -6;
                        return;
                    }

                    if (rc != 0) {
                        LOG_ERROR("pipeline '" + pp->name + "': Schritt '" + st.name +
                                  "' endete mit " + std::to_string(rc));
                        // F62-39: die Reichweite steht am Schritt, nicht an der
                        // Pipeline — ein fehlgeschlagenes fetch_sources macht
                        // jede Lane sinnlos, ein fehlgeschlagenes build in einer
                        // Lane sagt ueber die anderen nichts.
                        if (reach == 1) {           // pipeline: nichts startet mehr
                            state.stop_all = true;
                            state.stop_rc  = rc;
                            return;
                        }
                        if (reach == 0) {           // lane
                            state.worst_rc = rc;
                            // F62-117: bei einem `per: once`-Schritt gibt es
                            // keine einzelne Lane, die enden koennte — statt
                            // dessen starten genau seine Abhaengigen nicht.
                            if (st.per == 1) {
                                state.skipped.insert(st.name);
                                continue;
                            }
                            return;
                        }
                        // proceed: vermerkt, die Lane laeuft weiter — der Lauf
                        // selbst gilt deshalb NICHT als gescheitert.
                    }
                }
            }
        }
    };

    int32_t limit = pp->parallel;
    if (limit == 0) {
        unsigned hw = std::thread::hardware_concurrency();
        limit = (hw > 0) ? static_cast<int32_t>(hw) : 1;
    }
    if (limit > static_cast<int32_t>(lanes.size())) limit = static_cast<int32_t>(lanes.size());

    // ── §5.5 / F62-116: Abschnitte, getrennt an jedem `per: once`-Schritt ──
    //
    // Ein `per: once`-Schritt ist ein RIEGEL: alle Lanes erreichen ihn, er
    // laeuft genau einmal, danach laufen die Lanes weiter.  Zwischen zwei
    // Riegeln bleibt es bei Lane-Parallelitaet (§6) — gewartet wird nur dort,
    // wo etwas Gemeinsames entsteht.
    //
    // Der Plan wird in Deklarationsreihenfolge gebildet.  Das genuegt fuer
    // `needs`, weil F62-9 nur FRUEHERE Schritte erlaubt: wer in der Reihenfolge
    // vorn steht, ist fertig, bevor sein Nachfolger beginnt.
    struct Segment {
        bool                 once = false;
        std::vector<StepRef> steps;   // bei once genau einer
    };
    std::vector<Segment> segments;
    for (auto& stg : pp->stages) {
        for (auto& st : stg.steps) {
            if (st.per == 1) {
                Segment seg;
                seg.once = true;
                seg.steps.push_back({&stg, &st});
                segments.push_back(std::move(seg));
            } else {
                if (segments.empty() || segments.back().once) segments.push_back(Segment{});
                segments.back().steps.push_back({&stg, &st});
            }
        }
    }

    auto run_lane_segment = [&](const std::vector<StepRef>& plan) {
        if (limit <= 1) {
            for (size_t i = 0; i < lanes.size(); ++i) {
                if (state.stop_all) break;
                run_steps(lanes[i], static_cast<int32_t>(i), plan);
            }
            return;
        }
        // Arbeitsteilung ueber einen gemeinsamen Index: so bekommt eine Lane,
        // die lange braucht, nicht sieben weitere an den Hals, waehrend andere
        // Threads leer laufen.
        std::atomic<size_t> next{0};
        std::vector<std::thread> workers;
        workers.reserve(static_cast<size_t>(limit));
        for (int32_t w = 0; w < limit; ++w) {
            workers.emplace_back([&]() {
                for (;;) {
                    size_t i = next.fetch_add(1);
                    if (i >= lanes.size()) return;
                    {
                        std::lock_guard<std::mutex> g(state.mu);
                        if (state.stop_all) return;
                    }
                    run_steps(lanes[i], static_cast<int32_t>(i), plan);
                }
            });
        }
        for (auto& t : workers) t.join();
    };

    for (const auto& seg : segments) {
        {
            std::lock_guard<std::mutex> g(state.mu);
            if (state.stop_all) break;
        }
        if (seg.once) {
            // Einmal, ohne Lane-Bindungen und einthreadig: der Riegel selbst
            // ist die Stelle, an der nichts nebenher laufen soll.
            run_steps(Bindings{}, -1, seg.steps);
        } else {
            run_lane_segment(seg.steps);
        }
    }

    pp->lifecycle = state.paused ? 2 : 3;
    return (state.stop_rc != 0) ? state.stop_rc : state.worst_rc;
}

// ── spec/62 §20: die uebersetzte Pipeline LESEN ─────────────────────────────
//
// Geschrieben wird von der Umschreibung (pipeline_expand), gelesen vom Wirt.
// Beides geht ueber EINEN Schluesselnamen je Feld, damit die Namen in Spec,
// Umschreibung und Leser nicht auseinanderlaufen koennen.

static std::string* stage_text(PStage* g, const std::string& key) {
    if (key == "name")       return &g->name;
    if (key == "agent")      return &g->runs_on;
    if (key == "steplayer")  return &g->steplayer;
    if (key == "image")      return &g->image;
    if (key == "base")       return &g->base;
    if (key == "requires")   return &g->requires_on;
    if (key == "shares")     return &g->shares;
    if (key == "command")    return &g->command;
    return nullptr;
}

static int32_t* stage_num(PStage* g, const std::string& key) {
    if (key == "cpu")     return &g->cpu_cores;
    if (key == "mem")     return &g->mem_mb;
    if (key == "pids")    return &g->pids_max;
    if (key == "timeout") return &g->timeout_ms;
    return nullptr;
}

static char* str_out(const std::string& val) {
    char* out = aiml_str_alloc(val.size());
    if (!val.empty()) memcpy(out, val.data(), val.size());
    return out;
}

// Lanes dieser Pipeline, `exclude` schon abgezogen (§4.3).
static std::vector<Bindings> live_lanes(PPipeline* pp) {
    std::vector<Bindings> all;
    expand(pp->params, 0, Bindings{}, all);
    std::vector<Bindings> out;
    for (const auto& b : all) if (!excluded(pp->excludes, b)) out.push_back(b);
    return out;
}

void aiml_stage_set_attr(void* g, const char* key, const char* val) {
    if (!g || !key || !val) return;
    if (std::string* slot = stage_text(static_cast<PStage*>(g), key)) *slot = val;
    else LOG_ERROR(std::string("stage.set_attr: unbekannter Schluessel '") + key + "'");
}

void aiml_stage_set_limit(void* g, const char* key, int32_t value) {
    if (!g || !key) return;
    if (int32_t* slot = stage_num(static_cast<PStage*>(g), key)) *slot = value;
    else LOG_ERROR(std::string("stage.set_limit: unbekannter Schluessel '") + key + "'");
}

char* aiml_stage_attr(void* g, const char* key) {
    if (!g || !key) return aiml_str_alloc(0);
    std::string* slot = stage_text(static_cast<PStage*>(g), key);
    return str_out(slot ? *slot : std::string());
}

int32_t aiml_stage_limit(void* g, const char* key) {
    if (!g || !key) return 0;
    int32_t* slot = stage_num(static_cast<PStage*>(g), key);
    return slot ? *slot : 0;
}

int32_t aiml_stage_step_count(void* g) {
    return g ? (int32_t)static_cast<PStage*>(g)->steps.size() : 0;
}

// nullptr bei schiefem Index: der Aufrufer (builtin_registry) wirft daraufhin.
void* aiml_stage_step_at(void* g, int32_t idx) {
    if (!g) return nullptr;
    auto* st = static_cast<PStage*>(g);
    if (idx < 0 || idx >= (int32_t)st->steps.size()) return nullptr;
    return &st->steps[(size_t)idx];
}

char* aiml_step_attr(void* s, const char* key) {
    if (!s || !key) return aiml_str_alloc(0);
    auto* st = static_cast<PStep*>(s);
    const std::string k(key);
    if (k == "name")    return str_out(st->name);
    if (k == "cache")   return str_out(st->cache == 2 ? "pure" : (st->cache == 1 ? "resolved" : "opaque"));
    if (k == "per")     return str_out(st->per == 1 ? "once" : "lane");
    return str_out(std::string());
}

int32_t aiml_step_needs_count(void* s) {
    return s ? (int32_t)static_cast<PStep*>(s)->needs.size() : 0;
}

char* aiml_step_need_at(void* s, int32_t idx) {
    if (!s) return aiml_str_alloc(0);
    auto* st = static_cast<PStep*>(s);
    if (idx < 0 || idx >= (int32_t)st->needs.size()) {
        LOG_ERROR("step.need_at: Index " + std::to_string(idx) + " liegt ausserhalb");
        return aiml_str_alloc(0);
    }
    return str_out(st->needs[(size_t)idx]);
}

void aiml_pipeline_add_push(void* p, const char* from, const char* to, const char* tag) {
    if (!p || !from || !to) return;
    PPush push;
    push.from = from;
    push.to   = to;
    push.tag  = tag ? tag : "";
    static_cast<PPipeline*>(p)->pushes.push_back(push);
}

char* aiml_pipeline_name_of(void* p) {
    return p ? str_out(static_cast<PPipeline*>(p)->name) : aiml_str_alloc(0);
}

int32_t aiml_pipeline_stage_count(void* p) {
    return p ? (int32_t)static_cast<PPipeline*>(p)->stages.size() : 0;
}

void* aiml_pipeline_stage_at(void* p, int32_t idx) {
    if (!p) return nullptr;
    auto* pp = static_cast<PPipeline*>(p);
    if (idx < 0 || idx >= (int32_t)pp->stages.size()) return nullptr;
    return &pp->stages[(size_t)idx];
}

// Der Bindungstext der i-ten Lane — dieselbe Form wie im Beobachter (F62-152).
char* aiml_pipeline_lane_bindings(void* p, int32_t idx) {
    if (!p) return aiml_str_alloc(0);
    std::vector<Bindings> lanes = live_lanes(static_cast<PPipeline*>(p));
    if (idx < 0 || idx >= (int32_t)lanes.size()) {
        LOG_ERROR("pipeline.lane_bindings: Index " + std::to_string(idx) + " liegt ausserhalb");
        return aiml_str_alloc(0);
    }
    return str_out(bind_text(lanes[(size_t)idx]));
}

int32_t aiml_pipeline_axis_count(void* p) {
    return p ? (int32_t)static_cast<PPipeline*>(p)->params.size() : 0;
}

char* aiml_pipeline_axis_name(void* p, int32_t idx) {
    if (!p) return aiml_str_alloc(0);
    auto* pp = static_cast<PPipeline*>(p);
    if (idx < 0 || idx >= (int32_t)pp->params.size()) {
        LOG_ERROR("pipeline.axis_name: Index " + std::to_string(idx) + " liegt ausserhalb");
        return aiml_str_alloc(0);
    }
    return str_out(pp->params[(size_t)idx].first);
}

char* aiml_pipeline_axis_type(void* p, int32_t idx) {
    if (!p) return aiml_str_alloc(0);
    auto* pp = static_cast<PPipeline*>(p);
    if (idx < 0 || idx >= (int32_t)pp->params.size()) return aiml_str_alloc(0);
    auto it = pp->param_types.find(pp->params[(size_t)idx].first);
    return str_out(it == pp->param_types.end() ? std::string("string") : it->second);
}

int32_t aiml_pipeline_axis_value_count(void* p, int32_t idx) {
    if (!p) return 0;
    auto* pp = static_cast<PPipeline*>(p);
    if (idx < 0 || idx >= (int32_t)pp->params.size()) return 0;
    return (int32_t)pp->params[(size_t)idx].second.size();
}

char* aiml_pipeline_axis_value(void* p, int32_t axis, int32_t vix) {
    if (!p) return aiml_str_alloc(0);
    auto* pp = static_cast<PPipeline*>(p);
    if (axis < 0 || axis >= (int32_t)pp->params.size()) return aiml_str_alloc(0);
    const auto& vals = pp->params[(size_t)axis].second;
    if (vix < 0 || vix >= (int32_t)vals.size()) {
        LOG_ERROR("pipeline.axis_value: Index " + std::to_string(vix) + " liegt ausserhalb");
        return aiml_str_alloc(0);
    }
    return str_out(vals[(size_t)vix]);
}

int32_t aiml_pipeline_push_count(void* p) {
    return p ? (int32_t)static_cast<PPipeline*>(p)->pushes.size() : 0;
}

char* aiml_pipeline_push_field(void* p, int32_t idx, const char* which) {
    if (!p || !which) return aiml_str_alloc(0);
    auto* pp = static_cast<PPipeline*>(p);
    if (idx < 0 || idx >= (int32_t)pp->pushes.size()) {
        LOG_ERROR("pipeline.push_*: Index " + std::to_string(idx) + " liegt ausserhalb");
        return aiml_str_alloc(0);
    }
    const PPush& push = pp->pushes[(size_t)idx];
    const std::string key(which);
    if (key == "from") return str_out(push.from);
    if (key == "to")   return str_out(push.to);
    return str_out(push.tag);
}

// spec/62 F62-124a: hier faellt die Ablehnung — beim LAUF, nicht beim
// Uebersetzen.  Die Meldung nennt drei Dinge: was verlangt wird, dass hier
// keine Laufzeit da ist, und den Weg dorthin (F62-124).
int32_t aiml_pipeline_local_refusal(void* p) {
    if (!p) return 0;
    auto* pp = static_cast<PPipeline*>(p);
    for (const auto& stg : pp->stages) {
        if (!stg.image.empty()) {
            LOG_ERROR("pipeline '" + pp->name + "': Stufe '" + stg.name + "' verlangt das Abbild '"
                      + stg.image + "' (spec/62 §18.1) — ein lokaler Lauf hat keine Laufzeit, "
                      "die eine Wurzel bereitstellt.  Weg: ueber einen Wirt fahren, der Container "
                      "kann (cosmos doc/spec/110).");
            return -4;
        }
    }
    if (!pp->pushes.empty()) {
        LOG_ERROR("pipeline '" + pp->name + "': `push:` verlangt einen Ort, an den ein Abbild "
                  "geschrieben wird (spec/62 §18.1) — ein lokaler Lauf hat keinen.  Weg: ueber "
                  "einen Wirt fahren, der Container kann (cosmos doc/spec/110).");
        return -4;
    }
    return 0;
}

} // extern "C"
