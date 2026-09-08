# AIML-Fixplan: Heap-Lifetimes und Render-Speicher

Stand: 2026-09-07. Status: Plan; noch keine Implementierung.

Ausgangspunkt: [Memory-Messbefunde von 9rooms_v2](../../../9rooms_v2/doc/analysis/memory_checks/findings_2026-09-07.md).

## Ziel und Abgrenzung

Wiederholte Arbeit mit konstantem Nutzdatenbestand soll nach dem Warmup einen
begrenzten lebenden Heap behalten. Rückgaben, Aliase, Container und Runtime-Aufrufe
müssen dabei korrekt bleiben. Der Fix muss dem nativen Server und dem WASM-Client
zugutekommen.

Die bereits bearbeiteten Mirror-/Disconnect-Fixes bleiben erhalten. Änderungen an
9rooms-Match-Lifecycles, Schema-Migrationen und Produktivdatenbereinigung sind
separate Aufgaben. Dieser Plan umfasst keinen Daten-Wipe und kein Deployment.

## 1. Evidenz und noch offene Ursachen

- Der Bericht dokumentiert starkes Wachstum bei konstanten Lifecycle-Zählern.
  Das passt zu temporären Allokationen; die Zähler allein beweisen jedoch weder
  vollständiges Cleanup noch einen bestimmten Allokationspfad.
- `src/core/ast_compiler_stmt.cpp` erzeugt für Model-Value-Locals aus `kv.get_copy`
  explizit `aiml_ser_deep_copy`. Diese zusätzlichen Allokationen sind unabhängig
  vom KV-Decode-Cache. Cache-Eviction allein kann sie nicht zurückgewinnen.
- Probe E zeigt, dass auch dynamische Strings untersucht werden müssen. Probe D
  mit Literalen ist nur eine Kontrolle ohne dynamischen Stringinhalt.
- RA und RB rufen beide `populate` mit Interpolation auf. Gleiches Wachstum
  belegt deshalb kein separates Instantiate-/Destroy-Leck.
- `aiml_prefab_set_text` in `src/sys/prefab_runtime.cpp` weist einem `std::string`
  zu. Dieser verwaltet seinen bisherigen Inhalt selbst. Die Aussage im Bericht,
  dass dort der alte Text nicht freigegeben werde, ist damit nicht belegt.
  Der AIML-Eingabestring kann trotzdem als Temporary lecken.
- RC enthält Populate und Draw, aber keinen vollständigen Frame-Abschluss mit
  Buffer-Swap. Zusätzlicher RSS kann auch aus Treiberallokationen oder ausstehenden
  GPU-Kommandos stammen. Ein C++-Renderer-Leck braucht einen isolierten Nachweis.
- Die vorhandene Render-Probe steht beim Review auf 300 Iterationen, während
  der Bericht 20000 nennt. Quellstand, Binary und Parameter müssen zusammenpassen.

## 2. Arbeitspaket A: Reproduzierbare Baselines

Vor Compileränderungen die vorhandenen Proben korrigieren und isolieren:

1. `tests/test_mem_leak_probe.aiml`: Probe C auf konstante Payload-Größe umstellen
   (bestehenden Wert ersetzen statt pro Iteration anhängen). Proben einzeln in
   eigenen Prozessen ausführen. Ergebnisse nutzen, damit Optimierung die Arbeit
   nicht entfernt. Temporäres DB-Verzeichnis pro Lauf verwenden; keine festen
   rekursiv gelöschten Pfade und keine Produktivdaten.
2. A/B/D/E ergänzen um nackte Interpolation ohne Container, wiederverwendete
   dynamische Strings, Models mit ausschließlich skalaren Feldern, verschachtelte
   Models sowie getrenntes Decode und Copy.
3. `tests/test_mem_render_probe.aiml`: Populate ohne Renderer, `set_text` mit
   Literalen und wiederverwendetem Text, Instantiate/Destroy ohne Populate, Draw
   einer unveränderten Szene und vollständige Frames mit Swap getrennt messen.
   Textlänge und Zeichenmenge begrenzen; Font-/Glyph-Warmup vor der Messung.
4. Native Debug-Binaries mit ASan/LSan und/oder Heap-Profiler untersuchen.
   RSS ergänzen um lebende Allokationen mit Stacks, anonyme und dateibasierte
   Speicherseiten sowie relevante Objektzähler. Im WASM-Client den benutzten
   Allocator-Heap getrennt von der WebAssembly.Memory-Kapazität erfassen.
