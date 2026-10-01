# Spec 61: Cascade-Parameter - Befunde und Implementierungsplan

Stand: 2026-10-01. Untersuchter Modus: AIML_MEM_V3=1, AIML_LIFETIME_DROPS=1.
Umfang: Analyse, neue Probes und Plan; keine Compilerkorrektur durch diese Untersuchung.

## Ergebnis

Die gemeldete Inkonsistenz ist real. Im waehrend der Untersuchung aktualisierten
Arbeitsstand ist cascade bereits in CompilerContext::isByrefParamType enthalten.
Das behebt freie Funktionen, reicht aber weder fuer alle Aufrufwege noch fuer
Spec-61-konforme Ownership. Der Compiler wurde mit exodus build -j 2 neu gebaut;
die unten genannten Ergebnisse stammen aus diesem Build.

1. **P1: Parameter-Rebinding zerstoert Caller-Eigentum.**
   visitFunction registriert normale Containerparameter als Borrow ohne Retain/Drop
   (src/core/ast_compiler.cpp:5480, :5543, :5624). Der Handle-set-Pfad
   (src/core/ast_compiler_stmt.cpp:2848) released trotzdem den alten Handle.
   rebind.aiml belegt UAF/Double-free und einen Leak des neuen Handles:
   24 Bytes definite + 16 Bytes indirect, 12 Valgrind-Fehler, Exit 99.
2. **P1: Der Parameter ueberlebt ein Rebinding seines urspruenglichen Owners nicht.**
   owner_rebind.aiml uebergibt holder.row und ref<Holder>, ersetzt holder.row
   im Callee und benutzt danach row. Der alte Handle wurde bereits durch
   aiml_ser_free_decoded freigegeben. Valgrind meldet UAF; Exit 139.
   Die Probe initialisiert das Feld explizit und beendet den Seed-Scope vor dem Call,
   sodass keine zusaetzliche lokale Referenz den Fehler verdeckt.
3. **P1: Klassenmethoden haben weiterhin eine abweichende Vordeklaration.**
   registerTypeDecl prueft bei Methoden noch ausschliesslich list/map
   (src/core/ast_compiler.cpp:4109); visitFunction verwendet dagegen den neuen
   gemeinsamen Helper (:5366). Die vorhandene LLVM-Vordeklaration wird wiederverwendet.
   Interface-Konformitaet erwartet einen Pointer (:4772) und lehnt die Methode ab.
   Auch direkte Methoden sind deshalb zu pruefen. buffer fehlt an derselben Stelle.
4. **P1: fn-Aufrufe benutzen eine weitere abweichende Signatur.**
   src/core/ast_compiler_call.cpp:717 baut Parametertypen unmittelbar mit getLLVMType
   und uebergibt rohe Expressionswerte. indirect.aiml scheitert an der LLVM-Verifikation:
   GenericList als Argument gegen list_i32 in der Signatur; ausserdem muss das Ziel
   nach der neuen ABI einen Handle-Pointer erhalten.

## Reproduktion

bash tests/lifetime/cascade_params/run.sh

Der Runner baut native AOT-Probes und linkt gegen out/libaiml. Er prueft auch
ERROR-Meldungen im Buildlog, weil der Compiler bei LLVM-Verifikationsfehlern in
dieser Untersuchung teils Exit 0 lieferte. Valgrind ist zwingend; Fehler, Leaks,
Timeouts und nichtnull Programm-Exits lassen den Runner fehlschlagen.

| Probe | Geprueft | Ergebnis |
|---|---|---|
| direct.aiml | freie Funktion, Weiterreichen, Wachstum auf 40 Elemente, Inhalt | Exit 0; 0 definite/indirect; 0 Valgrind-Fehler |
| dispatch.aiml | direkte Methode und Interface-Methode mit gleicher Signatur | Buildfehler: arg row passt nicht zum Interface |
| indirect.aiml | fn<cascade<ProbeCode>:i32> | LLVM-Verifikationsfehler |
| rebind.aiml | lokale Neubelegung des Parameters, Caller bleibt unveraendert | Exit 99; UAF/Double-free; 24/16 Bytes verloren |
| owner_rebind.aiml | alter Parameter bleibt trotz Owner-Neubelegung lebendig | Exit 139; UAF |

