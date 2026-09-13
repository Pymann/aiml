# Spec 61: Gegenüberstellung mit der Implementierung

Stand: 2026-09-12. Volatiler Quelltext-Review; Zeilenangaben gelten für den gelesenen Arbeitsstand.

## Umfang und Aussagegrenzen

Spec 61 wurde vollständig gelesen. Die folgenden zehn Punkte entsprechen der zuvor besprochenen Spec-Prüfung. Gegengeprüft wurden relevante Compiler-, Ownership- und Runtime-Pfade, ergänzend die Verweise auf Collections und Heap-Serialisierung.

Es wurden keine Builds, Repro-Tests, Sanitizer-Läufe oder neuen Heap-Messungen durchgeführt. Aussagen über erzeugtes Verhalten sind statische Ableitungen aus dem Code. Die gemeldeten Raten von ca. 60 KB/min Server und 8 MB/min Client werden damit nicht quantitativ einzelnen Ursachen zugeordnet. Es wurden für diesen Report keine Implementierungen geändert und keine Git-Kommandos ausgeführt.

**Ergebnis:** Die Implementierung ist nicht einfach unbestimmt: viele Entscheidungen sind konkret programmiert. Sie sind jedoch über Analyzer, Drop-Registrierung und einzelne Lowering-Pfade verteilt und stimmen nicht überall überein. Eine eindeutige Spec allein würde die nachfolgenden Implementierungslücken nicht beheben.

## 1. Gegenüberstellung der zehn Spec-Punkte

Alle Codepfade sind relativ zum AIML-Projektroot angegeben. Die verlinkten Dateien sind vom Report aus erreichbar; Zeilennummern stehen separat, damit die Links auch im Markdown-Viewer funktionieren.

