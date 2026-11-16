# Machbarkeit von Closures in AIML

Stand: 15.10.2026. Quellcodebasierte Architekturprüfung; keine Implementierung, kein Closure-Prototyp und kein neuer Compiler-Testlauf. Aufwand ist eine grobe technische Schätzung, keine gemessene Umsetzungsgeschwindigkeit. Annahme: eine mit AIML vertraute Person, konzentrierte Arbeit, keine größeren zusätzlichen Compilerfehler.

> **Nachtrag 15.10.2026:** Als Spec und Abnahmetests ausformuliert: `spec/64_closures.md`
> (PROPOSAL) und `tests/closures/` (5 positive + 16 neg-Proben; geschrieben, noch nicht
> kompiliert). Drei Entscheidungen weichen vom Text unten ab:
> **(1)** Signaturen werden **benannt** deklariert (`- callable: { name: Each, args: … }` →
> `block<Each>` / `closure<Each>`) statt inline `<param:return>`; **(2)** es gibt **zwei**
> Arten — `block<Sig>` (nicht-entkommend, implizite Captures per Referenz, nicht-besitzend,
> null Allokation) und `closure<Sig>` (entkommend, explizite Captures, besitzend move-only);
> **(3)** `fn<…>` bleibt vollständig unverändert. Punkt (2) macht den `engine.run`-Fall zum
> leichtesten statt zum schwersten: ein `block` erreicht den App-Zustand im aufrufenden Frame,
> ohne Global, ohne Heap-Environment, ohne Ownership-Transfer. Implementierung steht aus.

## Ergebnis

Closures sind machbar. Ein eingeschränkter Prototyp ist mittelschwer; eine sichere, allgemeine Sprachfunktion ist anspruchsvoll. Die größte Arbeit liegt in Ownership, Lebensdauer und Callback-Integration, nicht im YAML-Parser oder im indirekten LLVM-Aufruf.

AIML hat bereits nackte Funktionszeiger und Interface-Aufrufe mit Objektkontext. Das liefert einen erheblichen Teil der technischen Grundlage. Es fehlt die Verknüpfung von lexikalisch eingefangenen Werten mit einem aufrufbaren, korrekt verwalteten Objekt.

## Was heute existiert

| Bereich | Befund | Relevante Stelle |
|---|---|---|
| Funktionswerte | `fn<...>` ist ein einzelner opaker Codezeiger, kein Paar aus Code und Umgebung | `src/core/context.h:1277`, `spec/12_1_function_pointers.md` |
| Funktionsreferenz | Funktionsname wird in einen Zeiger umgewandelt | `src/core/ast_compiler_expr.cpp:423` |
| Indirekter Aufruf | Signatur wird aus fn-Typ rekonstruiert; Aufruf über `callOrInvoke`, inklusive bestehender Argument-Aufräumlogik | `src/core/ast_compiler_call.cpp:1035` |
| Objektkontext | Interface-Dispatch verwendet `{obj, vtable}` und einen versteckten this-Parameter | `src/core/ast_compiler_call.cpp:1158` |
| Syntax/AST | Geprüfte AST- und Expression-/Block-Builder haben keinen dedizierten Closure-Ausdruck beziehungsweise Capture-Knoten | `src/core/ast_nodes.h`, `src/core/ast/expression_builder.h`, `src/core/ast/block_builder.cpp` |
| Speicherregeln | fn ist laut Spec ein trivial kopierbarer Wert ohne Teardown; ptr/shared/ref und Cleanup-Pfade existieren für andere Typen | `spec/61_memory_management.md:156`, `src/core/borrow_check.cpp` |
| Engine-Callbacks | Feste native Signaturen `void(float)` und `void()`; Builtin übergibt drei Zeiger | `src/sys/engine_runtime.cpp:491`, `src/core/builtin_registry.cpp:8311` |
| Browserpfad | Der aktive engine.run-Pfad verwendet eine manuelle Asyncify-Schleife | `src/sys/engine_runtime.cpp:503` |

Achtung: Die Engine-Spec erwähnt als Vorschlag set_main_loop_arg; die aktive Implementierung nutzt bereits die manuelle Asyncify-Schleife. Eine Änderung muss dem Code folgen und diese Rückruf-/Suspendierungssemantik erhalten.

## Warum fn nicht einfach eine Umgebung bekommen sollte