5. Warmup und mehrere gleich große Batches messen. Compiler-/Runtime-Version,
   Buildflags, Parameter und Ergebnisse gemeinsam speichern. Saubere und
   inkompatible Test-KVs getrennt untersuchen.

Abnahme: Für jede als Leak eingestufte Quelle existiert ein Allokationsstack und
ein erklärter fehlender Freigabepfad. RSS-Wachstum allein reicht nicht für eine
Zuschreibung an KV, `set_text` oder Draw.

## 3. Arbeitspaket B: Ownership-Vertrag festlegen

Vor automatischem Free die geltenden Regeln in `spec/10_collections.md`,
`spec/15_models.md` und `spec/45_heap_serialization.md` mit der Implementierung
abgleichen. Für Compiler und Runtime eine gemeinsame Tabelle erstellen:

| Herkunft / Operation | Zu klärender bzw. durchzusetzender Vertrag |
|---|---|
| Stringliteral | Statisch; niemals freigeben |
| Interpolation / String-returnende Runtime | Besitzender Rückgabewert oder ausdrücklich geliehene Sicht |
| Model-Deep-Copy | Ziel besitzt die kopierten Heap-Felder |
| `kv.get`, `list.get`, Feldreferenz | Geliehene Sicht; Besitzer und Invalidierung dokumentieren |
| `kv.get_copy` → Model-Local | Cache-Zwischenwert und zusätzliche unabhängige Local-Kopie unterscheiden |
| Zuweisung, Parameter, `push`, `set` | Bestehenden Aliasvertrag erhalten; Copy, Retain oder Move eindeutig festlegen |
| `set_text` | Runtime kopiert synchron; Lebensdauer der Eingabe nach dem Call separat behandeln |
| Return / dauerhafte Speicherung | Wert überlebt den Erzeugerscope durch korrekten Besitztransfer oder geteilten Besitz |
| Pointer, Callbacks, FFI | Borrowed, owned und escaping ausdrücklich unterscheiden |

Bevorzugter Ansatz: Ownership-Metadaten im Codegen plus typspezifische
Copy-/Move-/Drop-Operationen. Ob gemeinsam genutzte Daten Retain/Release benötigen,
hängt von den bestehenden Aliasregeln ab. Nicht stillschweigend alle Zuweisungen
in Deep-Copies verwandeln. Erforderliche Semantik-/ABI-Änderungen vor Umsetzung
gesondert beschreiben.

Entscheidungstor: Ein Scope-Destructor ist erst zulässig, wenn die Entweichpfade
des Werts geklärt sind. Kein pauschales `aiml_ser_free_decoded` für beliebige
Models: Literal-, Borrowed- und Owned-Felder sowie flache Kopien können sonst
Double-Free oder Use-After-Free auslösen. Eine bloße Owned-Markierung am gesamten
Model genügt bei gemischten oder später ersetzten Feldern nicht.

Abnahme: Locals, Temporaries, Parameter, Felder, Container, Rückgaben und
Runtime-Grenzen sind abgedeckt. Ungeklärte Fälle werden nicht unsicher automatisch
freigegeben; gegebenenfalls ist eine gezielte Compilerdiagnose erforderlich.

## 4. Arbeitspaket C: Lifetime-Codegen schrittweise implementieren

Betroffene Einstiegspunkte: `src/core/context.h`, `ast_compiler_stmt.cpp`,
`ast_compiler_expr.cpp`, `ast_compiler_call.cpp`,
`src/core/ast/control_flow_builder.*`, `src/core/ast/function_builder.*`,
`list_impl.*`, `builtin_registry.cpp` und String-/Serialisierungsruntime.

1. Besitz und Initialisierungszustand an Symbolen und Ausdrucksergebnissen
   führen. AST-Form oder LLVM-Pointertyp allein unterscheiden Ownership nicht.
2. Temporaries bis zum Ende ihres vollständigen Ausdrucks am Leben halten.
   Nach synchron kopierenden Calls freigeben; bei Speicherung/Return übertragen
   oder korrekt teilen. Verschachtelte Calls und mehrere Argumente abdecken.
