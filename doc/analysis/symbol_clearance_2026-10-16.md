# Konsole im Wirtsprozess und Zugriffsstufen (Level) für Symbole

Stand: 16.10.2026. Quellcodebasierte Architekturprüfung, ausgelöst von der cosmos-Frage
*„eine Konsole für den Server selbst, pro Nutzer ein Thread oder ein eigener JIT — sind die
Symbole des Servers dort verfügbar, und kann man jedem Symbol einen Level geben?"*
Keine Implementierung. Die Messungen unten sind am gebauten `cosmosd` und am Compiler-Quellcode
erhoben, nicht geschätzt.

> **Ergebnis als Spec:** `spec/65_symbol_clearance.md` (PROPOSAL).

## 1. Sind die Symbole des Wirts im JIT verfügbar?

Heute **nein**, und zwar aus einem einzigen Grund: dem fehlenden Linker-Flag.

| Messung am gebauten `cosmosd` | Wert |
|---|---|
| `nm cosmosd` → aiml-Funktionen (Typ `T`) | **38** (`StepScheduler_claim`, `StepScheduler_report`, …) |
| `nm -D cosmosd` → davon im dynamischen Verzeichnis | **0** |
| Zustandssymbol | `__lib_sources_server_server_state_aiml_g_runtime` (Typ `B`) vorhanden, nicht exportiert |
| Link-Flags | `-Wl,-rpath,…`; **kein** `-rdynamic` |

Der JIT ruft `llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr)`
(`compiler.cpp:1353`, `:1406`, `:1817`, `:3057`) und löst damit alles auf, was in `.dynsym`
steht: die ganze `libaiml` (`aiml_*`), libc, OpenSSL. Die Funktionen des Wirts stehen dort
nicht. Der Zustand ist adressierbar, aber nicht auffindbar.

Zwei Wege, das zu ändern, mit sehr verschiedenen Folgen:

- **`-rdynamic`** — exportiert **alles**, auch jedes Interna. In einer Stunde gebaut und danach
  dauerhaft die Begründung, warum der Wirt nicht mehr umgebaut werden kann.
- **Export-Tabelle** — der Wirt meldet namentlich an, was er anbietet, über
  `DynamicLibrary::AddSymbol` (im Haus schon benutzt: `exec_impl.cpp:175`,
  `compiler.cpp:1375`, `:1879–1899`), die Skriptseite deklariert `- extern: "C"`
  (`tests/test_ffi.aiml`, Aufbereitung in `src/core/ast/extern_builder.cpp`). Das ist ein
  Vertrag, den man versionieren und prüfen kann.

## 2. Ein Thread pro Nutzer ist nicht möglich

`aiml_isolated_exec` baut einen frischen `AIMLCompiler` mit eigenem `LLVMContext` — aber es muss
den globalen Zustand des Aufrufers drumherum **retten und zurückschreiben**
(`exec_impl.cpp:335–362`):

```cpp
auto* saved_ctx = g_active_context;  auto* saved_ee = g_active_ee;
auto* saved_fptable = g_fptable_current;
{ AIMLCompiler iso; … rc = iso.compileAndRun(yaml_source); }
g_active_context = saved_ctx;  g_active_ee = saved_ee;  g_fptable_current = saved_fptable;
g_fptable = nullptr;  g_fptable_versions = nullptr;  g_func_names = nullptr;  g_func_count = 0;
```

Das funktioniert **nur sequenziell**. Zwei Threads, die gleichzeitig übersetzen, überschreiben
sich diese Globalen; der zweite schreibt beim Aufräumen den Zustand des ersten kaputt. Das ist
kein fehlendes Mutex, sondern fehlende Kontextbindung — dieselbe Wurzel wie der Kommentar in
`exec_impl.cpp` über mehrere isolierte Kontexte: *„brauchen aber ein rebindbares `ctx.module()`
— heute eine Referenz an 601 Stellen."*

**Folgerung: eine Konsole je Nutzer als eigener Prozess, nicht als Thread.** Das kostet nichts,
was gewollt war, und liefert zusätzlich Absturz-Eingrenzung, Speicher-/CPU-Grenze, `kill` und
die Einhaltung der KV-Einzelschreiber-Regel.

## 3. Die Symboltabelle des JIT hat keine Sichten

Entscheidend für die Level-Idee: `DynamicLibrary::AddSymbol` schreibt in eine **prozessweite**
LLVM-Tabelle ohne Mandantentrennung. Und `compileYamlChunk` schiebt vor jedem Bundle-Übersetzen
*alle* Funktionen des aktiven Kontexts hinein (`exec_impl.cpp:168–176`):

```cpp
for (auto& [nm, fi] : g_active_context->functions) {
    uint64_t addr = g_active_ee->getFunctionAddress(nm);
    if (addr) llvm::sys::DynamicLibrary::AddSymbol(nm, (void*)addr);
}
```

