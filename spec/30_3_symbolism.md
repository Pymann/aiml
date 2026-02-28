# Symbol-Semantik beim Hot-Reload

## 1. Das Kernproblem: Drei Schichten einer Symbol-Bedeutung

Ein Symbol (Funktion, Klasse, Enum, Interface, Model) hat in AIML drei unabhängige
Schichten, die beim Hot-Reload verschieden behandelt werden müssen:

```
┌─────────────────────────────────────────────────────────┐
│ 1. Symbolschicht (Compile-Time)                         │
│    ctx.functions["f"] / ctx.classes["C"] / ctx.enums["E"]│
│    → immer sofort aktualisierbar                        │
├─────────────────────────────────────────────────────────┤
│ 2. Code-Schicht (Maschinencode)                         │
│    Offsets, Sprungadressen, Immediates — baked in       │
│    → nur via fptable oder Trampoline austauschbar       │
├─────────────────────────────────────────────────────────┤
│ 3. Datenschicht (Heap)                                  │
│    Live-Instanzen mit aktuellem Byte-Layout             │
│    → niemals retroaktiv patchbar ohne Migration         │
└─────────────────────────────────────────────────────────┘
```

**Die zentrale Frage**: "Kriegen die Symbole einfach eine neue Bedeutung?"

Antwort: **Schicht 1 ja, immer.** Schicht 2 und 3 je nach Symbol-Typ und Art
der Änderung — das ist der Kern dieser Spezifikation.

---

## 2. Funktionen — vollständig austauschbar

Funktionen haben **kein persistentes Datum** (keinen Heap-Zustand). Ihr Symbol
ist rein Code.

```
Änderung:                          Kompatibel?
─────────────────────────────────────────────
Implementierung (Body)             ✓ immer
Rückgabewert-Typ                   ✗ Breaking (Call-Sites erwarten anderen Typ)
Signatur (Argumente)               ✗ Breaking (Call-Sites werden ungültig)
Name                               ✗ Breaking (alle Aufrufer müssen angepasst werden)
```

**Neue Bedeutung** (Schicht 1 + 2): Symbol-Eintrag wird aktualisiert, Maschinencode
via fptable/Trampoline ersetzt. Alle zukünftigen Aufrufe rufen die neue Implementierung.
Laufende Aufrufe beenden sich sauber mit der alten (Backup, Abschnitt 38_2 §6).

**Versionierung**: `version`-Feld im `def`-Block steuert, ob Hot-Reload stattfindet:
```aiml
- def:
    name: worker_task
    version: 2          # version 1 war im Hauptprogramm
    body: ...
```
→ `bundle_version (2) > ctx.version (1)` → Reload. Gleiche oder ältere Version → SKIP.

---

## 3. Enums — baked-in Immediates vs. Lookup-Tabelle

### 3.1 Aktueller Status: Compile-Time-Konstanten

Enum-Werte werden zur Compile-Zeit als LLVM-Immediates eingebettet:

```aiml
- enum:
    name: Status
    values: [Ok, Error, Pending]
    # Ok=0, Error=1, Pending=2
```

```llvm
; Generierter IR-Vergleich:
%cmp = icmp eq i32 %s, 1   ; "1" ist baked in — Error=1 zur Compile-Zeit fixiert
```

**Konsequenz**: Enum-Werte können nicht hot-reloaded werden. Änderungen an
Enum-Werten sind **immer Breaking** — alter Code vergleicht mit falschen Immediates.

### 3.2 Sichere Änderungen

```
Änderung:                          Kompatibel?
─────────────────────────────────────────────
Neues Member am Ende hinzufügen    ✓ bestehender Code kennt es nicht, ignoriert es
Bestehende Werte verändern         ✗ Breaking (alle icmp-Immediate-Vergleiche falsch)
Member entfernen                   ✗ Breaking (Code referenziert ungültigen Wert)
Member umbenennen                  ✗ Breaking (Compile-Fehler in Abhängigkeiten)
Reihenfolge ändern                 ✗ Breaking (implizite Werte verschieben sich)
```

### 3.3 Zukünftig: Enum-Lookup-Tabelle

Analog zu `__aiml_fptable` könnte eine globale Enum-Wertetabelle emittiert werden:

```llvm
@__aiml_enum_Status = global [3 x i32] [i32 0, i32 1, i32 2]

; Vergleich über Tabelle:
%slot = getelementptr [3 x i32], [3 x i32]* @__aiml_enum_Status, i32 0, i32 1
%val  = load i32, i32* %slot
%cmp  = icmp eq i32 %s, %val
```

Dann wäre ein Hot-Reload der Werte (nicht der Reihenfolge!) möglich. Performance-Kosten:
+1 Load pro Vergleich. Für die initiale Implementierung zurückgestellt.

---

## 4. Klassen — das Code/Layout-Trennungsprinzip

Klassen haben zwei völlig unterschiedliche Aspekte beim Hot-Reload:

### 4.1 Methoden (Code)

Methoden sind Funktionen — vollständig austauschbar wie in Abschnitt 2 beschrieben.
Eine Klassen-Methode `Foo.doThing` ist IR-seitig `@Foo__doThing(i8* self, ...)`.
fptable oder Trampoline ersetzen den Code; `self`-Zeiger bleibt gültig.

### 4.2 Felder (Layout)

Feld-Offsets werden zur Compile-Zeit als GEP-Konstanten eingebettet:

```aiml
- class:
    name: Payload
    members:
      - [id,    i32]   # Offset 0
      - [value, i32]   # Offset 4
```

```llvm
; Zugriff auf p.value:
%p_value = getelementptr %Payload, %Payload* %p, i32 0, i32 1   ; Index 1 = Offset 4
%v = load i32, i32* %p_value
```

Wenn nun `version 2` ein neues Feld **vor** `value` einfügt:
```aiml
- class:
    name: Payload
    version: 2
    members:
      - [id,       i32]   # Offset 0
      - [priority, i32]   # Offset 4  ← neu (verschiebt value!)
      - [value,    i32]   # Offset 8  ← war 4, jetzt 8
```

Alter Code liest `Index 1` → liest `priority` statt `value` → **stille Datenverfälschung**.
Keine Fehlermeldung, kein Crash (zumindest auf x86-64) — nur falsche Ergebnisse.

### 4.3 Kompatibilitätsmatrix für Klassen

```
Änderung:                              Methoden  Layout   Kompatibel?
─────────────────────────────────────────────────────────────────────
Methoden-Implementierung ändern        neu       gleich   ✓ immer
Neues Feld am ENDE hinzufügen          gleich    erweiter ✓ alte Offsets bleiben
Neues Feld in der MITTE einfügen       gleich    bricht   ✗ Breaking
Feld entfernen                         gleich    bricht   ✗ Breaking
Feld-Typ ändern (gleiche Größe)        gleich    ggf. ok  △ gefährlich
Feld-Typ ändern (andere Größe)         gleich    bricht   ✗ Breaking
Klasse umbenennen                      —         —        ✗ Breaking
```

**Goldene Regel**: Neue Felder nur am Ende. Methoden beliebig.

---

## 5. Models — pure Datenschemata

Models (siehe `15_models.md`) sind Klassen ohne Methoden — nur Felder und Validierung.
Das Layout-Problem aus Abschnitt 4.2 gilt identisch.

**Zusätzliche Dimension**: Models werden serialisiert/deserialisiert. Ein hot-reloaded
Model mit geändertem Schema kann beim Deserialisieren alter JSON/YAML-Blobs scheitern.

```
Änderung:                          Kompatibel?   Serialisierung
───────────────────────────────────────────────────────────────
Neues optionales Feld (mit Default) ✓ Layout ok   ✓ altes JSON enthält es nicht → Default
Neues Pflichtfeld                   ✗ Layout bricht ✗ altes JSON fehlt das Feld
Feld entfernen                      ✗ Layout bricht △ altes JSON hat Extra-Feld → ignoriert
Feld-Typ ändern                     ✗ Layout bricht ✗ Parse-Fehler möglich
```

**Empfehlung**: Model-Versionierung explizit mit `version`-Feld nutzen und
Migrations-Funktionen bereitstellen (Abschnitt 6.3).

---

## 6. Live-Instanzen — das Migrations-Problem

### 6.1 Das Problem

Wenn Klasse/Model `Payload` hot-reloaded wird (Layout-Änderung), existieren im
Heap möglicherweise `Payload`-Instanzen, die mit dem alten Layout allokiert wurden:

```
Heap vor Hot-Reload:          Heap nach Hot-Reload:
  p1: [id=1][value=42]          p1: [id=1][value=42]   ← unverändert im Heap
  p2: [id=2][value=7]           p2: [id=2][value=7]    ← unverändert im Heap

Neues Layout erwartet: [id][priority][value]
Alter Code liest p1.value bei Offset 4 → 42 ✓
Neuer Code liest p1.value bei Offset 8 → liest p2.id (=2) ✗ ← Datenverfälschung
```

### 6.2 Strategie 1: Invalidierung (einfachste Lösung)

Alle Live-Instanzen nach einer Layout-Änderung als ungültig markieren.
Zugriff darauf → Runtime-Fehler (Exception oder Assertion).

Voraussetzung: GC oder explizites Tracking aller Heap-Allokationen nach Typ.
Für AIML aktuell nicht implementiert (kein GC).

### 6.3 Strategie 2: Migration (korrekteste Lösung)

Eine Migrations-Funktion wird beim Hot-Reload aufgerufen:

```aiml
# Migrations-Callback (optional, vom Nutzer definiert):
- def:
    name: migrate_Payload_v1_to_v2
    args: [[old, Payload], [new, Payload]]   # old: Layout v1, new: Layout v2
    body:
      - set: [new.id,       old.id]
      - set: [new.priority, 0]         # Default für neues Feld
      - set: [new.value,    old.value]
```

