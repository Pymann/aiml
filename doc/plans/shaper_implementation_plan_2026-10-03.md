# Shaper — Anforderungen und Umsetzungsplan

Stand: 2026-10-03. Planungsstand, noch keine shaper-Implementierung.

**Historischer Ausgangsplan:** Shaper und glbview sind inzwischen implementiert.
Die ursprünglichen Fehlstellen unten sind keine aktuelle Restarbeitenliste.
Siehe [Statusabgleich vom 2026-10-04](shaper_open_topics_2026-10-04.md).

## 1. Ziel und vereinbarter Umfang

Shaper wird ein eigenständiges AIML-Programm. Es liest eine deklarative
YAML-Modellbeschreibung und erzeugt ein GLB-Modell samt UV-Koordinaten und
optional einer UV-Vorlage zum Bemalen. Exodus kann den Build ausführen;
shaper selbst ist kein Python-/Exodus-Tool.

Die Eingabe beschreibt Körper, offene Flächen und Punkte, Transformationen,
Materialien, Texturen und verschachtelte Geometrieoperationen.

- `union`: zuerst umsetzen. Geschlossene Körper boolesch vereinigen.
- `hull`: danach ergänzen. Konvexe Hülle der Eingabegeometrie bilden;
  Einbuchtungen und Zwischenräume können dabei überbrückt werden.
- Offene Flächen sind ohne Extrusion gültig, beispielsweise texturierte
  Blattflächen eines Baums. Außerhalb der Körper bleiben sie erhalten.
  Teilweise innenliegende Flächen werden geschnitten; vollständig innenliegende
  Teile entfallen. Offene Flächen entfernen untereinander kein Volumen.
- Reine Punkte bestimmen keine eindeutige nichtkonvexe Oberfläche. Sie benötigen
  eine explizite Flächenzuordnung oder `hull`. Punktwolken-Rendering ist kein
  Ersatz für diese Festlegung.
- Alle erzeugten Oberflächen erhalten UVs. Vorhandene UVs lassen sich erhalten;
  alternativ wird das Ergebnis neu abgewickelt.
- Materialien unterstützen insbesondere transparente Blatttexturen und
  beidseitige Darstellung. Transparenz im Bild verändert nicht die Geometrie.
- Differenz und Schnittmenge sind mögliche spätere Erweiterungen, nicht Teil
  der ersten Umsetzung.

## 2. YAML-Vertrag

Versioniertes Format mit `version`, `units`, `up_axis`, `materials`, `model`,
`uv` und `output`. Der Modellbaum besteht aus Formen und Operationen mit Kindern.
Namen und Syntax sind ein Entwurf und werden vor Implementierung festgeschrieben.

```yaml
version: 1
units: meter
up_axis: y

materials:
  leaves:
    texture: textures/leaves.png
    alpha_mode: mask
    alpha_cutoff: 0.5
    double_sided: true

model:
  operation: union
  children:
    - type: box
      size: [0.3, 3, 0.3]
      position: [0, 1.5, 0]
    - type: surface
      points: [[-1, 2, 0], [1, 2, 0], [1, 3, 0], [-1, 3, 0]]
      uv: [[0, 0], [1, 0], [1, 1], [0, 1]]
      material: leaves

uv:
  mode: preserve_or_generate
  atlas_size: [2048, 2048]
  padding_px: 8

output:
  file: tree.glb
  uv_layout: tree_uv.png
```

Festzulegen: Ursprung jeder Form (Quader: Mittelpunkt; Pyramide: Mitte der
Grundfläche), Händigkeit, Winkelmaß, Transformationsreihenfolge, Außenorientierung
der Flächen und Texturkoordinaten-Konvention. Ressourcenpfade werden relativ zur
YAML-Datei aufgelöst. Unbekannte Schlüssel, ungültige Indizes, nichtendliche Werte
und ungültige Dimensionen liefern Fehler mit YAML-Pfad und Quellposition.