Ein bisheriger fn-Wert ist nur ein Codezeiger und laut Speichermodell ohne Teardown kopierbar. Eine Closure benötigt dagegen Code, Umgebung und Regeln zum Freigeben der Umgebung. Eine stille Umstellung von fn auf zwei Zeiger würde Layout, native Callback-ABIs, Container, Parameter, Rückgaben und Ownership verändern.

Empfehlung: fn kompatibel lassen; separaten aufrufbaren Closure-Typ einführen. `closure<...>` ist hier nur ein Arbeitsname, keine existierende Syntax. Repräsentation entweder als verwaltetes Objekt mit Aufruf-/Destruktorfunktionen oder als Fat Pointer mit explizitem Besitzer der Umgebung. An der Grenze zu C braucht es immer einen passenden Adapter und einen Kontextparameter. Ein Cast auf einen nackten Funktionszeiger löst das nicht, insbesondere nicht für WASM.

## Empfohlener erster Umfang

- Explizite Capture-Liste; kein implizites Einfangen aller sichtbaren Variablen.
- Werte werden nach den bestehenden Typregeln kopiert oder explizit verschoben. Eine Wert-Capture ist nicht automatisch eine tiefe Kopie aller enthaltenen Handles.
- Zunächst eine besitzende, verschiebbare Closure mit eindeutigem Besitzer. Aufrufe borgen die Closure nur aus. Gemeinsamer Besitz erst nach klarer Semantik.
- ptr-Capture nur per Move; keine zweite Besitzinstanz auf denselben ptr erzeugen.
- Keine entkommenden ref-Captures im ersten Umfang: Rückgabe und Speicherung dürfen keine Referenz auf einen bereits verlassenen Stackframe erzeugen.
- Keine implizite Übertragung über Threads, Mirror, KV oder sys.bundle. Diese Verwendungen vorerst mit Diagnose ablehnen.
- Nicht unterstützte Kombinationen ausdrücklich zurückweisen, statt sie als opake Zeiger durchzulassen.

Ein Generator kann pro Closure eine interne Umgebungsklasse und eine aufrufbare Funktion erzeugen. Die Funktion bekommt die Umgebung als versteckten Parameter; Capture-Zugriffe werden auf ihre Felder umgeschrieben. Vorhandener Interface-Dispatch ist dafür eine Vorlage, aber keine fertige Closure-Implementierung.

## Notwendige Arbeitspakete

1. Semantik festlegen: Capture-Modi, Mutation, Kopie/Move, verschachtelte Closures, Lebensdauer, Null-Aufruf, Ausnahmen und unterstützte Speicherorte.
2. AST/Parser ergänzen: Closure-Ausdruck mit typisierten Argumenten, Rückgabetyp, Capture-Liste und Body. Quellpositionen erhalten.
3. Namensauflösung ergänzen: Parameter, lokale Namen und Captures trennen; Shadowing und verschachtelte Scopes korrekt behandeln. Bei expliziten Captures müssen nicht deklarierte freie Variablen diagnostiziert werden.
4. Closure-Konvertierung: interne Umgebung und aufrufbare Funktion erzeugen; Capture-Initialisierung genau einmal auswerten; Typen und Symbolnamen deterministisch erzeugen.
5. Codegen/ABI: Umgebung bei indirekten Aufrufen übergeben; bestehende Parameter-/Rückgabe-ABI und Ausnahmenbehandlung wiederverwenden. Native und WASM-Signaturen müssen exakt passen.
6. Ownership/Cleanup: Move, Scope-Ende, Überschreiben, Rückgabe, Containerablage und Ausnahme-Unwinding für Umgebung und Captures implementieren. Teilweise initialisierte Umgebungen auf Fehlerpfaden freigeben.
7. Laufzeit-APIs: zunächst engine.run closurefähig machen, inklusive Ownership während der gesamten nativen/Asyncify-Schleife und Freigabe beim Ende. Weitere Callback-APIs einzeln inventarisieren; sie werden nicht automatisch closurefähig.
8. Integration: AST-Walker, statische Analyse, Typinferenz, Import-/Cache-Schlüssel und Debug-/Quellinformationen aktualisieren. Indirekte Aufrufe sind bereits eine Grenze mancher statischer Effektprüfungen; das darf nicht als vollständige Sicherheitsprüfung dargestellt werden.

## Schwierige Randfälle

