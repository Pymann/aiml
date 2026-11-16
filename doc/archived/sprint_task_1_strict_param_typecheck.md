# sprint_task_1

## Titel

Strikte Parametertyppruefung im Call-Pfad konsolidieren

## Status

Teilweise vorbereitet, aber noch nicht als durchgaengig harte Sprachregel abgesichert.

## Ziel

Funktions- und Methodenaufrufe sollen bei falschen Argumenttypen frueh und eindeutig im Compiler scheitern, statt implizit weiterzucasten oder spaet in LLVM-/Runtime-Fehler zu laufen.

## Aktueller Stand

- Der relevante Hauptpfad liegt heute in [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp).
- Die alte Annahme eines separaten `call_dispatcher.cpp` als Kernpfad ist veraltet; diese Datei existiert im aktuellen Stand nicht.
- Builtin-Semantik lebt weiterhin stark in [builtin_registry.cpp](../../src/core/builtin_registry.cpp).

## Was an der alten Fassung veraltet war

- `call_dispatcher.cpp` als zentrale Abhaengigkeit stimmt nicht mehr.
- Der Task war zu breit fuer „alle Call-Arten gleichzeitig“.
- Die Trennung zwischen Sprachregel und Builtin-Sonderfall war nicht scharf genug.

## Fokus

1. Direkte User-Function-Calls haerten.
2. Danach Methodenaufrufe und `fn<...>`-Calls auf dieselbe Validierungslogik ziehen.
3. Builtin-Sonderfaelle explizit isolieren, statt sie still in den allgemeinen Call-Regeln mitzuschleppen.

## Betroffene Stellen

- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp)
- [compiler.cpp](../../compiler.cpp)
- [context.h](../../src/core/context.h)
- bei Builtin-Sonderfaellen: [builtin_registry.cpp](../../src/core/builtin_registry.cpp)

## Akzeptanzkriterien

- Falscher Parametertyp bei User-Funktionen erzeugt einen klaren Compilerfehler.
- Zu viele und zu wenige Argumente werden konsistent behandelt.
- Methoden und `fn<...>` folgen derselben Grundregel oder sind explizit als Folgearbeit markiert.
- Builtin-Ausnahmen sind sichtbar und nicht mehr unbeabsichtigter Seiteneffekt des allgemeinen Call-Pfads.
