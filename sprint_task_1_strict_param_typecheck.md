# sprint_task_1

## Titel

Strikte Typpruefung fuer Funktionsparameter im AIML-Compiler

## Ziel

Der Compiler soll bei falschen Argumenttypen fuer normale Funktionsaufrufe und Methoden einen klaren Compile-Fehler ausgeben, statt still zu casten, LLVM die Typabweichung zu ueberlassen oder erst zur Laufzeit schiefzugehen.

## Hintergrund

Der aktuelle Call-Pfad ist bei Parametertypen zu weich:

- zu wenige Argumente werden bereits gemeldet
- falsche Typen werden aber oft still gecastet oder nur implizit weitergereicht
- Pointer/Struct-Faelle werden teilweise automatisch angepasst
- viele Builtins casten Zahlen still

Dadurch fehlt ein sauberer Frontend-Fehler der Form:

- Funktion
- Argumentindex
- erwarteter Typ
- tatsaechlicher Typ

## Scope

- allgemeine Funktionsaufrufe
- Methodenaufrufe
- indirekte Aufrufe ueber `fn<...>` soweit moeglich mit derselben Logik
- konsistente Behandlung von zu wenigen, zu vielen und falsch typisierten Argumenten

## Betroffene Stellen

- [ast_compiler_call.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_call.cpp)
- [ir_generator.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ir_generator.cpp)
- [call_dispatcher.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/call_dispatcher.cpp)
- optional zur Sonderfallpflege: [builtin_registry.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/builtin_registry.cpp)

## Umsetzungsrichtung

1. Vor jedem echten `CreateCall(...)` eine zentrale Argumentvalidierung ausfuehren.

2. Fuer jedes Argument vergleichen:

- erwarteter LLVM-/AIML-Typ
- tatsaechlicher Ausdruckstyp
- ob ein impliziter Cast laut Sprachregel erlaubt ist

3. Nur bewusst erlaubte implizite Konversionen zulassen.

4. Bei ungueltigem Typ einen klaren Compilerfehler erzeugen und den Call nicht weiter emittieren.

5. Dieselbe Validierungslogik fuer:

- direkte Calls
- Methodenaufrufe
- fptable-/indirekte Calls

so weit wie moeglich vereinheitlichen.

## Empfohlene Regeln

- Keine stillen Casts zwischen `string` und Zahltypen.
- Keine stillen Casts zwischen Struct und nicht-passendem Pointertyp.
- Pointer/Struct-Byref nur dann automatisch erlauben, wenn das explizit Sprachregel ist.
- Numerische Casts nur dann implizit erlauben, wenn das bewusst gewollt ist und dokumentiert wird.

## Fehlerformat

Fehlermeldungen sollten mindestens enthalten:

- Funktions- oder Methodenname
- Argumentindex
- erwarteter Typ
- tatsaechlicher Typ

Beispielformat:

```text
Type mismatch in call to foo at argument 2: expected i32, got string
```

## Akzeptanzkriterien

- `fn(i32)` mit `string`-Argument erzeugt einen klaren Compilerfehler.
- Methodenaufrufe mit falschem Parametertyp erzeugen ebenfalls einen klaren Compilerfehler.
- Zu wenige und zu viele Argumente werden konsistent gemeldet.
- Der Compiler verlaesst sich fuer solche Faelle nicht mehr primaer auf spaetere LLVM-Fehler.
- Bestehende legitime Builtin-Casts brechen nicht unbeabsichtigt.

## Risiken

- Builtins und Runtime-nahe APIs nutzen heute teils absichtlich weiche Casts.
- Eine zu globale Härtung kann bestehende Skripte brechen.
- Deshalb sollte User-Function-Validation zuerst klar von Builtin-Sonderfaellen getrennt werden.

## Ergebnis

Nach Abschluss soll der AIML-Compiler bei falschen Funktionsparametern frueh, klar und reproduzierbar scheitern.