Für `union` zusätzlich deterministische Regeln für Berührungen und koplanare
Flächen festlegen, einschließlich offener Flächen genau auf einer Körpergrenze.
Explizite Flächen sind zunächst einfache planare Polygone; Löcher und nichtplanare
Eingaben werden erst mit einem definierten Verfahren unterstützt, sonst abgewiesen.

## 3. Bestandsaufnahme AIML

Vorhanden und anhand von Code sowie vorhandenen Tests geprüft:

- YAML-Parsing und Traversierung einschließlich Fehlern und Quellpositionen:
  `src/sys/yaml_runtime.cpp`, `tests/test_yaml_documents.aiml`.
- Modelle, Container, dynamische Arrays und typisierte Byte-/Zahlenbuffer.
- Vektor-/Matrix-/Quaternion-Mathematik: `src/sys/math_runtime.h`.
- Binäres Lesen: `src/sys/binary_io_runtime.cpp`, `tests/test_binary_io.aiml`.
- C-FFI und Buffer-Datenzeiger. Ein separater Test in `/tmp` schrieb über
  `fwrite` erfolgreich Nullbytes und einen `f32`-Wert; die Bytes wurden geprüft.
- GLB/glTF-Import: `src/sys/gltf_runtime.cpp`, `tests/test_gltf.aiml`.
- OpenGL-Mesh-Erzeugung im Code vorhanden; im Rahmen dieser Prüfung kein
  interaktiver Renderingtest. Ein `gl_mesh` ersetzt kein bearbeitbares CPU-Mesh.

Die sieben Funktionstests `test_yaml_documents`, `test_binary_io`, `test_buffer`,
`test_math`, `test_dyn_array`, `test_ffi`, `test_gltf` liefen erfolgreich mit dem
vorhandenen Compiler. Das ist weder eine vollständige Sprachprüfung noch ein
Leak-Nachweis. Einige Spezifikationen markieren bereits implementierte Funktionen
noch als Vorschlag; Code, Spec und Tests müssen zusammen betrachtet werden.

## 4. Was in AIML ergänzt werden muss

### 4.1 Allgemeine Runtime-/Bibliotheksfunktionen

1. **Binäre Dateiausgabe:** öffentliche längenbasierte Schnittstelle wie
   `io.write_bytes(handle, buffer)` mit Fehler-/Kurzschreibbehandlung.
   `io.open(path, "wb")` ist vorhanden; `io.write`/`io.write_file` sind textorientiert.
   FFI ist ein geprüfter Übergangsweg, keine fehlende Sprachfähigkeit.
2. **Binäres Packen:** explizites Little-Endian-Schreiben von Ganzzahlen und
   Gleitkommazahlen sowie Padding und Größenprüfung als wiederverwendbare Helfer.
   `buffer.write` speichert derzeit native Werte; keine plattformunabhängige
   Endianness aus der Dokumentation allein ableiten. Für GLB Felder einzeln packen,
   keine AIML-Structs samt Alignment/Padding ungeprüft übernehmen.
3. **PNG-Ausgabe:** RGBA-Buffer nach PNG kodieren, wenn die UV-Vorlage als PNG
   erzeugt wird. Bilddekodierung und WebP-Kodierung sind bereits vorhanden,
   ein entsprechender PNG-Encoder wurde nicht gefunden. Eine SVG-Vorlage kann
   zunächst als Text ausgegeben werden; sie ersetzt nicht die geplante PNG-Ausgabe.
4. **Spec-61-Integration jeder Erweiterung:** Ownership-Verträge, Freigabewege,
   temporäre Rückgabewerte und Fehlerpfade im Compiler/Runtime registrieren und
   prüfen. Neu erzeugte Buffer müssen echte verwaltete Class-3-Handles sein.
   Kein roher Heap-Block, der lediglich zufällig dasselbe Nutzdatenlayout besitzt.

