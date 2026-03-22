# sprint_task_3

## Titel

Spec normalisieren und Prioritaetsmatrix fuer Implementierungsluecken anlegen

## Ziel

Die AIML-Spec soll als belastbare Referenz brauchbar werden. Vor weiteren grossen Implementierungen muessen Widersprueche, Mehrdeutigkeiten und nicht entschiedene Stellen in eine priorisierte Matrix ueberfuehrt werden.

## Hintergrund

Die bestehende Spec ist breiter als die Implementierung und gleichzeitig intern noch nicht sauber normiert. [SPEC_REVIEW.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/spec/SPEC_REVIEW.md) dokumentiert bereits 71 Issues. Solange diese Punkte nicht in "gilt", "gilt nicht", "spaeter" aufgeloest sind, bleibt jede Implementierungsarbeit an mehreren Stellen unscharf.

## Scope

- bestehende Spec-Issues sichten und in umsetzbare Kategorien ueberfuehren
- fuer jedes Kapitel klar markieren:
  - implementiert
  - teilweise implementiert
  - nur spezifiziert
  - widerspruechlich oder noch offen
- eine Reihenfolge fuer sprachliche Kernfeatures festlegen

## Betroffene Stellen

- [spec](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/spec)
- [SPEC_REVIEW.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/spec/SPEC_REVIEW.md)
- [spec_impl_completeness.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/spec_impl_completeness.md)

## Umsetzungsrichtung

1. Bestehende Review-Punkte nach Sprachkern, Runtime, Syntax und Tooling clustern.

2. Pro Spec-Datei eine knappe Statuszeile einfuehren:

- Status
- groesste Luecken
- offene Entscheidungen

3. Daraus eine Prioritaetsmatrix erstellen:

- Sprint-tauglich
- blockiert durch Spec-Entscheidung
- nur Dokumentationsarbeit

4. Die Matrix so formulieren, dass daraus direkt Compiler- und Testarbeit abgeleitet werden kann.

## Akzeptanzkriterien

- Es gibt eine belastbare Uebersicht, welche Spec-Teile wirklich normativ sind.
- Widersprueche und offene Entscheidungen sind explizit markiert.
- Die wichtigsten Implementierungsluecken sind priorisiert.
- Nachfolgende Sprint-Tasks muessen nicht mehr gleichzeitig Spec und Code raten.

## Ergebnis

Nach Abschluss ist klar, welche Teile der AIML-Spec als verbindlicher Zielzustand gelten und welche Stellen zuerst angefasst werden muessen.