3. Typspezifisches Drop für owned Strings, Listen, Maps, Buffer und rekursive
   Model-Felder erzeugen. Literale und geliehene Pointer ausnehmen. Ownership von
   Decode-Ergebnissen mit den regulären Runtime-Containern abstimmen.
4. Cleanup in umgekehrter Initialisierungsreihenfolge an allen Scope-Ausgängen:
   normaler Blockausgang, jede Schleifeniteration, `break`, `continue`, sämtliche
   Returns und unterstützte Exception-/Unwind-Pfade. Teilinitialisierte und
   bereits bewegte Werte sicher behandeln.
5. Rückgabewerte vor Cleanup sichern: direkte und mehrstufige Model-Rückgaben,
   Rückgabe eines Felds sowie Container-Rückgaben. Der Callee darf übertragenen
   Besitz nicht erneut zerstören.
6. Überschreiben behandeln: RHS zuerst sicher auswerten, bisherigen Besitz
   korrekt freigeben, neuen übernehmen. Selbstzuweisung und Referenzen auf alte
   Felder berücksichtigen. `push/set/remove/clear` und Container-Wachstum müssen
   denselben Ownership-Vertrag erfüllen wie Locals.
7. Explizites `free` integrieren, damit keine zweite automatische Freigabe folgt.
   Escapende Stackadressen erkennen. Pointer, Interfaces und Klasseninstanzen
   nicht allein aufgrund ihrer Repräsentation als besitzende Models behandeln.

Abnahme (verbindlich): Die isolierten nativen Proben müssen unter ASan/LSan
bestehen: keine UAF, Invalid-/Double-Free und keine verlorenen eigenen
Allokationen. N/2N-Messungen des lebenden Heaps ergänzen diese Prüfung, ersetzen
sie aber nicht. Sinkender oder flacher RSS allein gilt nicht als Abnahme.
Zurückgegebene und gespeicherte Werte bleiben nach dem Erzeugerscope gültig;
gleichwertige WASM-Proben bestehen. Nicht verfügbare Sanitizer bedeuten eine
offene Abnahme, keinen stillschweigenden Ersatz durch reine RSS-Messung.

## 5. Arbeitspaket D: Bestätigte Renderer-Lecks separat reparieren

Dieses Paket kann nach A unabhängig vom größeren Compilerpaket umgesetzt werden,
sofern die Proben ein Runtime-Leck belegen:

- In `src/sys/prefab_runtime.cpp`, `ui_runtime.cpp` und aufgerufenen Font-/GL-Pfaden
  den gemessenen Allokationsstacks folgen. Temporäre Layout-Daten über RAII
  freigeben; bestehende String-/Vector-Destructoren nicht umgehen.
- Dauerhafte Text-/Glyph-/Texture-Caches von verlorenen Allokationen trennen.
  Falls wechselnde Inhalte unbeschränkt akkumulieren, Grenzen und Invalidierung
  festlegen. GPU-Ressourcen gesondert erfassen.
- Destroy muss Instanz, Kinder und Registrierungen bereinigen. Während Draw
  verzögertes Destroy erst am sicheren Frame-Ende abschließen. Gemeinsam genutzte
  Fonts und Texturen dürfen nicht pro Instanz gelöscht werden.
- Kein `free(text)` in `set_text`: Die Eingabe kann ein Literal oder eine
  geliehene Adresse sein. Ein dort sichtbares AIML-Temporary gehört zu Paket C.

Abnahme: Unveränderte Szene mit vollständigem Frame-Zyklus, Textwechsel und
Instantiate/Destroy zeigen nach Warmup keinen linearen Anstieg lebender Ressourcen.
Draw ohne Swap bleibt eine Diagnosekontrolle.

## 6. Verifikation und Integration

Gezielte Regressionen für:

- Literale und dynamische Strings; nested Models/Listen/Maps; leer/null;
  Copy/Move/Self-Assignment; Überschreiben; `push` mit anschließender Nutzung des
  Originals; `remove/clear`; borrowed `get`; Copy → Mutation → KV-Writeback.
- Direkte und mehrstufige Rückgaben, mehrere Return-Zweige, `break/continue`,
  Exceptions und Teilinitialisierung, explizites Free sowie Runtime-Calls mit
  temporären und gespeicherten Argumenten.
- Speicherproben A/B/C/D/E und isolierte Renderer-Proben mit konstantem Bestand.
  Nach Warmup Batches mit N und 2N vergleichen: kein proportional zu N wachsender
  eigener Live-Heap. Heap-/WASM-Kapazität und RSS müssen nach Free nicht sinken.