Neue Sprachsyntax ist dafür nach aktuellem Befund nicht erforderlich.
Gemeinsame Bibliothekshelfer können in AIML implementiert werden; native Runtime-
Funktionen sind dort sinnvoll, wo Datei- oder Bildcodec-Schnittstellen es erfordern.

### 4.2 Neue AIML-Module für shaper

Diese Funktionalität fehlt, gehört aber nicht zwingend als Builtin in den Compiler:

| Modul | Aufgabe |
|---|---|
| `schema` | YAML validieren, Defaults, Transformationen, Ressourcenpfade |
| `mesh` | CPU-Mesh, Flächenecken, Topologie, Materialien, Validierung |
| `primitives` | Quader, Pyramiden, weitere Körper und explizite Flächen |
| `geometry` | Schnitte, Klassifikation, Triangulierung, Toleranzregeln |
| `boolean` | Körpervereinigung und Beschneiden offener Flächen |
| `hull` | Konvexe Hülle; degenerierte Punktmengen diagnostizieren |
| `uv` | Projektion, UV-Erhalt, Abwicklung und Atlas-Anordnung |
| `normals` | Normalen, harte Kanten, optional Tangenten für Normalmaps |
| `gltf_export` | GLB-Container, JSON, Geometrie, Materialien und Texturen |
| `uv_layout` | Sichtbare UV-Vorlage als SVG/PNG |

Geometrie intern mit eigenen `f64`-Wertetypen berechnen; vorhandene `vec3`/`mat4`
verwenden `f32`. Robuste Seiten-/Schnittentscheidungen benötigen zusätzlich eine
definierte Numerikstrategie. `f64` allein garantiert keine robusten Booleans.

## 5. Mesh-, UV- und Exportregeln

- Geometrische Punkte von Flächenecken trennen. Jede Flächenecke kann eigene
  UVs und Normalen besitzen, auch wenn mehrere Ecken denselben Punkt referenzieren.
- Topologie zunächst über Indizes/IDs in flachen Containern abbilden.
  Körper und offene Flächen bleiben unterscheidbar.
- Schnitte erhalten Herkunft und Material-ID. Neue Schnittpunkte interpolieren
  die bestehenden Flächeneckattribute, insbesondere UVs; Normalen bei Bedarf
  neu berechnen beziehungsweise normalisieren.
- UV-Erhalt: vorhandene Texturabbildung bleibt bestehen; bewusst überlappende
  Blatt-UVs sind erlaubt. Fehlende UVs werden erzeugt.
- Neuabwicklung: Inseln bilden und mit festgelegter Texeldichte, Auflösung und
  Abstand packen. Neue Inseln dürfen sich im eindeutigen Malatlas nicht überlagern.
- Bestehende Texturen auf ein neues Layout übertragen erfordert Baking. Dies
  ist eine separate spätere Ausbaustufe; ohne Baking keine Behauptung, eine
  Neuabwicklung erhalte automatisch das bisherige Aussehen.
- GLB-Export: Positionen, Normalen, UVs, Indizes, Materialgruppen, Texturen,
  `alphaMode`, `alphaCutoff`, `doubleSided`, Szene und Knoten. Vertices an UV-/
  Normalennähten beim Export passend aufteilen.
- GLB-Größen und Ausrichtung prüfen; keine stillen Integer-Überläufe. Bilder
  können zunächst als vorhandene PNG-/JPEG-Dateien eingebettet werden, ohne sie
  neu zu kodieren. Der PNG-Encoder ist dafür nicht Voraussetzung.
- Ausgabe zunächst in temporäre Datei schreiben und nach Erfolg übernehmen.
  Fehler dürfen kein scheinbar fertiges, beschädigtes Zielmodell hinterlassen.

## 6. Verbindliche Speicherregeln — SPEC 61