| Nr. | Spec-Problem | Tatsächlicher Code | Bewertung / notwendige Klärung |
|---|---|---|---|
| 1 | §3 erklärt lokale Werte zu Besitzern; §3.2 nimmt gewöhnliche Funktionsrückgaben aus. | `ownership_analyzer.cpp:36–114`, `freshOwned`: feste Initializer-Muster für no-init Lists/Models, Interpolation, Model-get_copy, Producer-Calls und bestimmte Boxen. Andere Initializer bleiben nicht-owned. `ast_compiler_stmt.cpp:355–523` pflegt die Drop-Klassifikation separat. | **Konkrete Heuristik, aber doppelte Wahrheit.** Analyzer und Drop-Registrierung müssen dieselbe Klassifikationsfunktion verwenden. Bereits beobachtete Abweichung: F1. |
| 2 | §4 sagt push/return konsumieren immer; §7 ergänzt Copy- und Branch-Ausnahmen. | `ast_compiler_call.cpp:1261–1337`: Model-push entscheidet anhand des registrierten Drops im obersten Cleanup-Frame. Member-Stores verwenden den Ownership-Plan. Calls/Returns konsumieren über `consumeLocalDrop`, `ast_compiler_stmt.cpp:1771`. | **Uneinheitliche Mechanismen.** Nicht sämtliche Transfers werden als einheitliche Operation im Datenfluss abgebildet. Der Helper entfernt Drop-Einträge aus allen Cleanup-Frames. |
| 3 | Borrow-Escape ist laut §3/§8 Fehler, laut §7 teilweise automatische Deep-Copy; Getter dürfen Views zurückgeben. | `ast_compiler_stmt.cpp:1003–1020`: Warnung für bestimmte Pointer-/Referenzparameter, die in this-Member gespeichert werden. Andere Memberpfade kopieren Models/Container. `visitRetStmt` enthält einen konservativen Cleanup-Verzicht für unklare Rückgabeherkunft. | **Teilimplementierung, kein allgemeines Escape-Verbot.** Borrow speichern, Wert kopieren und Borrow zurückgeben brauchen getrennte Verträge. |
| 4 | Ersetzung: RHS auswerten, Ziel freigeben, dann übernehmen – unzureichend bei Aliasing/Copy-Fehlern. | Neu vorhanden: zentraler Member-Free in `ast_compiler_stmt.cpp:1373–1385`; `emitContainerFieldFree`, `ast_compiler.cpp:3155`, prüft Adressgleichheit. `emitContainerFieldDeepCopy:3243` nullt anschließend das Ziel. Lokale Model-Zuweisung in `ast_compiler_stmt.cpp:1044–1070` beschreibt weiterhin fehlende Freigabe des alten Ziel-Heaps. | **Verbesserung bei Member-Ersetzung, aber keine allgemeine alias-sichere Ersetzung.** F2 betrifft den neuen Pfad; lokale Zuweisung bleibt separat. Fallible Copy/Commit-Vertrag fehlt. |
| 5 | persistent benennt keinen tatsächlichen länger lebenden Owner oder Freigabeweg. | `freshOwned:37` und Drop-Registrierung schließen persistent aus. Ein konkreter Owner wird dadurch nicht gebunden. Spätere Kopien hängen vom Operations-/Typpfad ab. | **Eindeutig als Auto-Drop-Ausnahme**, nicht als vollständiger Lebenszyklus implementiert. Ein persistent-Original kann ohne später erreichbaren Eigentümer zurückbleiben. |
| 6 | consumed wird anhand der Pointer-Repräsentation akzeptiert, obwohl diese keinen Besitz/Destruktor garantiert. | Parameterprüfung in `ast_compiler.cpp`, Bereich ab 4737: Pointer-/Byref-Prüfung und String-Parameter-Drop. Caller in `ast_compiler_call.cpp:420` entfernt lokale Drops; dieser Pfad enthält keine allgemeine Herkunftsprüfung des Arguments. | **Strukturell eindeutig, semantisch unvollständig.** Borrow-Argumente, nicht kopierbare Handles und typgerechte Destruktoren brauchen explizite Regeln. |
| 7 | Kein Reference Counting laut §1, aber RC-Strings und decref in §3/§3.2. | `string_runtime.cpp:15–94`: RC-Header, alloc/dup, incref/decref. | **Hier ist der Code eindeutig und die Spec widersprüchlich.** Vollständige Erfassung aller Erzeuger/Transfers bleibt ein Implementierungsthema. Rohe malloc/strdup-Pointer haben nicht denselben Vertrag wie RC-Strings. |
| 8 | Ziel-Lebensdauer und Dominanz werden mit syntaktischen Kategorien vermischt. | Model-push verwendet das oberste Cleanup-Frame. `ownership_analyzer.cpp:184–214` entscheidet Member-Transfer anhand der Quellen-Origin; eine entsprechende Ziel-Lebensdauer-/Same-Block-Prüfung fehlt dort. | **Spec-Unschärfe spiegelt sich im Code.** Die Dominanzregel gilt nicht einheitlich für Member-Stores; die tatsächliche Lebensdauer des Zielcontainers wird nicht allgemein verglichen. |
| 9 | producer ist hauptsächlich für Call-initialisierte Deklarationen beschrieben. | Drop-Erkennung in `ast_compiler_stmt.cpp` und `freshOwned` matcht den letzten Funktionsnamensteil gegen `ctx.ownsReturnFns`. Allgemeine Owned-Expression-Temporaries sind damit nicht abgedeckt; Interpolations-Temporaries haben ein separates System. | **Implementierungslücke.** Ignorierte/nestende Producer-Calls und Transfers ohne lokale Deklaration brauchen einen Ausdrucksvertrag. Kurzname statt aufgelöster Funktionsidentität birgt Namenskollisionsrisiko. |
| 10 | Leak-Lint würde bereits bei irgendeinem syntaktischen Transfer schweigen. | Kein entsprechender Lint im geprüften Compiler-Code gefunden. `ownership_analyzer.cpp:256` lässt allgemeine Call-Effekte ausdrücklich außerhalb der Flow-Analyse; Moves via push/return erfolgen beim Lowering. | **Noch nicht implementiert.** Bestehende Flow-Fakten sind nicht ausreichend, um alle tatsächlichen Transfers und Exits einfach abzufragen. |

