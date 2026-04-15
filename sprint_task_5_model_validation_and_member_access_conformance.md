# sprint_task_5

## Titel

Member-Access und Modellvalidierung auf einen gemeinsamen Pfad ziehen

## Status

Weiterhin hoch relevant.

## Ziel

Tiefe Member-Zugriffe, LValue-Aufloesung und Modellvalidierung sollen konsistent werden, statt ueber mehrere halbgetrennte Compilerpfade zu laufen.

## Aktueller Stand

- Der Member-Zugriff ist heute noch ueber [ast_compiler_expr.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_expr.cpp), [ast_compiler_stmt.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_stmt.cpp) und [utils.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/utils.cpp) verteilt.
- [member_chain_plan.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/member_chain_plan.md) ist weiterhin die naheliegende Vorarbeit.

## Was an der alten Fassung veraltet war

- Zu viel Fokus auf rein dokumentierte Spec-Versprechen, zu wenig auf den realen zentralen Access-Pfad.
- Strukturelle Typisierung und tiefer Zugriff waren vermischt, obwohl sie unterschiedliche Risiken haben.

## Fokus

1. Einheitlicher Resolver fuer Read/Write/LValue.
2. danach tiefe Chains und Auto-Dot-Deref.
3. erst danach Map-zu-Model-Validierung und strukturelle Kompatibilitaet.

## Betroffene Stellen

- [spec/15_models.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/spec/15_models.md)
- [member_chain_plan.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/member_chain_plan.md)
- [ast_compiler_expr.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_expr.cpp)
- [ast_compiler_stmt.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_stmt.cpp)
- [utils.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/utils.cpp)

## Akzeptanzkriterien

- Tiefe Member-Chains verhalten sich fuer Read und Write konsistent.
- `ptr<Model>`-Zugriffe folgen einer klaren Regel.
- Modellvalidierung ist nachvollziehbar und nicht mehr nur Seiteneffekt einzelner Codepfade.