**Spec 61 ist die zentrale Entwurfs- und Abnahmegrundlage für shaper und alle
dafür ergänzten AIML-Funktionen.** Sie trägt derzeit noch den Titel DRAFT;
dieser Plan behauptet weder ihre vollständige Implementierung noch eine bereits
gemessene Leak-Freiheit. Abweichungen im tatsächlich verwendeten Compilerstand
müssen durch Proben erkannt und vor Freigabe behoben werden.

Referenz: [Spec 61](../../spec/61_memory_management_DRAFT.md), insbesondere
§2.1, §5–5.7, §6.2, §7, §8, §9.1/9.3 und §10.

| Ressource | Ownership-Vertrag für shaper |
|---|---|
| Zahlen, Punkte, Dreiecksindizes | Kleine Werttypen; keine eigenen Heap-Besitzer |
| `string`, `path` | Class 2; verwaltete Handles, kein manuelles C-`free` |
| `list`, `map`, `buffer` | Class 3; Zuweisung teilt Speicher per RC, keine unabhängige Kopie |
| `ptr<Mesh>` / andere `ptr<T>` | Ein Besitzer; Übergabe/Zuweisung als Besitztransfer, Quelle danach unbenutzbar |
| `ref<Mesh>` | Nicht besitzender Funktionszugriff; darf Besitzer nicht überleben |
| `shared<T>` | Nur bei notwendigem gemeinsamen Besitz; Zyklen vermeiden |
| `dyn_array<T>` | Class 4, einmaliger Besitzer; Move statt Alias, Freigabe bei Owner-Drop |
| `yaml_doc` | Besitzt den Parse-Baum; wird nach Übernahme der Eingabe freigegeben |
| `yaml_node` | Geliehene Sicht; nie über die Dokumentlebenszeit hinaus speichern |
| Datei-/native Codec-Handles | Eindeutiger Besitzer mit definierter Close-/Destroy-Funktion |

Konkrete Pflichten:

1. **Ein Besitzer pro Ergebnis:** Parser liefert eine eigene Modellbeschreibung;
   Evaluator liefert ein eigenes Mesh. Funktionen, die nur lesen oder bearbeiten,
   nehmen `ref<Model>`/`ref<Mesh>`, keine versehentlich konsumierenden `ptr`-Parameter.
   Containerparameter folgen §8: vorhandener RC-Handle, kein `ref<list/map/buffer>`.
2. **Keine Ownership-Zyklen:** Mesh besitzt seine Daten; Kanten, Nachbarn,
   Elternverweise und Materialzuordnungen verwenden IDs/Indizes oder zeitlich
   begrenzte Borrows. Auch eindeutige `ptr`-Besitzer verhindern Zyklen nicht allein.
3. **Aliasing ausdrücklich behandeln:** `set` eines Containers erzeugt keine
   unabhängige Mesh-Kopie. Für unabhängige Daten ein geprüftes `clone` oder gezielten
   Neuaufbau verwenden. Wachstum/Entfernung kann Element-Borrows invalidieren;
   keine Referenzen über solche Mutationen hinweg aufbewahren.
4. **Scopes begrenzen:** Schnittlisten, Polygonfragmente, UV-Arbeitsdaten und
   Exportbuffer möglichst pro Operation/Iteration besitzen und freigeben.
   Kein `persistent` als Workaround für eine falsche Lebenszeit.
5. **Alle normalen Ausgänge freigeben:** Erfolg, frühes `return`, Schleifenabbruch
   und behandelter Exception-Unwind müssen korrekt aufräumen. Explizite frühe
   Freigabe darf nicht zu einem zweiten Scope-Drop führen. Erwartete Eingabe- und
   Exportfehler bis zur CLI zurückreichen, nicht tief im Algorithmus mit
   `sys.exit` beenden und dadurch fehlendes Cleanup verdecken.