## 2. Konkrete Befunde

### F1 — P1: Neue UI-String-Drops werden vom Ownership-Plan unterdrückt

**Code:** [Drop-Registrierung](../../src/core/ast_compiler_stmt.cpp), Zeilen 448–474; [Analyzer](../../src/core/ownership_analyzer.cpp), Zeilen 55–58; [Drop-Gate](../../src/core/ownership_flow.cpp), `mayDrop`; [Runtime](../../src/sys/prefab_runtime.cpp), `aiml_prefab_text_value` / `aiml_prefab_dropdown_value`.

Die Runtime liefert inzwischen `aiml_str_dup` statt rohem `strdup`. `visitDeclStmt` erkennt die kurzen Namen `text_value` und `dropdown_value` als owned String-Erzeuger und registriert einen Drop mit Ownership-ID. `freshOwned` erkennt bei Strings weiterhin ausschließlich Interpolation. Der lokale Wert erhält im Plan daher Borrowed statt Owned. `mayDrop` verlangt aber Owned + Live + Resource und verweigert den Drop.

**Auswirkung:** In einem normal analysierten Funktionsblock kann die neue Drop-Registrierung das Getter-Leak nicht beheben. Ein fehlender Plan kann ein anderes Verhalten erzeugen, weil der normale Block-Exit dann registrierte Drops ohne diesen Gate-Check emittiert.

**Vorschlag:** Gemeinsame Klassifikation mit aufgelöster Builtin-/Funktionsidentität statt zwei Listen kurzer Namen. Nicht den Gate-Check pauschal entfernen. Künftige Validierung: Getter-Ergebnis im normalen Funktionsblock und auf Return-Pfaden prüfen; derzeit kein Repro ausgeführt.

### F2 — P1: Selbstzuweisungs-Guard schützt nur Free, nicht die anschließende Kopie

**Code:** [Compiler](../../src/core/ast_compiler.cpp), `emitContainerFieldFree:3155`, Guard gegen `dstPtr == srcPtr`, sowie `emitContainerFieldDeepCopy:3243–3270`; [Member-Store](../../src/core/ast_compiler_stmt.cpp), Bereich 1373–1432.

Bei identischen Adressen überspringt `emitContainerFieldFree` die Freigabe. Der nachfolgende Deep-Copy-Helper nullt `dstPtr` jedoch unabhängig davon und liest erst danach `srcPtr`. Bei identischen Adressen ist damit auch die Quelle bereits genullt. Eine Member-Selbstzuweisung einer Liste kann diesen Pfad erreichen: die Quelle ist ein Member und wird als DeepCopy klassifiziert; der Plain-Local-Self-Assign-Shortcut greift dort nicht.

**Auswirkung laut Codepfad:** Datenverlust und Verlust des bisherigen Heap-Pointers bei einem erreichten identischen Quell-/Zielslot. Kein ausgeführter Laufzeit-Repro.

**Vorschlag:** Identische Quelle/Ziel als No-op über die gesamte Ersetzung behandeln. Für überlappende, aber nicht identische Quellen eine unabhängige Kopie vor der Ziel-Freigabe herstellen. Pointer-Gleichheit allein beweist keine Alias-Freiheit.

### F3 — P1: Transfer-/Branch-Zustand und Drop-Listen bleiben getrennt

**Code:** [Analyzer](../../src/core/ownership_analyzer.cpp), Member-Transfer um 184 und Call-Fallback um 256; [Codegen](../../src/core/ast_compiler_stmt.cpp), `consumeLocalDrop:1771`; [List-push](../../src/core/ast_compiler_call.cpp), Bereich 1261–1337.

Ein Member-Store kann einen Owned-Local als Move klassifizieren, ohne die Same-Block-Bedingung des Model-push zu prüfen. `consumeLocalDrop` entfernt passende Einträge aus sämtlichen Cleanup-Frames. Die Datenflussanalyse erfasst allgemeine Calls nicht als entsprechende Zustandsübergänge. Ein späteres `mayDrop` kann einen zuvor vollständig entfernten Drop-Eintrag nicht wiederherstellen.

