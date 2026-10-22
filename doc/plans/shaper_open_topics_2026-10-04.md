# Shaper / glbview — offene AIML-Themen, 2026-10-04

## Herkunft und Prüfstand

Abgleich der Nutzerliste mit `../../../shaper/acceptance/P0/findings.md`, den
Projekt-READMEs und gezielt gelesenem AIML-Code sowie Specs. Keine erneute
Defektreproduktion oder Valgrind-Messung. W-/D-IDs stammen aus dem shaper-P0-
Register; gleichnamige IDs anderer AIML-Pläne sind nicht gemeint.

Bestehende Entscheidungen bleiben bestehen: W1, W3, W4 und ausdrücklich nur
zu meldende Defekte werden hier nicht zur Reparatur freigegeben. W10-Migration
bleibt entscheidungspflichtig. Dieser Abgleich ändert ausschließlich Dokumentation.

## Aktueller Projektstand

Laut README unterstützt shaper inzwischen Primitive, offene Flächen, Vereinigung,
Hülle, Spiegelung, UV-Atlas und GLB-/SVG-/PNG-Ausgabe. Texturen sind ausdrücklich
noch nicht unterstützt: texturierte Blattflächen bleiben funktionale Restarbeit.
Vorhandene UVs und Transparenz-Materialwerte ersetzen keine Blattbilder.

`buffer.write_to` und `buffer.encode_png` sind inzwischen in Spec 11 und im
Builtin-Register vorhanden. Nicht erneut als fehlend implementieren. Das schließt
W4 zum separaten Vertrag von `buffer.write` nicht.

## Entscheidungen und Einschränkungen

| ID | Befund | Umgang |
|---|---|---|
| W10 | Borrow als `ptr<T>` bei deklarierender Initialisierung abgewiesen, späteres `set` laut Register weiterhin möglich. | Spec-61-Lücke. Gemeldet: 43 Tests, 7 cosmos-Stellen, 0 in 9rooms; hier nicht nachgezählt. Vor Umsetzung Inventar erneuern und Migration zusammen mit Diagnostik planen. |
| W1 | Literale werden als f32 erzeugt; gemischte f64-Ausdrücke können ungültiges IR ergeben. | Weiter nur melden. f64-Zwischenwerte vermeiden gemischte Typen, stellen aber zuvor verlorene Dezimalpräzision nicht wieder her. |
| W3 | Spec 22: typisierte mehrteilige catch-Klauseln; laut Register nur ein Catch-all. | Weiter nur melden; shaper-Diagnostik als Daten/Status. Spec-61-Unwind bleibt separat einzuhalten. |
| W4 | Spec-11-Schreibvertrag weicht von nativen Stores ab: Endianness, Strings und Bounds/Exceptions; Register meldet auch kein Wachstum. | Weiter nur melden; Kapazitäten prüfen und kontrolliert packen. Automatisches Wachstum nicht allein aus der Zusammenfassung als Spec-Pflicht ableiten. |

W10 ist keine offene Designentscheidung über Besitz: Spec 61 definiert den
Borrow bereits. Offen ist die Freigabe des Migrationsumfangs. Nicht pauschal
`ptr` durch `ref` ersetzen: Besitzerlebenszeit und invalidierende Mutationen
prüfen; echten Besitz nur durch definierte Move-Out-Operation oder Clone erwerben.

## Gemeldete Defekte

| ID | Befund | Einordnung |
|---|---|---|
| D29 | Etwa 430 B definitely lost je `db.open`, nicht je Query laut Bericht. | Class 6: Runtime-/Registry-Besitz und Shutdown prüfen, keine manuelle Freigabe geliehener Store-Handles als Workaround. Hier nicht neu gemessen. |
| D23 | zip_entries/tar_entries geben eigene Elemente nicht frei. | Container, Elemente und deren Namen getrennt prüfen; laut Register nicht von shaper genutzt. |
| D19 | u8-Ausgabe als Bool und falsche Vergleiche über 127. | Binärdatenrelevant. Ausgabe und Vergleich getrennt reproduzieren; 127/128/255 testen. i32-Workaround ist kein Fix. |
| D11 | Multi-File-JIT von shaper scheitert bei externen Relokationen. | shaper/glbview weiter AOT. Nicht pauschal jedes Multi-File-Programm als betroffen erklären. |
| D1 | Numerische Operatorpromotion. | Nutzerliste nennt f64 mit i32, P0-Register f32 mit f64. Beide Fälle separat prüfen; Zusammenhang mit W1 dokumentieren. |
| D6 | Membername input führt laut Register zum Absturz. | Namensauflösung/Diagnostik; Umbenennung bleibt Workaround. |
| D10 | sys.get_arg in importierter Methode scheitert unter JIT. | Argumente im Entry-Modul lesen; Verhältnis zu D11 untersuchen, nicht ohne Beleg zusammenlegen. |

