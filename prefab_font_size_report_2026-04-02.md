# Prefab Font Size Report

Datum: 2026-04-02

## Kurzfassung

`font_size` fuer Prefab-Labels ist aktuell nicht korrekt wirksam.

Die Werte werden zwar aus AIML eingelesen und in der Runtime gespeichert, beim Rendern aber nicht verwendet. Dadurch erscheinen Texte auf Karten effektiv alle in derselben Schriftgroesse.

## Was korrekt funktioniert

- `font_size` ist in den Prefabs unterschiedlich definiert, z.B. in `CardFull` und `CardToken`.
- Der AIML-Builder liest `font_size` korrekt aus dem Prefab-Quelltext.
- Die Prefab-Runtime parsed und speichert `font_size` ebenfalls korrekt im Child.

## Wo der Fehler wirklich sitzt

Der Fehler liegt nicht primaer im Compiler, sondern in der Prefab-Runtime:

- Beim Rendern von `CHILD_LABEL` wird immer nur die global gesetzte Prefab-Font benutzt.
- `child.font_size` wird dabei ignoriert.
- `push_text_batch(...)` arbeitet nur mit den Metriken des einmal gebackenen Font-Atlas.

Praktische Folge:

- alle Prefab-Labels wirken gleich gross
- wenn die global gesetzte Prefab-Font gross geladen wurde, wirken auch kleine Labels zu gross

## Warum die Karten besonders auffallen

Im `board_screen` wird die Prefab-Font relativ gross gesetzt:

- `ui.load_font_default(24.0)`
- danach `engine.prefab_set_font(...)`

Da pro Label keine eigene Groesse mehr angewendet wird, rendern die Kartenlabels mit dieser globalen Font-Metrik statt mit ihren lokalen `font_size`-Werten wie `11`, `10`, `9` oder `6`.

## Fix einfach erklaert

Es gibt zwei moegliche Richtungen:

### Option A: pro Label skalieren

Idee:

- eine Basisfont wird einmal geladen, z.B. 24 px
- beim Rendern eines Labels wird daraus ein Faktor berechnet
- Beispiel: Label will 12 px, Basisfont ist 24 px -> Render-Scale `0.5`

Vorteile:

- relativ kleiner Eingriff
- keine Font-Neubackerei fuer jede Groesse

Nachteile:

- kleine/grosse Groessen koennen optisch etwas unschaerfer werden als echte separat gebackene Fonts

## Option B: Fonts pro Groesse cachen

Idee:

- fuer jede benoetigte `font_size` wird eine eigene gebackene Font geladen oder gecacht
- das Label waehlt dann die passende Font mit echter Metrik

Vorteile:

- typografisch sauberer
- exaktere Groessen und Abstaende

Nachteile:

- mehr Runtime-Komplexitaet
- mehr Atlas-/Speicherverwaltung

## Empfehlung

Fuer `9rooms` wuerde ich mit Option A anfangen.

Grund:

- sie behebt sofort das sichtbare Problem
- sie ist deutlich kleiner und risikoaermer
- falls noetig, kann spaeter immer noch auf Font-Caching pro Groesse erweitert werden

## Aufwand

- Option A: etwa `0.5 bis 1 Tag`
- Option B: etwa `1 bis 2 Tage`

## Status frueherer Reports

Ich habe keine alten Reports geloescht.

Grund:

- in `test/aiml` liegen aktuell nur die beiden vorhandenen Reports
- ohne klare Anweisung, welche davon weg sollen, ist Loeschen unnoetig riskant