Nach bestandenen gezielten Checks relevante vorhandene Compiler-, Collections-,
Serialisierungs-, Mirror- und Prefab-Tests durchführen. Vor Abschluss des
Compilerpakets die vollständige Suite ausführen; bestehende Fehler getrennt
dokumentieren. Nicht nach jedem kleinen Schritt die komplette Suite wiederholen.

Native Runtime und WASM-Runtime sowie beide 9rooms-Binaries passend neu bauen.
Anschließend isolierte 9rooms-Messung mit frischer Test-DB: Menü, genau ein Match,
Match-Ende und Tab-Schließen. Gleiche Aktivität wie in der Baseline verwenden;
ein Vergleichslauf muss mindestens die zuvor beobachteten ungefähr 50 Minuten
abdecken. Speicher pro Tick/Poll/Frame und Lifecycle-Zähler gemeinsam auswerten.

## 7. Reihenfolge, Lieferung und mögliche Mitigation

Dringlichkeit und Entscheidungstore vor dem großen Compilerpaket:

1. **A und Decode-Gültigkeit zuerst:** Baselines isolieren und den Fehlervertrag
   aus 8.6.2 durch Runtime und Scheduler führen. Ungültige Blobs dürfen nicht als
   erfolgreich geladene Matches verarbeitet werden. Dies ist eine zwingende
   Voraussetzung für den Server-Nachmessungslauf.
2. **Server-Nachmessung als Gate vor C:** Die bereits im Scheduler verwendete
   get_copy_at-Mitigation ist umgesetzt, ihre Wirkung gegen Server-OOM aber noch
   unbestätigt. Nach erfülltem Decode-Gate mindestens die bisherigen ca. 50 Minuten
   mit konstantem Match-Bestand messen: RSS, Live-Heap und Lifecycle-Zähler.
   Saubere Testdaten und kontrollierte inkompatible Test-Blobs getrennt prüfen.
3. **Nach Messergebnis priorisieren:** Bei flachem Verlauf ist das akute
   OOM-Risiko für das gemessene Lastprofil entschärft; C wird sorgfältige
   Qualitätsarbeit ohne OOM-Zeitdruck. Bei weiterem Wachstum zuerst verbleibende
   Allokationsstacks isolieren. C nicht als unbestätigten Notfallumbau vorziehen.
4. **B, dann C:** Ownership-Vertrag und Kompatibilitätsprüfung vor Aktivierung
   automatischer Drops. C ist ein grundlegender Ausbau des Lifetime-Codegens,
   kein kleiner lokaler Fix. Bestehende Sprachsemantik bleibt erhalten; nur wenn
   eine Änderung nachweislich unvermeidbar ist, den konkreten Fall zuvor mit dem
   Nutzer klären. Regressionstests und längere Speicherläufe sind autorisiert.
5. **D und Integration:** Bestätigte Renderer-Fixes können nach A separat
   vorgezogen werden. Danach native/WASM-Integration und Vorher-/Nachher-Messung.

Jeder Teil erhält passende Nachweise und aktualisierte Spezifikation. Bestehende
uncommittete Nutzeränderungen, insbesondere in `mem_runtime.cpp` und
`mirror_runtime.cpp`, bleiben erhalten. Rücknahme einzelner Änderungen muss ohne
Zurücksetzen fremder Arbeit möglich sein.

`get_copy_at` ist bereits als Server-Mitigation im Scheduler eingesetzt:
Der Zielpuffer muss seinen bisherigen Heap freigeben und entwichene Aliase
ausschließen. Nur die Struct-Hülle wiederzuverwenden löst das Problem nicht.
Ein pauschaler Wechsel des Schedulers auf `kv.get` ist wegen KV-Invalidierung
ebenfalls kein sicherer Fix.

Der Plan setzt weder voraus, dass alle beobachteten Renderer-Zuwächse echte Lecks
sind, noch verspricht er, dass ein einzelner Model-Destructor sämtliche Client-
und Serverlecks behebt. Diese Abgrenzung wird durch die Baselines geprüft.

## 8. Konkrete Änderungen mit Zeilenangaben

