# sprint_task_3

## Titel

Spec-Status normalisieren und als lebende Prioritaetsmatrix pflegen

## Status

Teilweise vorbereitet durch vorhandene Review-Dokumente, aber noch nicht als belastbare Arbeitsmatrix verdichtet.

## Ziel

Die AIML-Spec soll nicht nur gesammelt, sondern in einen aktuellen, umsetzbaren Status ueberfuehrt werden: verbindlich, offen, widerspruechlich oder bereits ueberholt.

## Aktueller Stand

- [SPEC_REVIEW.md](../../spec/SPEC_REVIEW.md) und [spec_impl_completeness.md](../../spec_impl_completeness.md) existieren bereits.
- Es gibt damit schon Vorarbeit; der Engpass ist heute eher Pflege, Priorisierung und Entschlackung als reine Erstaufnahme.

## Was an der alten Fassung veraltet war

- Die Formulierung klang nach kompletter Erstinventur.
- Die Zahl alter Issues ist als Steuerungsmodell zu grob.

## Fokus

1. Bestehende Review-Arbeit auf „normativ / offen / ueberholt / ersetzt“ verdichten.
2. Pro Sprachkapitel nur noch wenige konkrete Blocker benennen.
3. Daraus eine Reihenfolge fuer Compiler-, Runtime- und Testarbeit machen.

## Betroffene Stellen

- [spec](../../spec)
- [SPEC_REVIEW.md](../../spec/SPEC_REVIEW.md)
- [spec_impl_completeness.md](../../spec_impl_completeness.md)

## Akzeptanzkriterien

- Pro wichtigem Sprachkapitel ist sichtbar, ob es normativ, offen oder ueberholt ist.
- Nachfolgende Sprint-Tasks muessen keine Spec-Inventur mehr mittransportieren.
- Widersprueche und Altlasten sind klar von echter Folgearbeit getrennt.
