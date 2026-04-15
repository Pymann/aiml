# sprint_task_14

## Titel

Typsemantik aus verteilten Sonderfaellen in eine zentrale API ziehen

## Status

Weiterhin offen und strategisch wichtig.

## Ziel

Typregeln fuer Pointer, Container, Heap-Typen und Serialisierung sollen nicht weiter als verstreute String- und Sonderfalllogik ueber den Compiler verteilt sein.

## Aktueller Stand

- Eine explizite `TypeSemantics`-API existiert im aktuellen Code nicht.
- Typentscheidungen sind weiterhin ueber [context.h](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/context.h), [ast_compiler_expr.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_expr.cpp), [ast_compiler_stmt.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_stmt.cpp), [serialize_impl.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/serialize_impl.cpp) und [builtin_registry.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/builtin_registry.cpp) verteilt.

## Was an der alten Fassung veraltet war

- Inhaltlich wenig; die Task war schon nahe am eigentlichen Problem.
- Zu ergaenzen ist heute nur die klarere Verbindung zu Serialisierung, Mirror und Containerlayout.

## Fokus

1. kleine zentrale Typsemantik-API definieren.
2. die wichtigsten Fragen dort kapseln.
3. Call-, Decl-, Serialize- und Builtin-Pfade schrittweise darauf umstellen.

## Betroffene Stellen

- [context.h](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/context.h)
- [ast_compiler_stmt.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_stmt.cpp)
- [ast_compiler_expr.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_expr.cpp)
- [serialize_impl.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/serialize_impl.cpp)
- [builtin_registry.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/builtin_registry.cpp)

## Akzeptanzkriterien

- Neue Typregeln entstehen nicht mehr als verstreute String-Sonderfaelle.
- Mehrere Compilerpfade greifen auf dieselbe Typentscheidung zu.
- Container-, Pointer- und Heap-Edge-Cases werden konsistenter.