Ein Symbol, das für Nutzer A sichtbar wird, ist damit für **jeden** JIT im Prozess auflösbar.

**Folgerung: Level können nicht als Sichtbarkeit im JIT gebaut werden.** Sie müssen eine
**Namenszulassung beim Übersetzen** sein. Das ist keine Schwächung — es verlegt die Prüfung nur
an die Stelle, an der die Namen ohnehin aufgelöst werden.

## 4. Das Gerüst für Level steht größtenteils schon

`FunctionInfo` (`src/core/context.h:92`) trägt bereits alles, was eine Flussprüfung braucht:

```cpp
AccessLevel access = AccessLevel::Public;    // existiert — an FUNKTIONEN nie geprüft
std::set<std::string> referencedFuncs;       // der Aufrufgraph (spec/29_2 §16)
std::vector<BuiltinUse> referencedBuiltins;  // gerufene Builtins (spec/62 §10.2)
std::set<std::string> referencedGlobals;     // berührte Globale (spec/29_2 §22)
```

`AccessLevel` wird heute **nur an Klassenmitgliedern** durchgesetzt (`src/core/utils.cpp:40`,
`ast_compiler.cpp:6862/6956`); an Funktionen ist das Feld toter Code. Die drei `referenced*`-Mengen
werden fürs Bündeln und die Reinheitsprüfung ohnehin erhoben.

Daraus folgt der Teil, der die Idee von Deko zu einem Prüfwerkzeug macht: nicht nur der Zugriff
muss geprüft werden, sondern der **Privilegienfluss entlang der Aufrufkanten**. Ruft eine für
alle offene Funktion intern eine eingeschränkte, hat jeder Nutzer über den Umweg die
eingeschränkte Wirkung. Die statische Regel dagegen ist mit den vorhandenen Daten prüfbar; das
Vorbild für die Tabellenform ist `src/core/purity.h` (Vorgabe je Namensraum, Ausnahmen je
Funktion).

## 5. Die vier Fluchtwege

| Fluchtweg | Warum er Level umgeht | Gegenmittel |
|---|---|---|
| `- extern: "C"` | Das Skript deklariert das Symbol selbst; es findet **keine** Auflösung gegen die Level-Tabelle statt | `extern` nur für Namen der freigegebenen Export-Liste (`extern_builder.cpp`) |
| `sys.exe`, `sys.lib_load`, `sys.bundle`, `db.open_kv`, `io.*`, `net.*` | Builtins, keine Kontext-Symbole — Umgehung in einer Zeile | Level **auch** im Builtin-Registry, als Tabelle nach `purity.h` |
| `ptr<T>` + `cast:` + Buffer | Adressen rechnen und schreiben erreicht in-process alles, auch `g_runtime` | Im eingeschränkten Dialekt keine Rohzeiger/Zeiger-Casts |
| verschachteltes `aiml_exec_code` | Ein Skript übersetzt ein neues — mit welcher Freigabe? | Freigabe wird **geerbt und ist nicht erhöhbar** |

## 6. Die Grenze, die bleibt

Level sind eine Prüfung beim Übersetzen, **keine Sandbox**. Sie verhindern Versehen, machen
Absicht lesbar und ergeben eine versionierbare API-Oberfläche. Gegen einen böswilligen Teilnehmer
im selben Prozess schützen sie nicht; dort hilft nur die Prozessgrenze und am Ende echte
Isolation (rootless/userns). Die bestehende Auflage — kein Sandkasten im MVP, also nur
vertrauenswürdige Beteiligte — wird durch Level **nicht** aufgehoben.

## 7. Und der Normalfall: ein Binary, kein Bedarf

Einwand des Users (16.10.2026) und Vorgabe für die Spec: normalerweise läuft **ein** Binary von
**einem** Urheber; dort ist so etwas schlichtweg nicht nötig. Level sind deshalb keine Eigenschaft
eines Programms, sondern einer **Grenze** zwischen einem Wirt und fremdem Code, der in ihn
eintritt. Ohne diese Grenze darf das Merkmal nicht bloß abgeschaltet sein — es darf gar nicht
vorkommen: keine Annotation, kein Durchlauf, keine Diagnose, keine Laufzeitkosten, nichts zu
lernen. Die Spec beginnt daher mit diesem Satz und nicht mit der Mechanik.

## 8. Empfohlene Reihenfolge

1. Konsole als **eigener Prozess** je Nutzer, über die vorhandenen beglaubigten RPCs. Deckt die
   genannten Fälle (Jobs erzeugen, Hooks, Einstellungen, Queue/Agenten) ohne `-rdynamic`, ohne
   zweiten KV-Schreiber, ohne Absturzrisiko für den Wirt.
