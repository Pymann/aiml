# AIML sys.serialize / sys.deserialize Nested Types Report

Datum: 2026-04-02

## Kurzfazit

Verschachtelte Datentypen sind aktuell nur teilweise robust.

Es gibt zwei getrennte Serialisierungspfade:

- JSON-basierter Pfad fuer `sys.serialize` / `sys.deserialize` in [serialize_impl.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/serialize_impl.cpp)
- binaerer Descriptor-Pfad `aiml_ser_*` in [ser_runtime.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/sys/ser_runtime.cpp)

Der JSON-Pfad ist fuer einige wichtige Nested-Faelle brauchbar:

- struct mit nested struct
- struct mit `list<T>`
- struct mit `list<struct>`
- struct mit `list<list<primitive>>`

Er ist aber nicht allgemein rekursiv korrekt. Insbesondere Maps sind der groesste Schwachpunkt.

Der binaere Descriptor-Pfad ist konzeptionell generischer und fuer `struct/list/map` deutlich naeher an echter Rekursion, hat aber eigene offene Punkte bei `buffer<T>` und bei der Fehlerhaertung.

## Verifizierte Laufzeitbefunde

Positiv verifiziert:

- [test_serialize_struct.aiml](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/tests/test_serialize_struct.aiml) laeuft erfolgreich
- [test_serialize_card.aiml](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/tests/test_serialize_card.aiml) laeuft erfolgreich
- [test_serialize_ext.aiml](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/tests/test_serialize_ext.aiml) laeuft erfolgreich

Damit ist aktuell konkret belegt:

- `list<struct>` funktioniert fuer den JSON-Pfad
- `list<list<i32>>` funktioniert fuer den JSON-Pfad
- ein Card-aehnlicher Nested-Fall mit `list<Ability>` und `Ability.effects: list<list<i32>>` funktioniert

Negativ verifiziert:

- `map<string:list<i32>>` im JSON-Pfad fuehrt zu einem Laufzeitcrash
- `map<ManaColor:i32>` im JSON-Pfad fuehrt zu einem Laufzeitcrash

Diese beiden Repros habe ich lokal direkt mit dem aktuellen AIML-Binary gefahren.

## Findings

### Kritisch: JSON-Maps sind faktisch nur fuer string->primitive/string implementiert

Die JSON-Schicht behauptet Rekursion, implementiert sie bei Maps aber nicht sauber.

Belege:

- Map-Schreibpfad fuer struct-Felder kennt nur `string`, `i64`, `f32`, `f64`, sonst faellt alles auf `i32` zurueck in [serialize_impl.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/serialize_impl.cpp#L252)
- Top-Level-Map-Schreibpfad hat dieselbe Einschraenkung in [serialize_impl.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/serialize_impl.cpp#L489)
- Map-Lesepfad fuer struct-Felder kennt nur primitive/string-Werte in [serialize_impl.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/serialize_impl.cpp#L714)
- Top-Level-Map-Lesepfad hat dieselbe Einschraenkung in [serialize_impl.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/serialize_impl.cpp#L900)

Folge:

- `map<string:list<i32>>`
- `map<string:struct>`
- `map<string:map<...>>`

sind trotz `isSerializable(...)` in der JSON-Schicht nicht korrekt umgesetzt und koennen crashen oder falsch serialisieren.

### Kritisch: JSON-Maps ignorieren den Key-Typ und behandeln Keys effektiv als `string`

Die JSON-Schicht liest und schreibt Map-Keys als `i8*`/String.

Belege:

- Beim Schreiben wird `kp` immer als `i8pT` gelesen in [serialize_impl.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/serialize_impl.cpp#L284) und [serialize_impl.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/serialize_impl.cpp#L535)
- Beim Lesen wird fuer Maps hart `ctx.getLLVMType("string")` als Key-Typ verwendet in [serialize_impl.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/serialize_impl.cpp#L716) und [serialize_impl.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/serialize_impl.cpp#L906)

Folge:

- `map<ManaColor:i32>` ist im JSON-Pfad strukturell inkonsistent
- der Laufzeitcrash fuer den Repro mit Enum-Key-Map ist damit technisch erklaerbar

Das ist auch deshalb problematisch, weil [test_serialize_card.aiml](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/tests/test_serialize_card.aiml) bereits `map<ManaColor:i32>` benutzt, aber nur den Deserialize-only-Fall ohne Roundtrip oder Map-Inhaltstest abdeckt.

### Hoch: `isSerializable()` ist fuer den JSON-Pfad zu permissiv

`isSerializable()` erlaubt rekursiv `list<T>` und `map<K:V>`, solange der Value-Typ rekursiv serialisierbar ist, siehe [serialize_impl.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/serialize_impl.cpp#L958).

Das passt nicht zur echten JSON-Implementierung, die:

- bei Lists nur `list<primitive>`, `list<struct>`, `list<list<primitive>>` sichtbar behandelt
- bei Maps nur primitive/string-Werte korrekt behandelt

Folge:

- Der Compiler akzeptiert Typen, die der Laufzeitpfad nicht robust verarbeiten kann.
- Das fuehrt zu Abstuerzen statt zu sauberen Compilerfehlern.

### Hoch: Nested-Lists sind im JSON-Pfad nur eine Ebene tief speziell implementiert

Es gibt gezielte Sonderfaelle fuer `list<list<T>>`, aber keine allgemeine rekursive Listen-Implementierung.

Belege:

- Schreibpfad: Sonderfall `isListType(elemTn)` in [serialize_impl.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/serialize_impl.cpp#L183) und [serialize_impl.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/serialize_impl.cpp#L424)
- Lesepfad: derselbe Sonderfall in [serialize_impl.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/serialize_impl.cpp#L668) und [serialize_impl.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/serialize_impl.cpp#L850)

Folge:

- `list<list<list<i32>>>` ist nicht als generischer Fall abgesichert
- `list<list<struct>>` ist ebenfalls nicht sauber ersichtlich unterstuetzt

### Mittel: Spec und Implementierung laufen beim JSON-Pfad auseinander

Die Spec beschreibt Nested-Maps und Nested-Lists deutlich allgemeiner, siehe [29_serialize.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/spec/29_serialize.md#L90).

Die echte Implementierung ist enger:

- Map-Keys praktisch `string`
- Map-Werte praktisch primitive/string
- Listen nur mit begrenzten Sonderfaellen verschachtelt

Das fuehrt zu einer false sense of support.

### Mittel: Binary Descriptor-Pfad ist fuer `struct/list/map` generischer, aber `buffer<T>` ist nicht tief serialisiert

Im binaeren Pfad werden `struct`, `list` und `map` rekursiv ueber `enc/dec/fre` traversiert, siehe [ser_runtime.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/sys/ser_runtime.cpp#L147), [ser_runtime.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/sys/ser_runtime.cpp#L157) und [ser_runtime.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/sys/ser_runtime.cpp#L171).

Bei `buffer<T>` wird aber nur ein Rohbyteblock kopiert:

- Encode macht `w_raw(...)` in [ser_runtime.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/sys/ser_runtime.cpp#L189)
- Decode macht `memcpy(...)` in [ser_runtime.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/sys/ser_runtime.cpp#L309)
- Free traversiert `buffer` aber rekursiv ueber Elemente in [ser_runtime.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/sys/ser_runtime.cpp#L401)

Folge:

- `buffer<primitive>` ist plausibel ok
- `buffer<string>` oder `buffer<struct-with-heap>` ist inkonsistent und sehr wahrscheinlich falsch

### Mittel: Binary Decode validiert nur List-Counts hart

Listen haben eine Absurditaetspruefung in [ser_runtime.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/sys/ser_runtime.cpp#L264).

Maps und Buffer haben keine vergleichbare harte Count/Size-Pruefung:

- Map in [ser_runtime.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/sys/ser_runtime.cpp#L287)
- Buffer in [ser_runtime.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/sys/ser_runtime.cpp#L309)

Folge:

- kaputte oder manipulierte Blobs koennen unnoetig grosse Allokationen ausloesen

### Niedrig: Debug-Ausgabe ist noch in der Binary-Decode-Implementierung aktiv

In [ser_runtime.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/sys/ser_runtime.cpp#L248) gibt es noch `fprintf(stderr, "[ser-pos] ...")`.

Das ist fuer Produktivpfade noisig und sollte raus oder hinter ein Debug-Flag.

## Aktueller Supportstand

### JSON `sys.serialize/sys.deserialize`

Robust bzw. praktisch bestaetigt:

- primitive
- enum
- struct/class/model mit primitiven Feldern
- nested struct
- `list<primitive>`
- `list<struct>`
- `list<list<primitive>>`

Nur teilweise oder nicht robust:

- `map<string:primitive>`: eher ja
- `map<non-string-key:V>`: nein
- `map<string:struct/list/map>`: nein
- `list<list<struct>>`: unklar und nicht generisch
- `list<map<...>>`: nein
- `buffer<T>`: ueber `sys.serialize` aktuell gar nicht als echter JSON-Typpfad vorgesehen

### Binary `aiml_ser_*`

Robust wirkend:

- struct
- nested struct
- list rekursiv
- map rekursiv

Offen/problematisch:

- `buffer<T>` nur shallow
- fehlende harte Plausibilitaetschecks fuer Map/Buffer-Groessen
- Rest-Debug-Ausgaben

## Empfohlene Fix-Reihenfolge

### Phase 1: JSON-Map-Pfad sofort absichern

Aufwand:

- etwa 0.5 bis 1 Tag

Inhalt:

- `isSerializable()` fuer den JSON-Pfad an die echte Implementierung anpassen
- `sys.serialize/sys.deserialize` fuer nicht unterstuetzte Map-Formen als Compilerfehler ablehnen
- mindestens diese Typen vorerst blocken:
  - `map<K:V>` mit `K != string`
  - `map<string:V>` falls `V` nicht primitive/string ist

Nutzen:

- verhindert die aktuell belegten Laufzeitcrashs sofort

### Phase 2: JSON-Maps wirklich rekursiv machen

Aufwand:

- etwa 1.5 bis 3 Tage

Inhalt:

- Key-Typ sauber parsen statt implizit `string`
- Map-Werte analog zu Lists rekursiv serialisieren/deserialisieren
- Unterstuetzung fuer:
  - `map<string:list<T>>`
  - `map<string:struct>`
  - optional `map<string:map<...>>`

Nutzen:

- bringt den JSON-Pfad auf das Niveau, das Spec und `isSerializable()` derzeit andeuten

### Phase 3: JSON-List-Rekursion verallgemeinern

Aufwand:

- etwa 1 bis 2 Tage

Inhalt:

- die harten Sonderfaelle `list<struct>` und `list<list<T>>` durch einen allgemeinen rekursiven Emitter/Reader ersetzen

Nutzen:

- weniger ad-hoc-Code
- besseres Verhalten fuer tiefer verschachtelte Listen

### Phase 4: Binary `buffer<T>` sauber definieren

Aufwand:

- etwa 0.5 bis 1.5 Tage

Es gibt zwei saubere Optionen:

- `buffer<T>` auf POD/raw-bytes beschraenken und alles andere compile-time ablehnen
- oder `buffer<T>` rekursiv wie `list<T>` serialisieren

Mein Vorschlag:

- kurzfristig: nicht-POD `buffer<T>` hart verbieten

### Phase 5: Binary Decode haerten

Aufwand:

- etwa 0.25 bis 0.5 Tag

Inhalt:

- Count-/Size-Limits fuer Map und Buffer
- Rest-Debug-`fprintf` entfernen

## Empfehlung

Die wichtigste Sofortmassnahme ist nicht "mehr Features", sondern das Schliessen der falschen Freigaben im JSON-Pfad.

Mein pragmatischer Vorschlag:

1. JSON-Maps sofort auf den wirklich unterstuetzten Bereich einschränken
2. erst danach echte rekursive Map-Unterstuetzung bauen
3. parallel den Binary-Pfad nur noch fuer die Typen erlauben, die dort wirklich tief korrekt serialisiert werden

## Einschätzung

Wenn du nur einen sicheren Stand willst:

- 0.5 bis 1 Tag fuer Guardrails und Compilerfehler

Wenn du echten guten Nested-Type-Support im JSON-Pfad willst:

- grob 2.5 bis 4.5 Tage insgesamt

## Methodik

Das ist eine Kombination aus:

- statischer Codeanalyse
- Laufzeittests der vorhandenen Serialisierungs-Tests
- zwei zusaetzlichen Crash-Repros fuer problematische Map-Faelle