Das erfordert ein Instanz-Register (alle `Payload*`-Zeiger bekannt) und ist
für die initiale Implementierung zu komplex.

### 6.4 Strategie 3: Append-Only (pragmatischste Lösung, empfohlen)

Nur Erweiterungen am Ende erlauben. Alter Code liest alte Felder korrekt.
Neuer Code kann neue Felder lesen. Keine Migration nötig.

```
Payload v1: [id: 0..3][value: 4..7]          ← 8 Bytes
Payload v2: [id: 0..3][value: 4..7][prio: 8..11] ← 12 Bytes, v1-Felder unverändert
```

Neuer Code allokiert 12 Bytes. Alter Code greift nur auf Bytes 0..7 zu — korrekt.
Neue Felder in alten Instanzen sind undefiniert (0-initialisiert bei malloc).

**Einschränkung**: Alter Code, der `sizeof(Payload)` kennt, allokiert zu wenig.
In AIML: Allokation immer über `CodeImpl::new(ctx, typename)` → nutzt aktuelles
`sizeof` → kein Problem, solange alter Code keine Payload selbst allokiert.

---

## 7. Interfaces — strukturelle Verträge

AIML-Interfaces sind Compile-Time-Konstrukte (kein Vtable-Objekt im Heap).
Eine Klasse "implementiert" ein Interface durch strukturelle Übereinstimmung.

```aiml
- interface:
    name: Runnable
    methods: [[run, [], void]]

- class:
    name: Worker
    implements: [Runnable]
    methods:
      - def:
          name: run
          body: ...
```

**Was passiert beim Hot-Reload eines Interfaces?**

Das Interface selbst hat kein Heap-Objekt — es ist nur eine Typannotation. Ein
Hot-Reload bedeutet, das Interface-Constraint ändert sich in der Symboltabelle.

```
Änderung am Interface:                  Konsequenz
────────────────────────────────────────────────────────────────────
Neue Methode hinzufügen                 Alle implementierenden Klassen werden
                                        "unvollständig" → Compile-Fehler bei
                                        neuem Code, der Interface nutzt
Methode entfernen                       Implementierende Klassen haben Extra-Methode
                                        → harmlos (strukturell kompatibel)
Methoden-Signatur ändern               Alle Implementierungen werden inkompatibel
Interface umbenennen                    Alle Referenzen müssen aktualisiert werden
```

**Neue Bedeutung?**: Ja, für neu kompilierten Code sofort. Alter kompilierter Code,
der das Interface-Constraint bereits geprüft hat, ist nicht betroffen (Constraint
wurde zur Compile-Zeit validiert, kein Runtime-Check).

**Hot-Reload eines Interfaces** bedeutet praktisch immer, auch alle implementierenden
Klassen zu hot-reloaden. Diese werden im Bundle-Dependency-Graph erfasst
(sys.bundle schließt Abhängigkeiten transitiv ein).

---

## 8. Kompatibilitäts-Übersicht

| Symbol-Typ | Code-Änderung | Layout-Änderung | Wert-Änderung |
|------------|--------------|-----------------|---------------|
| Funktion | ✓ (Trampoline/fptable) | — | — |
| Enum | — | — | ✗ (Immediates baked) |
| Enum (zukünftig, Lookup-Tabelle) | — | — | ✓ (wenn kein Reorder) |
| Klasse — Methode | ✓ | — | — |
| Klasse — Felder | — | △ nur Append-Only | — |
| Model — Felder | — | △ nur Append-Only | — |
| Interface | △ (nur Entfernen) | — | — |

Legende: ✓ kompatibel, △ eingeschränkt, ✗ Breaking Change

---

## 9. Versionierungs-Strategie (Empfehlung)

```aiml
# Regel: version immer explizit setzen bei Hot-Reload-fähigen Definitionen.

- class:
    name: Payload
    version: 1
    members:
      - [id,    i32]
      - [value, i32]

# Kompatibles Update (Append-Only → version erhöhen):
- class:
    name: Payload
    version: 2
    members:
      - [id,       i32]
      - [value,    i32]
      - [priority, i32]   # NEU — am Ende

# Inkompatibles Update → neuen Namen wählen statt Hot-Reload:
- class:
    name: PayloadV3           # ← neuer Name, kein Breaking Hot-Reload
    version: 1
    members:
      - [priority, i32]       # Reihenfolge geändert
      - [id,       i32]
      - [value,    i32]
```

**Faustregel**:
- Code (Funktionen, Methoden): Hot-Reload immer möglich → `version` erhöhen
- Daten (Klassen, Models): Hot-Reload nur Append-Only → `version` erhöhen
- Enum-Werte: kein Hot-Reload → Lookup-Tabelle abwarten
- Inkompatible Schema-Änderungen: neuen Typnamen vergeben, Migration explizit schreiben