2. Level als Sprachmittel mit drei Prüfstellen (Funktionsauflösung, Builtin-Registry,
   `extern`-Zulassung) **plus** Flussprüfung über die Aufrufkanten — `spec/65`.
3. Wirtsinternen JIT mit Zugriff auf eigene Symbole **nicht** bauen, solange 1 und 2 nicht stehen;
   dann über Export-Tabelle, nie über `-rdynamic`.

## 9. Nachtrag 16.10.2026: zwei Befunde beim Nachschärfen der Specs

Beim Durchgehen der `sys.exec`-Seite kamen zwei Dinge heraus, die vorher nirgends standen:

1. **`sys.get_arg` liefert im JIT-Lauf für jeden Index `""`.** `main(argc, argv)` schreibt die
   Globalen `__argv`/`__argc` — das passiert nur im AOT-Pfad (`compiler.cpp`). In einem
   `aiml -r`-Lauf schreibt sie niemand, `__argv` bleibt null, und `SysGetArg` nimmt den
   Sicherheitszweig (der Kommentar dort lautet wörtlich *„if argv is null (JIT)"*). Ein Programm,
   das seine eigenen Argumente liest, verhält sich im JIT also anders als gebaut. Nachgetragen in
   `spec/23_system.md`.

2. **Der isolierte Objekt-Cache hat im AOT-Fall keinen Versionsschlüssel.**
   `aiml_isolated_exec` übernimmt `compilerPath` nur `if (g_active_context)`; im AOT ist der
   null, `compilerBinaryRev()` liefert `"unknown"`, `cacheRevDir()` landet in `rev_unknown`.
   Übersetzte Skripte einer Konsole würden damit unter einem Schlüssel liegen, der
   Compiler-Versionen nicht unterscheidet. Nachgetragen in `spec/29_2_sys_exec.md` §12, mit der
   Richtung: Schlüssel an die Revision von `libaiml.so` hängen, und ein fehlender Schlüssel soll
   den Cache **abschalten** statt einen gemeinsamen Eimer aufzumachen.

Dazu eine Richtigstellung in `spec/29_2` §3.2: der isolierte Kontext war dort als *„complete
sandboxing"* und *„No connection to the host program"* beschrieben. Das widersprach dem Ergebnis
dieser Analyse (§3, §6) direkt — gleicher Prozess, gleicher Adressraum, gleiche prozessweite
Symboltabelle. Hätte spec/65 sich auf diese Formulierung gestützt, wäre die Konsole als
„gesandboxt" gebaut worden.

**Folge für die Reihenfolge in §8:** Punkt 1 (Konsole als eigener Prozess) bleibt; vor Punkt 3
gehört der Cache-Schlüssel behoben, weil der isolierte Kontext im AOT-Wirt der einzige verfügbare
Pfad ist (spec/65 F65-25).

## 10. Nachtrag: §2 ist gemessen, nicht nur gelesen

Die Aussage aus §2 („ein Thread pro Nutzer ist nicht möglich") stand zunächst auf dem Lesen des
Codes. Sie ist inzwischen **gemessen**, und zwar von einem Test, der genau dafür existiert:

`tests/test_concurrent_isolated_exec.aiml` lässt vier Threads je drei **verschiedene** Bundles
übersetzen und ausführen (verschiedene Quelltexte, damit kein Cache den Vorgang verdeckt). Sein
Kopf nennt sich selbst „die Messung" zu `spec/62` F62-99 — jener Anforderung, die das Übersetzen
serialisiert, *solange die Antwort unbekannt ist*. Erwarteter Rückgabewert: 0.

Gemessen am 16.10.2026 auf einem `--clean`-Vollbau: **SIGSEGV (139)**. Im ganzen Korpus (846
Tests, 730 bestanden, 78 übersprungen) ist das der **einzige** Fehlschlag außerhalb der zwei
spec-vor-Code-Labore (`tests/closures/` 21, `tests/clearance/` 16).

Damit ist die Antwort nicht mehr „wäre der nächste Schritt", sondern: gleichzeitiges isoliertes
Übersetzen stürzt heute ab. Die Serialisierung in spec/62 F62-99 ist also keine Vorsichtsmaßnahme
mehr, sondern die einzig mögliche Form — und `spec/65` F65-25 (ein Prozess je Nutzer) steht auf
einer Messung.

## 11. Zwei Infrastruktur-Befunde am Rand

1. **Fünf Korpus-Tests laufen seit immer nicht.** `run_all_tests.sh` sammelt mit
   `find tests/ -name "*.aiml" | grep -v "build" | grep -v "out" | …`. Die Filter sollen die
   Verzeichnisse `build/` und `out/` ausschließen, treffen aber jeden **Namen**, der die
   Zeichenfolge enthält — und „la**out**" wie „r**out**es" enthalten sie:
   `tests/lifetime/45_store_view_outparam.aiml`, `tests/test_event_system_layout_repro.aiml`,
   `tests/test_http_routes.aiml`, `tests/test_layout_helpers.aiml`,
   `tests/test_ptr_i32_out_param.aiml`. Behebung: Pfad-Komponenten ausschließen
   (`-not -path "*/out/*"`), nicht Teilzeichenfolgen.
2. **Fehlerhafte `sys.exe`-Argumente stürzen den Compiler ab, statt zu diagnostizieren.** Ein
   `call: [sys.exe, "id", []]` — erstes Argument ein String statt `list<string>` (spec/23 §5.1) —
   endete in SIGSEGV. Das war ein Fehler in einer meiner Proben, aber ein Absturz ist die falsche
   Antwort darauf; erwartet wäre „lieber laut scheitern" mit einer Typmeldung.

### Korrektur zu §10 (17.10.2026)

Die Formulierung in §10 war zu stark. Nachgemessen: der Absturz trat **einmal** im vollen
Korpuslauf auf; danach lief derselbe Test im nächsten vollen Lauf und in **sechs** aufeinander
folgenden Einzelläufen durch. Der Fehler ist also **sporadisch und lastabhängig**, nicht
deterministisch.

Das ist die unangenehmere Sorte: ein Test, der sechs von sieben Mal grün ist, verleitet zu dem
Schluss, gleichzeitiges isoliertes Übersetzen sei in Ordnung. Es ist nicht in Ordnung — es ist
unbewiesen, und ein beobachteter Absturz genügt, um die Serialisierung aus spec/62 F62-99 und
die Ein-Prozess-Regel (spec/65 F65-25) beizubehalten. In spec/65 entsprechend korrigiert.

## 12. ⚠️ ANGEHALTEN: Spec-vs-Korpus-Konflikt beim wiederhergestellten `free:`

Nachdem `free` auf oberster Ebene wieder geroutet wird (§11 Punkt 1 war nur die halbe
Geschichte), läuft es dort tatsächlich — und **5 von 8** betroffenen Tests scheitern nun mit
derselben, korrekten Diagnose:

```
tests/test_oo.aiml:55: free of a ref<T> ('var_c'):
    a borrow never owns and must not be freed (§10).
```

Das Muster im Korpus:

```yaml
- decl: [box, ref<ListBox>, { new: ListBox }]    # Heap-Allokation an eine BORROW gebunden
...
- free: [box]                                    # abgelehnt: eine Borrow besitzt nie
```

Betroffen: `test_oo.aiml:55`, `test_chan_class_member.aiml:37`,
`test_buffer_class_member_heap_init_repro.aiml:20`,
`test_list_class_member_heap_init_repro.aiml:21`,
`test_map_class_member_heap_init_repro.aiml:28`.

**Beide Zustände sind falsch, nicht nur der neue.** Vorher wurde das `free` verworfen: die
Allokation leakte, und der Test war grün. Jetzt lehnt der Compiler die unzulässige Freigabe
korrekt ab, und der Test ist rot. Die Quelle war in beiden Fällen nicht spec-konform.

Die drei anderen Tests (`test_model_defaults`, `test_inherited_defaults`,
`test_chan_ptr_model_repro`) laufen jetzt durch und geben erstmals wirklich frei, was sie
freizugeben behaupten.

### Warum hier angehalten wird

Das ist genau der Konflikt, für den die Arbeitsregel „Spec durchsetzen, Tests spec-konform
anpassen, Spec/Impl NICHT aufweichen — **bei solchen Konflikten anhalten**" gilt. Zwei Wege,
und die Wahl gehört nicht mir:

1. **Tests spec-konform machen:** `ptr<ListBox>` statt `ref<ListBox>` für eine `new`-Allokation,
   die später freigegeben wird. Das entspricht dem Ziel („`new`/`ptr<Model>` ist der Heap-Pfad,
   `ref` ist eine Borrow") und lässt §10 unangetastet. Fünf Dateien.
2. **§10 lockern**, sodass eine `ref<T>`, die unmittelbar aus `{ new: T }` initialisiert wurde,
   als Eigentümer gilt. Das wäre ein Eingriff ins Besitzmodell und widerspricht der Auflage,
   Spec/Impl nicht aufzuweichen — ich halte es für den falschen Weg, nenne es aber, weil die
   Entscheidung sonst nicht vollständig beschrieben ist.

Bis zur Entscheidung bleibt der Routing-Fix drin: die fünf roten Tests sind **korrekte
Diagnosen auf fehlerhafter Quelle**, keine Compiler-Regression. Ein Rückbau wäre in einer
Minute gemacht, würde aber den Fehler wieder verstecken.