Zusätzlich im ursprünglichen Register offen, in der Zusammenfassung nicht genannt:

- **D5:** i32 nach i64 bei Call-Argumenten mit Zero- statt Sign-Extension.
- **D8:** unbekanntes `lifetime: persistent` im new-Initializer still ignoriert.
- **D9:** dyn_array-Probe 21 meldete 16 B definitely lost; erneut am aktuellen
  Compiler messen. Spec-61-relevant, auch wenn shaper den Typ laut Register meidet.
- **W6:** Globber fehlt; eigener FileGlob in shaper, nur melden.
- Intermittierender test_http_auth_helpers-Fehler im Register; separater Test-/
  Netzwerkbefund, kein nachgewiesener shaper-Defekt.

Historische Einträge sind damit weder erneut reproduziert noch als erledigt erklärt.

## Bisher fehlende glbview-Dokumentationsbefunde

Lokale IDs DOC-G1 bis DOC-G5 vermeiden Kollisionen mit bestehenden W-IDs.

| ID | Beobachtung | Nächste Klärung |
|---|---|---|
| DOC-G1 | ui.init fehlt in Spec 44.3; expliziter Aufruf wurde als praktisch nötig gemeldet. `aiml_ui_begin` initialisiert jedoch selbst, wenn g_ui fehlt (`src/sys/ui_runtime.cpp`). | Konkrete Aufrufreihenfolge und frühere Style-/Widget-Zugriffe reproduzieren. Keine allgemeine Pflicht allein aus dem Bericht ableiten. |
| DOC-G2 | ui.button laut Tabelle bool, aber Test und ein Beispiel in Spec 44.3 verwenden i32; Runtime liefert int32_t. | C-ABI von AIML-Typ unterscheiden; Registrierung, Konvertierung, Tests und Dokumentation zu einem Vertrag zusammenführen. |
| DOC-G3 | input.init: ptr in 44.0, window_handle in 44.1. | Öffentliche Signatur und Migrationsbeispiele abgleichen; gleiche Repräsentation ersetzt keinen Typvertrag. |
| DOC-G4 | 43.2 nennt konkret gladLoadGLLoader(SDL_GL_GetProcAddress), erlaubt andernorts aber SDL/GLFW und GLAD/GLEW. Runtime nutzt GLEW, Engine laut 44.1 GLFW. | Loader und Fenster-Backend getrennt dokumentieren; SDL-Kontexte nicht pauschal für ungültig erklären. |
| DOC-G5 | aUV in 43.2 §4.1 auf Location 2; gl.create_mesh bindet Position=0, UV=1, Normal=2 (`src/sys/gl_runtime.cpp`). | Beispiel an Mesh-Vertrag anpassen und texturierten Referenzfall prüfen. Frei konfigurierte VAOs sind ein anderer Fall. |

## Empfohlene Priorität — noch keine Reparaturfreigabe

1. Spec-61-Befunde W10, D29, D23, D9 abgrenzen und reproduzieren; tatsächliche
   Besitzer und betroffene Anwendungen erfassen.
2. Numerische Korrektheit D19, D5, W1/D1 und W4 gemeinsam planen; bisherige
   Meldeentscheidungen respektieren.
3. DOC-G5 und UI-/Input-/Backend-Verträge nach Signaturabgleich bereinigen.
4. D10/D11 als JIT-Paket untersuchen; AOT bleibt bis zur Abnahme nutzbarer Weg.
5. Übrige Sprach-/Diagnostikthemen und fehlende Texturunterstützung separat planen.

## Spec 61 als Abnahmekriterium

Jede spätere Reparatur benötigt positive/negative Regressionen, native Funktions-
prüfung und getrennte Speicherprüfung. Kein Abschalten von Lifetime-Drops und
kein persistent zum Verdecken eines Ownership-Fehlers. Rückgaben, temporäre Werte,
Fehlerpfade, Aliasing und Container-Elementfreigaben ausdrücklich abdecken.

Ein grüner Safety-Exit ist kein Leak-Nachweis. Wiederholungen im selben Prozess,
Compiler-/Runtime-Baseline und tatsächliche Leak-Zahlen vergleichen. Class-6-
Lebenszeit, Verluste pro Öffnung und Verluste pro Verarbeitungsschritt getrennt
bewerten. Keine Aussage über aktuelle Leak-Freiheit allein aus dieser Notiz.