**Auswirkung:** Bedingte Transfers und unterschiedliche Operationspfade bleiben eine Quelle von Under-Drop. Eine allgemeine Use-after-move-/Merge-Fehlerdiagnostik wird dadurch nicht ersetzt. Nicht jedes denkbare Branch-Beispiel wurde ausgeführt.

**Vorschlag:** Transferplan und Exit-Plan müssen die gemeinsame Entscheidungsgrundlage sein; lokales Erasen einer global verwendeten Cleanup-Liste darf keine pfadabhängige Entscheidung ersetzen.

### F4 — P2: Producer-Erkennung über kurze Namen statt Funktionsidentität

**Code:** [Analyzer](../../src/core/ownership_analyzer.cpp), `freshOwned`; [Deklarationen](../../src/core/ast_compiler_stmt.cpp), Model-/Box-/List-Producer-Erkennung; [Context](../../src/core/context.h), `ownsReturnFns`.

Die Erkennung trennt den Aufrufnamen am letzten Punkt und sucht den Rest im Producer-Set. Damit kann die Ownership einer gleichnamigen Methode eines anderen Empfängertyps zugerechnet werden. Für ein irrtümlich als Producer erkanntes Getter-Ergebnis wäre ein Drop auf fremden Besitz möglich.

**Status:** Namenskollisionsrisiko aus der Implementierung; keine konkrete 9rooms-Kollision und kein Laufzeitfehler in diesem Review nachgewiesen.

**Vorschlag:** Producer-Metadaten vom aufgelösten Callee bzw. der Interface-/Funktionssignatur beziehen. Dieselbe Identität in Analyzer und Codegen verwenden.

## 3. Ergänzende Lifecycle- und Statusbefunde

- **Drop-Reihenfolge:** Normaler Block-Exit und unterstützte Return-Pfade durchlaufen Cleanup-Frames/Einträge rückwärts. Das ist bereits konkret implementiert, nicht grundsätzlich offen.
- **Return-Cleanup:** `visitRetStmt` (ast_compiler_stmt.cpp ab 1590) schränkt Cleanup anhand von `safeToCleanup` ein. Bei unklarer Rückgabeherkunft werden Freigaben bewusst unterlassen. Das vollständige Ziel deterministischer Freigabe ist damit noch nicht erreicht.
- **Exceptions:** `visitTry` / `visitThrow` (dieselbe Datei ab 2055 / 2175) enthalten keinen vollständigen Ownership-Unwind. Auch der Analyzer behandelt Try-/Throw-Strukturen nicht wie seine Block-/If-/While-/Return-Pfade.
- **Container mit Boxen:** `aiml_list_free` (string_runtime.cpp ab 105) kennt Raw-, String- und Model-Elemente. Die Registrierung für `list<ptr<T>>` etabliert keinen allgemeinen rekursiven Owned-Box-Drop. Verschachtelte Container benötigen ebenfalls eine explizite Prüfung ihrer Deskriptoren und Drop-Kinds.
- **Consumed-Parameter:** Der Analyzer initialisiert Parameter als Borrowed; die String-Parameter-Drop-Registrierung ist ein eigener Pfad. Daraus ergibt sich keine allgemeine Ressourcen-Ownership für alle akzeptierten Handle-Typen.
- **Interfaces:** Die Statusbeschreibung in Spec 61 ist veraltet: Lifetime-Signaturinformationen, Interface-Dispatch-Transfers und eine Prüfung gegen inkompatible consumed-Overrides sind vorhanden. Das ist keine Behauptung vollständiger Interface-Ownership-Korrektheit.
- **Map-/andere Drop-Abdeckung:** Vorhandene Runtime-Free-Funktionen belegen nicht automatisch vollständige Registrierung und Aufrufe für jede lokale, temporäre oder Member-Form. Diesen Unterschied muss der Implementierungsstatus sichtbar machen.

## 4. Korrekturen gegenüber früheren Gesprächsbefunden

