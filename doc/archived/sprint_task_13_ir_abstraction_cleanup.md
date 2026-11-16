# sprint_task_13

## Titel

IR-Abstraktion auf einen klaren Besitzpfad reduzieren

## Status

Weiterhin offen.

## Ziel

Die parallele Existenz von rohen LLVM-Pfaden und Wrapper-Abstraktion soll auf eine belastbare Zielarchitektur reduziert werden.

## Aktueller Stand

- [ir_generator.cpp](../../src/core/ir_generator.cpp) existiert weiterhin, ist aber klein.
- Gleichzeitig lebt der Grossteil der echten Compilerarbeit weiterhin sehr nah an rohem LLVM in den `ast_compiler_*`-Dateien.

## Was an der alten Fassung veraltet war

- Die Task klang nach reinem `Symbol`-Cleanup; das Problem ist breiter und betrifft den offiziellen Besitzpfad fuer IR-Erzeugung insgesamt.

## Fokus

1. Zielarchitektur explizit entscheiden.
2. danach Mischpfade markieren.
3. neue Compilerlogik nur noch ueber den Zielpfad zulassen.

## Betroffene Stellen

- [ir_generator.cpp](../../src/core/ir_generator.cpp)
- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp)
- [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp)
- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp)
- [context.h](../../src/core/context.h)

## Akzeptanzkriterien

- Neue Compilerarbeit muss nicht mehr zwischen zwei gleichberechtigten IR-Welten springen.
- Mischpfade sind sichtbar und werden schrittweise kleiner.
- Die Zielarchitektur ist fuer Folgearbeit verbindlich genug.
