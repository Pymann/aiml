# sprint_task_10

## Titel

`builtin_registry.cpp` funktional modularisieren

## Status

Unveraendert relevant.

## Ziel

Den weiterhin sehr grossen Builtin-Monolithen in fachliche Module schneiden, ohne die bestehende Builtin-Oberflaeche zu brechen.

## Aktueller Stand

- [builtin_registry.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/builtin_registry.cpp) liegt weiterhin bei rund 7250 Zeilen.
- Es gibt bereits [builtin_registry.h](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/builtin_registry.h), aber noch keine echte Domaenentrennung.

## Was an der alten Fassung veraltet war

- Die Problemformulierung ist weiterhin richtig; praktisch zu ergaenzen ist heute nur der aktuelle Ist-Stand.

## Fokus

1. Registrierungsschnitt klein halten.
2. Domaenenmodule einfuehren.
3. gemeinsam genutzte Hilfen separat auslagern.
4. keine semantische Aenderung als Vorbedingung.

## Betroffene Stellen

- [builtin_registry.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/builtin_registry.cpp)
- [builtin_registry.h](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/builtin_registry.h)

## Akzeptanzkriterien

- Neue Builtins landen in Domaenenmodulen.
- Die zentrale Registrierungsoberflaeche bleibt klein.
- Die Aufspaltung fuehrt nicht zu stillen Sprachveraenderungen.