Logs des abschliessenden Laufs: /tmp/aiml-cascade-params-33EmlC/.
Der vorherige Lauf ohne indirect liegt in /tmp/aiml-cascade-params-vFFbEM/.
Die Probes sind absichtlich noch kein GREEN-Gate.

## Sollvertrag

Spec 61, Tabelle 2.1: cascade<X> ist ein Class-3-Container mit list<i32>-Layout.
Section 5 fordert Aliasing durch geteilte Handles; Section 8 und 11.2 fordern
Incref bei der Parameteruebergabe. Gemeint ist ein kopierter Handle auf denselben
Container, keine Kopie von size/capacity/data und keine Referenz auf den Caller-Slot.

- Mutation und Reallokation des Containers sind fuer alle Aliase sichtbar.
- set auf den Parameter ersetzt nur dessen lokalen Handle.
- Jeder Parameter besitzt fuer seine Lebensdauer eine eigene Referenz.
- Rebinding sichert zuerst den neuen Handle, released den alten und speichert neu.
- Normaler Exit, fruehes return und Exception-Unwind released genau diese Referenz.
- Rueckgabe oder Speicherung darf die Referenz korrekt teilen/uebertragen.

**Spec-Widerspruch:** Section 9.2 beschreibt RC-Parameter noch als standardmaessig
borrowed und consumed als Retain/Co-ownership; Section 8 verlangt bereits Incref.
Zudem behandelt der Compiler consumed pauschal als Caller-Move.
Empfehlung: Section 8 als Default beibehalten; 9.2 fuer RC-Typen darauf abstimmen
und consumed als Co-ownership-Vertrag konkretisieren. ptr bleibt Move.
Eine blosse Abschwaechung von Section 8 auf Borrow loest owner_rebind nicht.

## Implementierungsplan

### 1. Parameter-ABI vollstaendig vereinheitlichen

Den vorhandenen Helper auch fuer Methoden-Vordeklarationen (:4109) einsetzen.
Freie Vordeklaration, Definition, Interface-Konformitaet, Vtable-Aufruf und fn-Aufruf
muessen dieselbe Parametertyp-Abbildung verwenden. Beim fn-Pfad auch Argumentwerte
als Handle laden; eine reine Aenderung des LLVM FunctionType genuegt nicht.
Container-Rueckgabetypen im fn-Pfad ebenfalls auf Handle-ABI abstimmen.
Kein globales Umstellen von getLLVMType: Layout der Containeroperationen bleibt noetig.
Vorhandene Alias-/Namespace-Aufloesung verwenden. Legacy memV3=0 separat beurteilen.

Abnahme: direct, dispatch und indirect kompilieren; Mutationen, Inhalte und
Weiterreichen sind korrekt. Vorwaertsreferenzen und buffer-Methoden ergaenzen.

### 2. Normale Containerparameter als lokale RC-Owner fuehren

Im Funktionsprolog unter memV3 den eingehenden Handle retainen und einen passenden
pendingParamDrop registrieren: cascade nutzt List-Release mit i32-Elementlayout.
Denselben Vertrag fuer list/map/buffer anwenden; der fehlende Retain ist eine
gemeinsame Containerluecke. Bestehende Drop-/Deskriptor-Helfer wiederverwenden.

Parameterstatus und Ownership getrennt fuehren: is_param darf nicht pauschal
"besitzt keine Referenz" bedeuten. Ownership-Analyse und Cleanup muessen lokale
Parameter-Slots bei Rebind und allen Exits abbilden. Bereits vorhandene Caller-
Temp-Releases duerfen nicht als zweite Freigabe derselben Referenz wirken.

Abnahme: rebind und owner_rebind ohne Speicherfehler und ohne definite/indirect Leaks.
Kontrollvarianten fuer list/map/buffer, zwei gleiche Argumente, Self-assignment,
fruehes return, Fallthrough und throw/catch pruefen.

### 3. Return, Escape, consumed und temporaere Argumente ausbalancieren

visitRetStmt (:3764, :3996) behandelt Parameter aktuell als Borrow und increft
Rueckgaben. Nach Schritt 2 entweder Parameter-Owner in das Resultat bewegen und
den Drop ausnehmen, oder Resultat retainen und Parameter normal droppen. Nicht
Incref plus Drop-Ausnahme kombinieren: das verliert eine Referenz.