6. **Temporäre Ergebnisse prüfen:** Auch verschachtelte Producer-Aufrufe,
   Methodenketten und ungenutzte Ergebnisse müssen freigegeben oder übernommen
   werden. Beim Wurf während eines späteren Arguments müssen bereits erzeugte
   temporäre Werte korrekt aufgeräumt werden.
7. **FFI-Verträge dokumentieren:** Für jeden Parameter/Rückgabewert borrowed,
   owned oder consumed festlegen, einschließlich passendem Allocator/Deallocator.
   `buffer.ptr` nur während der gültigen Buffer-Lebenszeit und ohne Reallokation
   verwenden. Native Funktionen dürfen geliehene Zeiger nicht dauerhaft speichern.
8. **Fehler bei Teilaufbau:** Bereits angelegte Mesh-Teile, Bilder und Dateien
   müssen auch bei nachfolgendem Fehler freigegeben werden. Native Runtime-Codepfade
   mit RAII oder vollständig geprüften Cleanup-Pfaden absichern.
9. **Zunächst ein Thread:** Kein gleichzeitiges Mutieren geteilter Class-3-Container.
   Spätere Parallelisierung benötigt einen eigenen Spec-61-§5.3-Nachweis.

## 7. Umsetzung in überprüfbaren Etappen

### P0 — Verträge und Speicher-Baseline

- YAML-Konventionen und das CPU-Mesh-Modell festschreiben.
- Ownership je Datenstruktur und öffentlicher Funktion dokumentieren.
- Tatsächliche Compiler-/Runtime-Version und aktive Lifetime-Einstellungen erfassen;
  alte Kommentare oder Feature-Gates nicht als verlässlichen Status übernehmen.
- Relevante vorhandene Spec-61-Proben ausführen. Besonders YAML, Containerfelder,
  verschachtelte Container, Buffer, `dyn_array`, Rückgaben und Call-Temporaries.
- Gegebenenfalls reproduzierte AIML-Lifetime-Fehler zuerst beheben.

### P1 — YAML bis GLB ohne boolesche Operation

- Binärausgabe und Packing-Helfer ergänzen; Leaks und Schreibfehler testen.
- Quader, einzelne offene Fläche, explizite UVs und Materialdaten lesen/erzeugen.
- Minimalen GLB-Exporter bauen; erzeugtes Modell wieder in AIML laden.
- Textur und beidseitige transparente Blattfläche ergänzen.
- Formatvalidierung und Datenvergleich; Rückimport allein ist keine vollständige
  GLB-Konformitätsprüfung.

### P2 — Boolesche Vereinigung und offene Flächen

- Zuerst zwei überlappende Quader, dann Pyramiden und weitere gültige Polyeder.
- Triangulierung, Innen-/Außenprüfung und gemeinsame Schnittgeometrie entwickeln.
- Außenliegende Flächen erhalten, innenliegende Fragmente entfernen; Material
  und UVs an neuen Schnittpunkten erhalten.
- Disjunkte, eingeschlossene, identische, berührende und koplanare Fälle prüfen;
  ungültige beziehungsweise nicht unterstützte Eingaben diagnostizieren.
- Gültige geschlossene Körper müssen geschlossen bleiben; offene Blattflächen
  dürfen durch die Validierung nicht irrtümlich als fehlerhafte Körper gelten.

### P3 — UVs für sämtliche gültigen Oberflächen

- Analytische Abwicklung einfacher Körper und planare Flächenprojektion.
- Generische Inselbildung und Atlas-Anordnung für komplexe Resultate.
- Normalen und harte Kanten konsistent erzeugen.
- UV-Vorlage zunächst SVG, anschließend PNG mit ergänztem Encoder/Rasterizer.
- UV-Erhalt und Neuabwicklung getrennt prüfen. Entartete Flächen zurückweisen,
  statt ungültige UVs oder NaN-Werte zu exportieren.

### P4 — Konvexe Hülle

