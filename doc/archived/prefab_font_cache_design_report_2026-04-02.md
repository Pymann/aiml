# Prefab Font Cache Design Report

Datum: 2026-04-02

## Kurzfassung

Ja, eine Wiederverwendung bereits gebackener Font-Groessen ist sinnvoll und sollte fuer Prefab-Labels in der Runtime umgesetzt werden.

Die richtige Stelle dafuer ist nicht der AIML-Compiler, sondern die Prefab-/UI-Runtime. Der Compiler kennt zwar die `font_size`-Werte aus den Prefabs, aber das eigentliche Backen der Font-Atlanten passiert zur Laufzeit, nachdem die Font-Datei geladen wurde.

## Warum nicht im Compiler

Der Compiler sieht nur:

- Prefab-Definitionen
- Child-Typen
- `font_size`-Zahlen

Der Compiler hat aber nicht automatisch:

- die geladene Font-Datei als TTF-Bytes
- die zur Laufzeit gesetzte Prefab-Font
- den konkreten DPI-/Scale-Kontext
- den Moment, in dem `engine.prefab_set_font(...)` aufgerufen wird

Deshalb ist ein Compiler-Cache hier die falsche Ebene.

## Richtige Ebene: Runtime

Die Runtime hat alle noetigen Informationen:

- die aktuell gesetzte Prefab-Font
- die TTF-Daten dieser Font
- die benoetigten `font_size`-Werte pro Label
- die echte Bildschirm-/Scale-Situation

Darum sollte die Loesung so aussehen:

1. Eine Basisfont wird normal geladen.
2. Die Runtime merkt sich die TTF-Quelle dieser Font.
3. Fuer jede benoetigte Prefab-Label-Groesse wird bei Bedarf eine passende gebackene Font erzeugt.
4. Diese wird in einer Cache-Map gespeichert.
5. Naechste Labels mit derselben Groesse verwenden dieselbe gebackene Font wieder.

## Konkretes Design

### Datenmodell

`AimlUIFont` sollte zusaetzlich behalten:

- `ttf_data`
- `ttf_size`
- `baked_size_px`

Die Prefab-Szene sollte einen Cache halten, z.B.:

```cpp
std::unordered_map<int, AimlUIFont*> font_cache;
```

### Cache-Key

Praktisch ist ein quantisierter Integer-Key, z.B.:

- `round(font_size * 10)`

Damit lassen sich auch halbe oder zehntel Groessen stabil wiederverwenden.

### Lookup-Pfad

Beim Rendern eines Label-Children:

1. Zielgroesse bestimmen
2. Cache-Key daraus berechnen
3. Cache pruefen
4. falls nicht vorhanden: Font mit genau dieser Groesse backen
5. Ergebnis im Cache speichern
6. mit dieser Font rendern

## Warum das fuer moderne Rechner okay ist

Die Zahl der real benoetigten Font-Groessen ist normalerweise klein.

Im aktuellen `9rooms`-Prefab-Set sind z.B. nur wenige Werte relevant:

- `6`
- `9`
- `10`
- `11`
- `12`
- `16`

Das bedeutet:

- nicht tausende Atlanten
- sondern nur einige wenige gebackene Varianten
- danach fast nur noch Cache-Hits

Der Speicher- und CPU-Aufwand ist fuer heutige Rechner voellig vertretbar.

## Vorteile gegenueber einfachem Scale-Fix

Option B hat diese Vorteile:

- echte Typometrik pro Groesse
- sauberere Glyph-Abstaende
- schaerfere kleine Texte
- weniger visuelle Artefakte als bloesses Runterskalieren einer grossen Atlas-Font

## Offene Punkte

Beim Design muss man noch zwei Dinge klar machen:

### 1. Invalidierung bei Font-Wechsel

Wenn `engine.prefab_set_font(...)` erneut aufgerufen wird, muss der Cache geleert werden.

Sonst wuerden gebackene Groessen einer alten Font weiterverwendet.

### 2. Cleanup

Die gecachten `AimlUIFont*` muessen beim Font-Wechsel oder beim Scene-Teardown sauber freigegeben werden.

## Empfehlung

Empfehlung: genau diese Runtime-Cache-Variante implementieren.

Kurz gesagt:

- nicht im Compiler
- sondern in der Prefab-Runtime
- pro Groesse lazy backen
- per Map wiederverwenden
- bei Font-Wechsel Cache leeren

## Aufwand

Saubere Implementierung inklusive Cleanup und Rebuild:

- etwa `0.5 bis 1 Tag`

