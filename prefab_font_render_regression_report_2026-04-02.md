# Prefab Font Render Regression Report

Datum: 2026-04-02

## Kurzfazit

Der kaputte Spiel-Render kommt sehr wahrscheinlich nicht daher, dass in `board_screen.aiml` eine zu große Font gesetzt wird, sondern durch einen Struct-Layout-Mismatch zwischen `ui_runtime.cpp` und `prefab_runtime.cpp`.

## Root Cause

`prefab_runtime.cpp` definiert lokal eine eigene `AimlUIFont`-Struktur und kommentiert selbst, dass sie `ui_runtime.cpp` exakt matchen muss.

In [prefab_runtime.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/sys/prefab_runtime.cpp#L41) ist sie aktuell:
- `info`
- `ttf_data`
- `atlas_tex`
- `cdata`
- `line_height`
- `ascent_px`
- `scale`
- `atlas_w`, `atlas_h`

In [ui_runtime.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/sys/ui_runtime.cpp#L64) wurde sie aber erweitert um:
- `ttf_size`
- `baked_size_px`

Damit ist das Layout nicht mehr identisch.

## Warum das das Rendering zerlegt

Prefab-Rendering liest an mehreren Stellen Felder direkt aus `AimlUIFont*`, z.B. in [prefab_runtime.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/sys/prefab_runtime.cpp#L587):
- `lf->atlas_tex`
- danach indirekt auch Glyph-Daten und Metriken in `push_text_batch(...)`

Wenn `prefab_runtime.cpp` das Font-Objekt mit falschem Layout interpretiert, dann liest es verschobene Offsets:
- `atlas_tex` ist potentiell nicht mehr der echte Texture-Handle
- `cdata`, `line_height`, `ascent_px`, `scale` koennen aus falschen Speicherpositionen gelesen werden
- dadurch sind kaputte Glyphen, falsche Positionen, unplausible Groessen oder kompletter visueller Muell plausibel

Das ist ein harter ABI-/Layout-Fehler, kein bloßes Tuning-Problem.

## Einordnung zur neuen Font-Logik

Die neue Font-Cache-Idee in `prefab_runtime.cpp` ist an sich sinnvoll.

Der eigentliche Regressionspunkt ist sehr wahrscheinlich die Kombination aus:
- Erweiterung von `AimlUIFont` in `ui_runtime.cpp`
- aber nicht nachgezogener Spiegelstruktur in `prefab_runtime.cpp`

## Nicht der Hauptschuldige

Die Stelle in [board_screen.aiml](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/9rooms/client/board_screen.aiml#L929) mit `ui.load_font_default, 24.0` ist eher nicht die eigentliche Ursache.

Sie kann Texte groß machen, aber sie erklärt nicht, warum das Spiel insgesamt "nicht mehr richtig rendert". Dieses Symptom passt deutlich besser zu dem Layout-Mismatch.

## Empfohlener Fix

### Fix A: gemeinsame Font-Definition erzwingen

Am saubersten:
- `AimlUIFont` nicht doppelt definieren
- gemeinsame Struct-Definition in ein gemeinsames Header auslagern und in beiden Runtimes verwenden

Das beseitigt die Fehlerklasse dauerhaft.

### Fix B: kurzfristiger Minimalfix

Falls schnell:
- die lokale `AimlUIFont`-Definition in `prefab_runtime.cpp` exakt auf den neuen Stand von `ui_runtime.cpp` bringen

Das ist aber nur die weniger robuste Variante.

## Aufwand

- Minimalfix: `0.25 bis 0.5 Tag`
- robuster gemeinsamer Header-Fix: `0.5 bis 1 Tag`

## Bewertung

Wahrscheinlichkeit, dass das der aktuelle Render-Bug ist: hoch.