- `hull` in denselben Modellbaum integrieren; Punktmengen und Körper unterstützen.
- Doppelte, kollineare und koplanare Punkte sowie zu wenige Punkte diagnostizieren.
- Neue Hüllflächen mit UVs versehen. Materialzuordnung explizit regeln;
  neu entstandene Flächen besitzen nicht automatisch eine eindeutige Quelltextur.

### P5 — Größere Modelle und Abschlussabnahme

- Mehrere Exportaufträge innerhalb eines Prozesses ausführen, einschließlich
  wiederholter Fehlerfälle, ohne wachsende nicht freigegebene Auftragsdaten.
- Große Blattmengen, Größenlimits, Performance und Spitzenverbrauch messen.
- GLB-Formatprüfung, Rückimport und visuelle Prüfung zusammenführen.
- Baking bestehender Texturen bei Neuabwicklung als gesonderte Folgeetappe planen.

## 8. Leak- und Sicherheitsabnahme pro Etappe

**Keine Etappe gilt allein wegen korrekter Geometrie oder Exit-Code 0 als fertig.**

- Native Funktionstests getrennt von Speicherprüfungen ausführen.
- Valgrind Memcheck muss sowohl generierten AIML-Code als auch Runtime erfassen;
  vorhandene Kontrollproben und Harness nutzen. AOT zusätzlich prüfen, sobald
  der ausführbare shaper-Build steht. ASan/LSan nur mit passender Instrumentierung
  als Ergänzung, nicht als unbewiesenen Ersatz für die JIT-Prüfung verwenden.
- Keine Invalid Reads/Writes, Use-after-free, Double-free oder uninitialisierten
  Zugriffe in den neuen Pfaden.
- Keine neuen shaper-bedingten definite/indirect/possible Leaks; unklare Meldungen
  auflösen. Noch erreichbare Speicherblöcke separat bewerten: nach Auftragsende
  darf kein auftragsabhängig wachsender Bestand zurückbleiben. Runtime-/JIT-Caches
  mit identischer Baseline vergleichen und begründen, nicht pauschal ausblenden.
- Leak-Zahlen ausdrücklich auswerten: `tests/run_memory_sanitizers.sh` verwendet
  `--errors-for-leak-kinds=none`; ein grüner Safety-Exit ist kein Leak-Nachweis.
- Wiederholungsproben mit unterschiedlich vielen Aufträgen im selben Prozess;
  native Heap-/RSS-Trends nur ergänzend, nicht als alleinigen Beweis nutzen.
- Fehlerproben: ungültiges YAML, fehlende Textur, ungültiges Mesh, Abbruch nach
  Teilaufbau, nicht beschreibbare Ausgabe und simuliertes Kurzschreiben.
- Cleanup-Proben: Move-Rückgabe, Alias-Rebind, frühes Return, Exception-Unwind,
  Loop-Temporaries und explizite frühe Freigabe mit anschließendem Scope-Ende.
- Speicherberichte, Compilerstand, Laufparameter und erwartete Eigentümer als
  Abnahmeartefakte speichern. Keine Freigabe mit ungeklärter Regression.

Relevante bestehende Grundlagen:

- [Spec 61](../../spec/61_memory_management_DRAFT.md)
- [Memory-Sanitizer-Harness](../../tests/run_memory_sanitizers.sh)
- [Call-Temporary-Cleanup-Plan](spec61_call_temporary_cleanup_implementation_2026-10-02.md)
- `tests/lifetime/163_yaml_doc_scope_release.aiml`
- `tests/lifetime/141_buffer_handle_set_and_param.aiml`
- `tests/lifetime/21_dyn_array_class4.aiml`
- `tests/lifetime/call_temporaries/`

Die vorhandenen Pläne dienen als Prüfhinweise, nicht als Behauptung, ihre früheren
Fehler seien im aktuellen Compiler noch offen. Vor Änderungen reproduzieren.