Zeilenstand: 2026-09-07 beim Ergänzen geprüft. Bei Änderungen die genannten
Funktionsnamen als Suchanker nutzen. Helfernamen in den Vorschlägen sind Entwürfe,
keine schon vorhandenen APIs. Alle Vorschläge betreffen eine spätere Umsetzung.

### 8.1 Proben korrigieren — Paket A

| Datei / Zeile | Vorschlag |
|---|---|
| `tests/test_mem_leak_probe.aiml:56`, `probe_set` | Anhängen von `"more"` durch Ersetzen eines vorhandenen skalaren Elements ersetzen, etwa `idx: [mrec.nums, 0, ccnt]`. Payload bleibt konstant. |
| `tests/test_mem_leak_probe.aiml:139` | Probenwahl und Iterationen parametrisieren; Proben separat starten, Resultate verwenden und N/2N nach Warmup vergleichen. |
| `tests/test_mem_leak_probe.aiml:114` | Interpolation ohne Liste und Liste mit außerhalb der Schleife erzeugten dynamischen Strings als Kontrollen ergänzen. |
| `tests/test_mem_render_probe.aiml:26`, `populate` | Varianten mit Literalen und wiederverwendetem Text ergänzen; Interpolation von Textspeicherung trennen. |
| `tests/test_mem_render_probe.aiml:75`, `probe_frame` | Draw-only ohne Populate sowie vollständigen Frame mit vorhandenem Swap-/Frame-Ende-API separat messen. |
| `tests/test_mem_render_probe.aiml:103`, `:115` | Iterationen parametrisieren; `held` vor Window-/GL-Abbau zerstören. Font-/Scene-Abbau gemäß bestehendem API-Vertrag. |

### 8.2 String-Refcounting und Produzenten — B/C

- `src/sys/string_runtime.cpp:37`, `:74`, `:81`: `aiml_str_alloc/incref/decref`
  existieren bereits. Darauf aufbauen. Incref/Decref lesen derzeit einen
  Magic-Header vor beliebigen übergebenen Adressen; der Adressgrößencheck beweist
  keinen gültigen Speicher davor. Vor breiterem Einsatz sichere Managed-Herkunft
  im Codegen und für gemischte Runtime-Grenzen einen sicheren Registry-/Handle-
  Vertrag vorsehen. Registry bei nebenläufiger Nutzung absichern. Nicht beliebige
  Literale probeweise vor ihrer Adresse lesen.
- `src/sys/prefab_runtime.cpp:6021`, `aiml_prefab_text_value`: liefert `strdup`.
  Nach Konsumentenaudit auf `aiml_str_dup` umstellen und Resultat als owned
  Managed-String kennzeichnen. Dropdown-Stringrückgaben ab `:3839` mitprüfen.
  Vorhandenes `free(char*)` wäre danach ungültig und muss mitumgestellt werden.
- `src/core/ast_compiler_expr.cpp:43–68` und `src/core/string_utils.h:13`:
  Concat erzeugt neue Akkumulatoren. Ersetzten owned Akkumulator nach erfolgreichem
  Concat releasen, Endergebnis als owned Temporary weiterreichen. Zahl-zu-String-
  Segmente erfassen; Borrowed Segmente und Literale nicht freigeben.
- `src/core/ast_compiler_expr.cpp:1152–1164`: Lesen eines String-Listenelements
  führt bereits Incref aus. Ausdruck als owned erfassen und später releasen;
  kein zweites pauschales Retain bei `decl`. Bestehenden
  `tests/test_mirror_close_string_uaf.aiml` als Kontrolle erhalten.

### 8.3 Ownership-Metadaten und Scope-Cleanup — B/C

`src/core/context.h:27` (`Symbol`) um Ownership-/Initialisierungsinformationen
ergänzen; `:186` (`Scope`) um geordnete Cleanup-Liste. Die Variablen-Map speichert
keine Initialisierungsreihenfolge. Entwurf für gemeinsame Compiler-Helfer:

```cpp
enum class Ownership { Static, Borrowed, Owned, Shared };
registerCleanup(slot, type, initializedFlag);
emitDrop(slot, type);
emitCleanupsUntil(scopeBoundary);
transferOrRetainResult(source, destination);
```

Initialized/moved/maybe-initialized als Datenflusszustände auch an Ausdrücken
führen. Bedingte Initialisierung und Move benötigen gegebenenfalls Laufzeitflags;
ein einzelnes C++-bool beim Kompilieren reicht nicht. `is_byref_arg` berücksichtigen.
Gemischter Feldbesitz braucht einen einheitlichen Feldvertrag oder Metadaten;
eine Owned-Markierung nur am ganzen Model reicht nicht.