1. Der frühere pauschale Befund „Member-Zuweisung gibt den alten Inhalt nicht frei“ ist für den jetzt gelesenen Stand überholt. Zentraler Member-Free ist vorhanden; verbleibende Probleme betreffen insbesondere Alias-Schutz, abweichende lokale Zuweisungspfade und vollständige Drop-Abdeckung.
2. UI-Getter liefern inzwischen RC-Strings und haben einen Registrierungs-Sonderfall. Der offene Befund ist der fehlende Abgleich mit dem Analyzer (F1), nicht mehr allein rohes strdup.
3. Die ursprünglichen Server-/Client-Leakraten lassen sich aus diesen Änderungen oder dem Source-Review weder als behoben noch als unverändert bestätigen. Server-RSS und belegter WASM-Heap sind zudem unterschiedliche Messgrößen.

## 5. Priorität für die weitere Arbeit

1. F1 und F2 gezielt beheben; vorhandene Sicherheits-Gates dabei erhalten.
2. Ownership-Klassifikation für Analyzer und Codegen vereinheitlichen, einschließlich aufgelöster Producer-/Builtin-Identität.
3. Copy-/Move-/Replace-Entscheidungen einschließlich Branches an einen gemeinsamen Transferplan binden.
4. Spec-Status separat vom Zielvertrag pflegen; fehlende Unterstützung nicht mit „immer sicher, höchstens Leak“ pauschal beschreiben.
5. Danach die relevanten Laufzeitpfade mit geeigneter Speicherfehlerprüfung und Heap-Messung bestätigen. Dieser Report implementiert oder startet solche Tests nicht.

Dies ist kein Vorschlag, das verworfene WP-C als Ganzes wieder einzuführen. Die Befunde betreffen die bereits vorhandene Ownership-Implementierung und ihre konkreten Schnittstellen.

## 6. Gelesene Quellen

- [Spec 61](../../spec/61_lifetime_ownership.md)
- [Ownership-Analyzer](../../src/core/ownership_analyzer.cpp)
- [Ownership-Flow](../../src/core/ownership_flow.cpp)
- [Statement-Codegen](../../src/core/ast_compiler_stmt.cpp)
- [Call-Codegen](../../src/core/ast_compiler_call.cpp)
- [Compiler / Feld-Free und Copy](../../src/core/ast_compiler.cpp)
- [String-/Listen-Runtime](../../src/sys/string_runtime.cpp)
- [Prefab-Runtime](../../src/sys/prefab_runtime.cpp)

### Dateifingerprints beim Ablegen

Die Hashes dienen zur Erkennung späterer Änderungen; der Review ist kein atomarer Repository-Snapshot.

Erfasst: 2026-09-12T14:13:46+02:00

| Datei | SHA-256 |
|---|---|
| `spec/61_lifetime_ownership.md` | `768224cba95fe3273fd97432b96d84fed3c88f1f16c0bc305a10e36a79381986` |
| `src/core/ownership_analyzer.cpp` | `d6bf58551def3dea713b19035e4c219293afa55cd4af59cea1f743a6a93b10c9` |
| `src/core/ownership_flow.cpp` | `cd55ef68e0ccb726ff5e8521f2fc88c7862d074a09aaee5ed1baa8e1659e6404` |
| `src/core/ast_compiler_stmt.cpp` | `622f47037338d983b569d51bf1bc0628e0b5ed09049777041343dd77dec870e2` |
| `src/core/ast_compiler_call.cpp` | `bb25303d036692c452bda8f7dcb55251ab20a21191be7572088aa2eeedeaf808` |
| `src/core/ast_compiler.cpp` | `91828cfc759dfecef8b144ac2aa0cb26b61e1d9014e937054d1a18dd6aa3e882` |
| `src/sys/string_runtime.cpp` | `07d18e2b56ee04142225b36bf7315160cfdf83870dc0aaa3491bd120a03955b6` |
| `src/sys/prefab_runtime.cpp` | `de43d89a14172111dcf2ddc851a82e4c2d10bf53a92110b2c7b1f3a93dea75ef` |