- Eine Closure überlebt die Funktion, in der sie entstand: Captures dürfen nicht nur auf deren Stackslots zeigen.
- Mehrere Closures sollen dieselbe veränderliche Variable sehen: dafür braucht es eine gemeinsam besessene Box statt unabhängiger Kopien. Nicht automatisch in den ersten Umfang aufnehmen.
- Captures enthalten Strings, Container, Modelle oder besitzende Pointer: Freigabe und Move müssen auch bei throw korrekt sein.
- Eine Closure hält das Objekt, das sie selbst speichert: gemeinsamer Besitz kann Zyklen erzeugen. Referenzzählung allein löst diese nicht.
- Threadwechsel verlangt zusätzliche Synchronisations-/Send-Regeln; vorhandene shared-Regeln nicht umgehen.
- C-Callbacks ohne user-data-Parameter können beliebigen Closure-Zustand nicht ohne zusätzliche Registrierung/Adapter tragen. Generierte native Trampoline wären kein tragfähiger allgemeiner WASM-Weg.
- Persistente beziehungsweise remote ausgeführte Closures brauchen Codeidentität und serialisierbare Umgebung, keine rohen Prozesszeiger. Das wäre ein eigenes Vorhaben mit sys.bundle.
- Hot Reload muss die Lebensdauer von Code und Umgebung gemeinsam berücksichtigen; kein ungültiger Codezeiger nach Modulwechsel.

## Grobe Aufwandsspannen

| Ziel | Schwierigkeit | Größenordnung |
|---|---|---|
| Nur expliziter Kontext für engine.run, keine neue Sprachfunktion | klein bis mittel | etwa 2–4 Arbeitstage inklusive nativer/WASM-Prüfung |
| Eingeschränkter Closure-Prototyp: explizite primitive Captures, lokale/überschaubare Aufrufe | mittel | etwa 1–2 Wochen |
| Belastbare erste Version mit besitzenden Captures, sauberem Cleanup, Rückgabe/Speicherung und Engine-Anbindung | hoch | etwa 4–8 Wochen |
| Zusätzlich allgemeine Referenz-Captures, geteilter veränderlicher Zustand, Threadwechsel, Hot Reload und Remote-Serialisierung | sehr hoch | separates mehrwöchiges bis mehrmonatiges Vorhaben; ohne genaue Semantik nicht seriös enger schätzbar |

Die Spannen sind nicht additiv und hängen stark vom gewählten Umfang und vom Zustand der Ownership-/WASM-Pfade ab. Ein früher vertikaler Prototyp sollte die Schätzung verifizieren.

## Tests als Abnahmekriterien

- Zwei Instanzen derselben Closure mit unterschiedlichen Captures bleiben unabhängig.
- Rückgabe aus einer Factory: Capture nach Ende der Factory weiterhin gültig.
- Strings, Container und Modelle werden beim Scope-Ende, Überschreiben und throw exakt einmal freigegeben.
- ptr-Move in Capture: anschließende Nutzung des ursprünglichen Besitzers wird abgewiesen.
- Entkommende Stackreferenz wird abgewiesen.
- Signaturfehler und Null-Aufruf liefern definierte Diagnosen statt eines Absturzes.
- Verschachtelte Scopes und Shadowing greifen auf den richtigen Wert zu.
- Parameter und Rückgaben für Container/Modelle folgen exakt der bestehenden ABI.
- Native und WASM-Ausführung desselben Beispiels liefern gleiche Ergebnisse.
- Engine-Callback kann seinen App-Zustand ohne App-Global benutzen; Browser-Suspendierung und Beenden geben ihn nicht vorzeitig frei.
- Bestehende fn- und Lifetime-Tests bleiben erfolgreich.

## Empfehlung für Cosmos

Wenn das Ziel nur das letzte g_cosmos_app ist, ist der explizite Engine-Kontext der kleinste Eingriff. Die entsprechende Spec hält eine frühere Entscheidung „jetzt nicht umsetzen“ fest; diese Prüfung implementiert deshalb nichts.

Wenn Closures als Sprachfunktion gewünscht sind, lohnt sich ein eigener, begrenzter Entwurf: fn behalten, explizite Captures, besitzende Umgebung, keine entkommenden Referenzen. Zuerst einen vertikalen Prototyp über Parser → Capture → Aufruf → Freigabe → WASM bauen; danach Engine-Integration und allgemeinere Capture-Typen. So wird der schwierigste Teil früh geprüft, bevor die Syntax auf alle Laufzeit-APIs ausgedehnt wird.