set/push/Member-Store muessen bei RC-Quellen die benoetigte Referenz sichern;
Weitergabe darf keine fremde Caller-Referenz konsumieren. consumed-Verhalten gemaess
bereinigtem Vertrag an freien, direkten Methoden- und Interface-Aufrufen angleichen
(ast_compiler_call.cpp:526, :874, :3363).
Owned Producer-Temps, verschachtelte Calls und Interface-/fn-Temps muessen nach
Call/Statement beziehungsweise beim Unwind genau einmal freigegeben werden.

Abnahme: Parameter rueckgeben, Ergebnis ignorieren/binden/weiterreichen; Parameter
in Feld/Container speichern und nach Caller-Scope verwenden; consumed mit Alias,
frischem Temp, unbenutztem Parameter und Exception. Jeweils Inhalts- und Leaktests.

### 4. Cascade-Typregeln und Spec konsistent dokumentieren

Section 8/9.2 sowie Kommentare zu is_param/is_byref_arg auf den neuen Vertrag bringen.
isRefContainerType (context.h:424) erfasst bisher kein ref<cascade<...>>.
Das Verbot entsprechend der Class-3-Einordnung explizit in der Spec auffuehren
und fuer Parameter, Return, Feld und Local mit negativen Probes absichern.

Separater Nebenbefund: at auf cascade wird derzeit abgelehnt (ast_compiler_expr.cpp);
die neuen Probes benutzen deshalb das unterstuetzte .get. Ob at/idx als Cascade-
API gelten sollen, getrennt von der Parameter-ABI klaeren. Ebenso wurde ein nicht
explizit initialisiertes Cascade-Modellfeld in der ersten Probe als null beobachtet;
Initialisierung ist separat zu untersuchen, kein Nachweis fuer Parameter-Ownership.

### 5. Abnahme und Gate

Erst nach gruenen semantischen Assertions und null Valgrind-Fehlern die Probes in
run_lifetime_leak_gates.sh uebernehmen. Grenzwert: 0 definite + 0 indirect Bytes.
Bestehende 97_cascade_model_carrier_render, 132_interface_returns,
141_buffer_handle_set_and_param und 142_cascade_param_handle mitlaufen lassen.
Danach Lifetime-/Sanitizer-Suites und vorhandene Cascade-/Interface-Suites ausfuehren.
JIT sowie WASM fuer die geaenderte direkte/indirekte ABI gesondert verifizieren.

Die Untersuchung selbst hat native AOT + Valgrind ausgefuehrt. JIT, WASM und die
gesamte Testsuite wurden nicht ausgefuehrt; dieser Plan ist keine Gesamtfreigabe
der Implementierung von Spec 61.


## Umsetzungsstand (2026-10-01, Nacht)

- **Schritt 1 erledigt:** `CompilerContext::isByrefParamType` (list/map/buffer/cascade) an allen
  Signaturstellen (Definition, freie und Methoden-Vordeklaration, Interface-Pruefung, Vtable-Aufruf);
  `fn<…>`-Aufruf nutzt dieselbe Abbildung und uebergibt Container-Locals als Handle.
- **Schritt 2 erledigt:** Container-Parameter halten eine eigene Referenz (Prolog-Incref + Drop via
  `makeHandleDrop`), `consumed` uebernimmt die Caller-Referenz ohne Incref. `set` auf/aus Parametern
  teilt den Handle. spec/61 §8 praezisiert (kein echter Widerspruch zu §9.2: "borrowed for the call").
- **Schritt 3 erledigt:** Rueckgabe eines Parameters bewegt dessen Referenz (kein Incref + Drop-Ausnahme).
- **Schritt 4 erledigt:** `ref<cascade<…>>` ist Compile-Fehler (neg/n14). Nebenbefund behoben:
  Cascade-Modellfelder zaehlten nicht als Heap (`serHasHeap`) → Modell-Drop gab sie nie frei (Test 145).
- **Schritt 5:** alle Probes in `tests/run_lifetime_leak_gates.sh` (GREEN), dazu 142/144/145.
- Unwind (spec/61 §9.3, neu) mitumgesetzt: Exception-Pfade releasen Locals/Parameter, catch besitzt das Objekt.
- Offen (separat): `at` auf cascade (API-Frage), JIT/WASM-Sonderverifikation der indirekten ABI.
