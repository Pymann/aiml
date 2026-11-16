# Implementierungsplan: Erkennung unbenutzter Enumeratoren in AIML

## Ziel

Die statische AIML-Analyse soll eine nicht-blockierende Compiler-Diagnose erzeugen, wenn Enum-Mitglieder deklariert, aber innerhalb des aktuellen Builds nie verwendet werden.

Die Ausgabe soll **genauso wie andere statische Probleme** als Compiler-Diagnose erscheinen, nicht als Runtime-`log.*`-Aufruf.

Beispielrichtung:

```text
models/enums.aiml:12: warning: enum member 'PassiveAbility.armored' is never referenced in this build
```

## Gewuenschte Semantik fuer V1

### Was als Benutzung zaehlt

In der ersten Ausbaustufe zaehlen nur **explizite symbolische Referenzen** auf Enum-Mitglieder, insbesondere:

- `EnumName.Member`
- voll qualifizierte Enum-Zugriffe, soweit sie ueber denselben Resolver laufen

### Was in V1 noch nicht als Benutzung zaehlt

Diese Faelle sollen zunaechst **nicht** als sichere Verwendung gewertet werden:

- rohe Integerwerte, die zufaellig einem Enum-Mitglied entsprechen
- implizite Nutzung nur ueber Persistenz-/Netzwerkdaten
- reine Enum-Typverwendung ohne Bezug auf konkrete Mitglieder
- UI-Bindings wie `enum: SomeEnum` an Dropdowns oder Radios

Das ist absichtlich konservativ: V1 meldet nur das, was der Compiler mit hoher Sicherheit als **konkrete Member-Verwendung** sieht.

## Warum das in die bestehende Architektur passt

Die bestehende Compiler-/Static-Analysis hat bereits ein sehr aehnliches Muster fuer **whole-program**-Checks:

- Prefab-Deklarationen und Prefab-Verwendungen werden gesammelt in
  [context.h](../../src/core/context.h#L299)
- die Analyse laeuft in
  [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L269)
- die Funktion wird sowohl fuer Entry- als auch fuer `libraryMode`-Module aufgerufen in
  [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L968)
  und
  [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L1207)

Die Enum-Aufloesung existiert ebenfalls schon zentral:

- Enum-Registry in
  [context.h](../../src/core/context.h#L262)
- Enum-Registrierung in
  [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L1922)
- explizite Enum-Referenz-Aufloesung in
  [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp#L301)
  und
  [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L2911)

## Implementierungsstrategie

### Phase 1: Deklarierte Enum-Mitglieder erfassen

#### Ziel

Der Compiler soll fuer jedes Enum alle deklarierten Member inklusive Quellort registrieren.

#### Betroffene Stellen

- [ast_nodes.h](../../src/core/ast_nodes.h#L359)
- [context.h](../../src/core/context.h#L262)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L1922)

#### Konkrete Aenderungen

1. In `CompilerContext` neue Strukturen anlegen, analog zu `prefabDecls` / `prefabUses`.

Vorschlag:

```cpp
std::map<std::string, std::set<std::string>> enumDeclMembers;
std::map<std::string, std::set<std::string>> enumUsedMembers;
std::map<std::string, YAML::Node> enumDeclNodes;
std::map<std::string, std::map<std::string, YAML::Node>> enumMemberDeclNodes;
```

Minimal reicht fuer V1:

```cpp
std::map<std::string, std::set<std::string>> enumDeclMembers;
std::map<std::string, std::set<std::string>> enumUsedMembers;
std::map<std::string, std::map<std::string, YAML::Node>> enumMemberDeclNodes;
```

2. Beim Registrieren eines `EnumDecl` in `visitTopLevel(...)` zusaetzlich alle `values` in `enumDeclMembers[enumName]` eintragen.

3. Dabei fuer jedes Mitglied auch den bestmoeglichen Quellort hinterlegen.

#### Wichtige Anmerkung

`EnumDecl::values` speichert derzeit nur `name -> int`, nicht den Quellort pro einzelnes Mitglied
([ast_nodes.h](../../src/core/ast_nodes.h#L362)).

Deshalb gibt es zwei Optionen:

- **V1 pragmatisch:** Warnung auf Enum-Block-Position ausgeben
- **V1.5 sauberer:** AST fuer Enum-Mitglieder um Member-spezifische `YAML::Node`-Marks erweitern

Empfehlung:

- zuerst pragmatisch mit Enum-Block-Position
- spaeter Quellort pro Member nachruesten

## Phase 2: Verwendete Enum-Mitglieder erfassen

#### Ziel

Jede explizite symbolische Referenz wie `GamePhase.combat` soll als Verwendung markiert werden.

#### Betroffene Stellen

- [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp#L301)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L2911)

#### Konkrete Aenderungen

1. Direkt vor oder in `resolveEnum(baseName, propName)` eine Hook einbauen:

```cpp
ctx.enumUsedMembers[resolvedEnumName].insert(propName);
```

2. Falls Namespaces / Qualifizierung eine Rolle spielen, muss die **kanonische Enum-ID** verwendet werden, nicht nur `baseName`.

Deshalb ist die beste Stelle **innerhalb** von `resolveEnum(...)`, sobald sicher ist, welches Enum tatsaechlich aufgeloest wurde.

3. Falls `resolveEnum(...)` sowohl fuer globale als auch namespace-lokale Enums verwendet wird, muss die Speicherung dieselbe Qualifizierungslogik nutzen wie `ctx.enums`.

#### Warum diese Stelle gut ist

- nur echte, erfolgreich aufgeloeste Enum-Mitglieder werden gezaehlt
- keine doppelte AST-Walk-Logik noetig
- geringes False-Positive-Risiko

## Phase 3: Whole-Program-Merge wie bei dead-prefab

#### Ziel

Die Mitgliedsverwendungen aus importierten/libraryMode-Modulen sollen in den Top-Level-Compile zurueckfliessen.

#### Betroffene Stellen

Die exakten Merge-Stellen fuer `allPrefabUses` muessen in der Compiler-Driver-/Import-Pipeline gespiegelt werden. Der Plan orientiert sich dabei explizit an dem schon existierenden `dead-prefab`-Pfad, der in den Kommentaren von `CompilerContext` referenziert ist
([context.h](../../src/core/context.h#L303)).

#### Konkrete Aenderungen

1. Neue Aggregatstrukturen im uebergeordneten Compiler/Driver anlegen, analog zu:

- `allPrefabDecls`
- `allPrefabUses`

2. Beim Rueckmerge eines Sub-Compilers:

- deklarierte Enum-Mitglieder vereinigen
- verwendete Enum-Mitglieder vereinigen

3. Merge-Key muss der voll qualifizierte Enum-Name sein.

#### Risiko

Wenn diese Merges nicht sauber entlang der Importkette passieren, entstehen False Positives bei Modulen, die Enums importieren, aber deren Verwendung nur in Geschwistermodulen stattfindet.

## Phase 4: Top-Level-Diagnose ausgeben

#### Ziel

Am Ende des Top-Level-Builds soll fuer jedes deklarierte, aber nie verwendete Enum-Mitglied genau eine Warnung ausgegeben werden.

#### Betroffene Stellen

- derselbe Top-Level-Postpass, der bereits whole-program-Warnungen wie `dead-prefab` ausgibt
- Diagnoseformat ueber `fmtLoc(...)`-artige Pfade aus
  [context.h](../../src/core/context.h#L377)

#### Konkrete Aenderungen

1. Mengen-Differenz bilden:

```cpp
unused = enumDeclMembers[enumName] - enumUsedMembers[enumName]
```

2. Fuer jedes `unused`-Mitglied eine Warning ausgeben.

Vorschlag:

```text
<loc>: enum member '<Enum>.<Member>' is never referenced in this build
```

3. Die Diagnose soll nur im **vollstaendigen Build-Kontext** erscheinen, analog zur bestehenden Whole-Program-Gating-Idee bei `dead-prefab`.

#### Empfehlung fuer Gating

Warnung nur ausgeben, wenn der Use-Set als vollstaendig betrachtet werden kann:

- echter Build ohne warme Import-Abkuerzung
- oder Single-File ohne Imports

Damit bleibt das Verhalten konsistent mit
[44_8_prefab_static.md](../../spec/44_8_prefab_static.md#L131).

## Phase 5: Spec ergaenzen

#### Betroffene Dateien

- [49_static_analysis.md](../../spec/49_static_analysis.md#L449)
- optional neue dedizierte Spec-Datei, falls die Regel groesser beschrieben werden soll

#### Inhalt

Die Spec sollte festhalten:

- dass unbenutzte Enumeratoren eine Warning sind
- dass es sich um einen whole-program Check handelt
- dass die Aussage `in this build` bewusst build-spezifisch ist
- welche Verwendungsarten in V1 zaehlen
- welche bewusst noch nicht gezaehlt werden

## Phase 6: Tests

### Testgruppe A: Basisfaelle

Neue AIML-Tests anlegen, zum Beispiel unter `tests/`:

- ein Enum mit 3 Werten, davon 1 genutzt -> 2 Warnings
- ein Enum, dessen alle Werte genutzt werden -> keine Warning
- zwei Enums, nur eines teilweise genutzt -> Warnings nur fuer dieses

### Testgruppe B: Importfaelle

- Enum in importierter Datei definiert, im Entry verwendet
- Enum im Entry definiert, in importierter Datei verwendet
- Enum in importierter Datei definiert und nur in anderer importierter Datei verwendet

### Testgruppe C: Nicht-Zaehlfaelle fuer V1

- Enum-Typ als Feldtyp vorhanden, aber kein Member referenziert
- Widget mit `enum: SomeEnum`, aber kein `SomeEnum.member`
- Integerwert `3` an Stelle eines Enum-Members

Diese Tests sollten dokumentieren, dass solche Faelle **noch nicht** als Benutzung gelten.

## Empfohlene Reihenfolge der Umsetzung

1. Datenstrukturen fuer `enumDeclMembers` / `enumUsedMembers` in `CompilerContext`
2. Member-Erfassung bei Enum-Registrierung
3. Use-Erfassung in `resolveEnum(...)`
4. Merge in der Import-/Subcompiler-Pipeline
5. Top-Level-Warning-Ausgabe
6. Tests
7. Spec-Update

## Aufwand nach Teilpaketen

### Paket 1: V1-Kern

- Enum-Mitglieder deklarieren und nutzen sammeln
- Top-Level-Warnung ausgeben
- 2 bis 4 kleine Tests

Schaetzung:

- etwa 0.75 bis 1.25 Tage

### Paket 2: Sauberer Whole-Program-Ausbau

- Merge-/Gating robust machen
- Importfaelle absichern
- Spec sauber nachziehen

Schaetzung:

- zusaetzlich 0.5 bis 1 Tag

### Paket 3: Member-genauer Quellort

- AST/Builder fuer per-Member-Source-Locations erweitern

Schaetzung:

- zusaetzlich 0.25 bis 0.5 Tage

## Zukunft: Warning fuer Serialize/Deserialize-Mismatch bei Enums

## Problem

Es soll spaeter eine Warning geben, wenn Enum-Werte serialisiert werden, bei der Deserialisierung aber **nicht korrekt wieder als Enum behandelt** werden.

Das ist relevant, weil Enums aktuell serialisierungsseitig wie Integer behandelt werden:

- Serialisierung von Enums als Integer in
  [serialize_impl.cpp](../../src/core/serialize_impl.cpp#L1102)
- `sys.deserialize` kennt den Zieltyp ueber `deserializeTargetType` in
  [context.h](../../src/core/context.h#L451)
- Deserialisierungspfad in
  [serialize_impl.cpp](../../src/core/serialize_impl.cpp#L1150)

## Ziel des Zukunftschecks

Warnung erzeugen fuer Muster wie:

- Enum-typisierte Daten werden serialisiert
- spaeter aber in `i32`, `string` oder ein strukturell falsches Ziel zurueckdeserialisiert

Beispielrichtung:

```text
state.aiml:44: warning: serialized enum value from 'GamePhase' is deserialized into 'i32' instead of 'GamePhase'
```

## Empfohlene V2-Strategie

### 1. Diagnose auf Compiler-Ebene, nicht zur Laufzeit

Der Check soll dieselbe Diagnoseklasse wie andere Static-Analysis-Faelle verwenden, nicht `log.warn`.

### 2. Tracking-Punkte

Beim Kompilieren von `sys.serialize(...)` sollte vermerkt werden:

- Quelltyp `typeName`
- wenn `ctx.enums.count(typeName)` gilt: es handelt sich um Enum-Serialisierung
- idealerweise Quellort der Serialisierung

Beim Kompilieren von `sys.deserialize(...)` sollte vermerkt werden:

- Zieltyp `typeName`
- Quellort der Deserialisierung

### 3. Mismatch-Regeln

Warning, wenn:

- Quelle Enum, Ziel Nicht-Enum
- Quelle Enum `A`, Ziel Enum `B`

Keine Warning, wenn:

- Quelle Enum `A`, Ziel Enum `A`

### 4. Schwierigkeit

Das ist deutlich schwerer als `unused enumerator`, weil Serialize/Deserialize nicht immer lokal paarweise im selben Ausdruck auftreten.

Es braucht entweder:

- lokale Heuristik fuer direkte Muster
  `set: [x, { call: [sys.deserialize, { call: [sys.serialize, y] }, T] }]`
- oder spaeter eine Datenflussanalyse ueber temporale Werte

## Konkreter V2-Plan fuer einen ersten Heuristik-Check

### Betroffene Stellen

- [serialize_impl.cpp](../../src/core/serialize_impl.cpp#L1098)
- [serialize_impl.cpp](../../src/core/serialize_impl.cpp#L1150)
- Call-Dispatch/Statement-Compile-Pfade, in denen `sys.deserialize`-Zieltypen bekannt sind

### Heuristische V2-Regel

Zunaechst nur direkte AST-Muster pruefen:

- `sys.deserialize(sys.serialize(expr), TargetType)`
- `decl/set` mit klar erkennbarem enum-typisiertem Ursprung und unpassendem Zieltyp

### Erwartete Diagnose

Nicht-blockierende Warning:

```text
...: serialized enum value from 'Status' is deserialized as 'i32'; prefer deserializing back into 'Status'
```

## Empfehlung

Die beiden Themen sollten getrennt umgesetzt werden:

1. **jetzt**: `unused enumerator` als simpler, robuster whole-program Check
2. **spaeter**: Serialize/Deserialize-Mismatch als eigener Heuristik-Check

So bleibt die erste Lieferung klein, klar und risikoarm.

## Fazit

Die Erkennung unbenutzter Enumeratoren ist architektonisch ein sehr guter Fit fuer die bestehende AIML-Static-Analysis. Die sauberste V1 ist:

- Use-Erfassung in `resolveEnum(...)`
- Decl-Erfassung bei Enum-Registrierung
- Whole-program-Auswertung analog zu `dead-prefab`
- Ausgabe als Compiler-Warning mit Build-Kontext `in this build`

Der zusaetzliche Serialize/Deserialize-Check ist ebenfalls sinnvoll, sollte aber als eigenes zweites Feature geplant werden, weil er ueber einfache Symbolsammlung hinausgeht und mindestens eine kleine Datenfluss- bzw. Musteranalyse braucht.