- `src/core/ast_compiler_stmt.cpp:1285`, `visitBlockStmt`: sichert/restauriert
  derzeit Symboltabelle. Cleanup-Frames betreten/verlassen; normalen Cleanup nur
  in noch nicht terminierten Basic Blocks emittieren.
- `:1362–1367`: Body-Locals vor While-Rücksprung freigeben, nicht erst nach der
  gesamten Schleife. Initialisierungs-/Move-Flags pro Iteration korrekt setzen.
- `:1190–1236`: Return-Ergebnis sichern, verlassene Scopes aufräumen, dann
  `ret`. Void-/implizite Returns mit erfassen; nichts hinter Terminatoren anfügen.
- `:1374`, `visitTry`: Cleanup der tatsächlich verlassenen Scopes auch im
  Exception-/Unwind-Pfad. Nur initialisierte Ressourcen freigeben. Unterstützte
  Break-/Continue-Emitter an dieselben Scope-Grenzen anbinden.
- Nicht allein Builder-Fassaden ändern: diese Statement-Visitor emittieren selbst
  Kontrollfluss und müssen die neuen Helfer tatsächlich verwenden.

### 8.4 Deep-Copy, Replace und Return — C

| Datei / Zeile | Eingriff nach geklärtem Ownership-Vertrag |
|---|---|
| `src/core/ast_compiler_stmt.cpp:586–607` | Nach erfolgreicher Local-Deep-Copy Besitz/Initialisierung markieren und Cleanup registrieren; KV-Cache-Slot nicht mitfreigeben. |
| `src/core/ast_compiler_stmt.cpp:862`, `:1104` | Weitere Deep-Copy-Zuweisungspfade auf belegte Ziele prüfen und gemeinsamen Replace-Helfer verwenden. |
| `src/core/ast_compiler_stmt.cpp:1219–1236` | Bestehende Return-Deep-Copy erhalten, Resultat an Caller übertragen und vom Callee-Cleanup ausnehmen. |
| `src/sys/ser_runtime.cpp:696`, `aiml_ser_deep_copy` | Vorbedingung „Ziel ist leer“ erhalten; checked Replace-Variante für belegte Ziele ergänzen. |
| `src/sys/ser_runtime.cpp:471`, `fre` | Nur für kompatibel besessene Decode-Felder nutzen; nach Listen-Free auch size/capacity nullen. Allgemeines Model-Drop braucht Feldvertrag. |

Entwurf für Replace, auch bei Selbstzuweisung und Quellaliasen:

```cpp
tmp = zeroInitialized<T>();
if (!tryDeepCopy(tmp, source)) {
    dropPartiallyInitialized(tmp);
    propagateError();             // bisheriges Ziel bleibt erhalten
}
drop(destination);
move(destination, tmp);           // tmp danach ohne eigenen Besitz
```

Aktuelle Deep-Copy liefert void: checked Variante oder entsprechender Fehlervertrag
nötig. Nicht zuerst Ziel freigeben, weil die Quelle auf dessen Felder zeigen kann.

### 8.5 Container-Elemente — C

`src/core/list_impl.cpp:56` (`push`), `:200` (`clear`), `:287` (`remove`):

- Push/Insert mit AIML-Elementtyp oder Lifetime-Descriptor versehen; Copy/Retain/
  Move nach Vertrag. LLVM-Pointertyp allein unterscheidet Stringbesitz nicht.
- Clear setzt aktuell nur size=0. Owned Elemente vorher droppen; Pufferkapazität
  darf bleiben. Sonst sind Elemente für spätere Freigabe nicht mehr erreichbar.
- Remove droppt genau das entfernte owned Element, verschiebt verbleibende Slots
  als Besitztransfer und neutralisiert letzten ungenutzten Slot. Verschobene
  Elemente nicht zusätzlich releasen.
- Container-Drop erst Elemente, dann Buffer. Borrowed Parameter nicht zerstören.
  Maps entsprechend prüfen. Flache Modelkopien aus at/get dürfen nicht zu zwei
  Besitzern desselben Heaps werden.

### 8.6 Vorhandenes get_copy_at absichern

