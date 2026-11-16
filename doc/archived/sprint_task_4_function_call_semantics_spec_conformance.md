# sprint_task_4

## Titel

Funktionsaufruf-Semantik an die aktuelle Spec annähern

## Status

Weiterhin relevant, aber enger zu schneiden als in der Erstfassung.

## Ziel

Die in [07_functions.md](../../spec/07_functions.md) beschriebenen Call-Regeln sollen schrittweise auf den realen Compilerpfad abgebildet werden.

## Aktueller Stand

- Der operative Kern liegt heute in [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp).
- `sprint_task_1` deckt den harten Typfehlerpfad ab; diese Task hier ist breiter und spec-orientierter.

## Was an der alten Fassung veraltet war

- Zu viel Ueberschneidung mit `sprint_task_1`.
- Verweise auf Dispatcher-Strukturen, die heute nicht mehr den Hauptpfad bilden.

## Fokus

1. Default-Argumente.
2. benannte bzw. trailing Map-Argumente.
3. klares Fehlermodell fuer nicht unterstuetzte Spec-Teile.
4. User-Function-Regeln vor Builtin-Sonderregeln.

## Betroffene Stellen

- [spec/07_functions.md](../../spec/07_functions.md)
- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp)
- [compiler.cpp](../../compiler.cpp)
- bei Builtin-Abweichungen: [builtin_registry.cpp](../../src/core/builtin_registry.cpp)

## Akzeptanzkriterien

- Unterstuetzte Call-Features verhalten sich konsistent zur Spec.
- Nicht unterstuetzte Features enden nicht in implizitem Chaos, sondern in klarer Begrenzung.
- Positiv- und Negativtests trennen User-Function-Regeln von Builtin-Ausnahmen.