Bereits vorhanden: `src/sys/db_runtime.cpp:1661` und
`src/core/ast_compiler_call.cpp:1832`. Die Mitigation in Abschnitt 7 ist inzwischen
ein Audit dieses vorhandenen Pfads, keine Aufforderung, ihn neu zu implementieren.

1. Kommentar ab `db_runtime.cpp:1653` korrigieren: „zero per-call allocation“ ist
   falsch; Decode allokiert Heap-Felder neu. Wiederverwendet wird die Struct-Hülle.
2. **Verbindliches Decode-Gate vor dem Server-Live-Test:** Missing key = 0 ist
   bereits vorhanden (db_runtime.cpp:1668/1672). Im gefundenen typed Pfad wird
   hingegen der Decode-Status ignoriert und danach return 1 ausgeführt.
   Präzisierung zum Review: aiml_ser_decode liefert im aktuellen Quellstand
   bereits int, nicht void (ser_runtime.h:84, ser_runtime.cpp:713): 0 bei Erfolg,
   -1 bei Fehler. Es fehlt die Weitergabe und Behandlung dieses Status.
   Vorschlag für get_copy_at: 1 = erfolgreich kopiert, 0 = Key fehlt,
   -1 = ungültiges Blob; den API-Vertrag in Spec und Codegen abgleichen.
   Scheduler/Wrapper dürfen ausschließlich bei 1 weiterarbeiten. Bei Fehler
   keinen teilweise dekodierten Zielwert lesen, ticken oder zurückschreiben;
   Teilallokationen sicher bereinigen und Zielzustand ausdrücklich definieren.
   Schema-Mismatch und strukturell korruptes Blob getrennt prüfen. Die drei
   berichteten Alt-Match-Keys als Testfall nachbilden; ungültige Keys für den
   aktuellen Datenstand markieren und vom regelmäßigen Tick ausschließen, mit
   definierter erneuter Prüfung nach Datenänderung. Kein Produktivdaten-Wipe.
3. Ziel als initialisierten eigenen Puffer verlangen, keinen KV-Cachealias.
   Codegen-Prüfung bei `ast_compiler_call.cpp:1832` präzisieren; unbekannte Aliase
   diagnostizieren oder über sicheren API-Vertrag ausschließen. Letzten Inhalt
   am Scope-Ende ebenfalls droppen.
4. Soll alter Zielinhalt bei Decode-Fehler erhalten bleiben, Replace aus 8.4
   verwenden. Aktuell wird er vor Decode gelöscht; Verhalten spezifizieren.

### 8.7 Renderer — konkrete Prüfstellen, bedingte Änderungen

- `src/sys/prefab_runtime.cpp:3662`, set_text: std::string-Zuweisung erhalten.
  Call-Vertrag „borrowed input, synchronously copied“ erfassen, damit owned
  Eingabe-Temporaries danach korrekt freigegeben werden. Kein free(text).
- `:4807`, draw_scene: Stacks für Layout, Renderlisten und GL getrennt messen.
  Lokale Vektoren haben RAII. Wiederverwendbare Scratch-Vektoren wären eine
  Allokationsoptimierung, kein pauschaler Leak-Fix; Reentrancy beachten.
- Pending-Delete-Sweep ab `:4875`: mit Instanz-/Kinderzählern vollständige
  Deregistrierung prüfen. Nur bei belegtem Restbestand fehlende Freigaben
  ergänzen; sicheren Frame-Ende-Zeitpunkt erhalten.

### 8.8 Abnahme und Aktivierungsreihenfolge

Strings: ausgeglichene Retain/Release und gültige Aliase nach Container-/Mirror-
Close. Models: A/B plus Return-/Loop-/Exception-Kontrollen. Container: E plus
Push/Clear/Remove mit erlaubten Aliasnutzungen. get_copy_at: fehlender Key,
inkompatibles Blob, wiederholtes Refill und finaler Drop. Renderer: isolierte
vollständige Frames. ASan/LSan sind für C verbindliche Abnahme; gleichwertige
WASM-Proben und Live-Heap-Messungen kommen hinzu. RSS allein genügt nicht.

Mit 8.1 und dem Vertrag aus B beginnen. Model-Drops erst zusammen mit den nötigen
Transfer-/Containerregeln aktivieren. Diese Zeilen sind keine unabhängigen
Einzeilen-Fixes.
