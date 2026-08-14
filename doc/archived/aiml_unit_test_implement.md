# Auto-Generation von AIML Unit Tests (Mock & Stub Builder)

Dieses Dokument bewertet die Komplexität und den theoretischen Ansatz, um aus bestehenden `.aiml` Quellcode-Dateien automatisch voll funktionsfähige, isolierte Unit-Test-Dateien (mit Mocks und gekappten Imports) zu generieren.

## 1. Machbarkeit & Schwierigkeit
Die Schwierigkeit ist erstaunlich **niedrig**. 
Da AIML seinen eigenen Abstract Syntax Tree (AST) rein als YAML strukturiert, müssen wir keinen lexikalischen Analyzer oder Text-Parser (wie bei C/C++ oder Rust) von Grund auf neu entwickeln. 

Ein einfaches Python-Skript, das `pyyaml` nutzt, kann die Datei laden, als geschachteltes Dictionary modifizieren und als perfektes AIML wieder auf die Festplatte schreiben.

## 2. Der Algorithmus (Python-basierter Test-Generator)

Der Tooling-Ablauf würde in diesen 6 klaren Schritten erfolgen:

### Schritt 1: YAML AST Parsen & Isolieren
Das Skript lädt die Original-Datei (z. B. `core/decoder.aiml`) in eine Python-Liste.
- **Importe abschneiden**: Alle Top-Level-Listenpunkte vom Typ `{ "import": ... }` werden via `del list[i]` entfernt, um Datei-Abhängigkeiten komplett zu kappen.

### Schritt 2: "Vorhandene Symbole" inventarisieren
Das Skript durchläuft die Top-Level-Nodes, um zu verstehen, was _wirklich_ da ist:
- Alle gefundenen `- class: { name: "X" }` $\rightarrow$ Registriere "X"
- Alle gefundenen `- def: { name: "Y" }` $\rightarrow$ Registriere "Y"

### Schritt 3: "Fehlende Symbole" finden (Semantic Pass)
Nun läuft das Skript rekursiv durch alle `body`-Blöcke und Variablen-Deklarationen des ASTs:
- Sucht Aufrufe der Art: `[call, FunktionX, ...]` oder `[obj.FunktionX]`.
- Sucht Deklarationen der Art: `[decl, var, TypX]`.
- **Logik**: Wenn `FunktionX` oder `TypX` weder im Inventar von Schritt 2 ist, NOCH zur AIML Standardbibliothek gehört (`sys`, `math`, `list`, `string`, `i32` ...), handelt es sich um eine abgetrennte Abhängigkeit.

### Schritt 4: Mocks & Stubs automatisch einbauen
Für jedes _fehlende_ Symbol baut das Skript nun oben in der YAML-Liste dynamischen Mock-Code (Dummy-Nodes) ein:

- **Für fehlende Klassen**:
  ```yaml
  - class:
      name: MissingDependency  # Dummy, damit der LLVM Compiler den Pointer-Typ kennt
      members:
        - [dummy, i32] # Hilfsfeld, falls Struct-Size geprueft wird
  ```
- **Für fehlende Funktionen**:
  ```yaml
  - def:
      name: missing_function
      args: [] # Typen können meist weggelassen/als 'any' definiert werden
      return: void
      body:
        - print: ["Mocked missing_function executed"]
  ```

### Schritt 5: Test-Skeletons generieren
Für _jede_ in Schritt 2 inventarisierte `def` oder `method` hängt das Skript am Ende der YAML-Datei einen Testfall an:
```yaml
- def:
    name: test_Decoder_decode_step
    return: void
    body:
      - print: ["--- Running: test_Decoder_decode_step ---"]
      # TODO: Manuelles Assert einbauen
```

### Schritt 6: Den Runner (`main`) generieren
Als letztes Element wird die `main`-Routine eingefügt, die eine Test-Suite bildet, den Compiler zufriedenstellt (weil `main` verlangt wird) und alle `test_*` Funktionen feuert:
```yaml
- def:
    name: main
    return: i32
    body:
      - call: [test_Decoder_decode_step]
      - call: [test_xyz]
      - print: ["All generated tests executed successfully."]
      - return: [0]
```

## 3. Vorteile für unsere Phase 1 bis 3
1. **100% Compiler-Sicherheit ohne Context**: Da fehlende Typen durch leere Dummy-`class`-Nodes gestubbed werden, meckert der Typ-Checker von `tulaiml` nicht. Es kompiliert sofort.
2. **Blitzschnelles TDD**: Man kann Algorithmen (z. B. Matrix-Multiplikation) innerhalb einer großen Agenten-Klasse entwickeln, den Generator ausführen lassen, und das isolierte `test_tensor_minimal.aiml` in Millisekunden über `out/aiml` evaluieren.
3. **CI/CD Hook-in**: Python kann 50 .aiml Dateien über das Skript in Sekunden in autarke Test-Suites umwandeln lassen.

## Fazit: Lohnt sich das als Python-Skript?
**Definitiv.** Was in klassischem C++ einen enorm komplexen Clang-AST-Matcher benötigen würde, ist in AIML nur ein **50-100 zeiliges Python-Script**, das ein simples JSON/YAML-Objekt rekursiv durchsuchen und verändern muss. Das wäre ein gigantischer Produktivitäts-Boost für unser Projekt.

---

## 4. Die Königsklasse: Nativer Compiler-Support (`--generate-tests`)

Deine Überlegung ist absolut spot-on! Anstatt ein externes Python-Skript zu pflegen, ist es paradoxerweise sogar **noch sauberer und mächtiger**, diesen Generator direkt in den `aiml` C++ Compiler einzubauen.

### Warum der Compiler der perfekte Ort dafür ist:
1. **Perfektes Wissen über die Standardbibliothek:** Ein Python-Skript müsste eine hartcodierte "Whitelist" pflegen, um zu wissen, dass `[sys.env]` oder `[math.vec2]` nicht gemockt werden dürfen. Der C++ Compiler hat dieses Wissen im `SymbolResolutionPass` bereits fehlerfrei eingebaut!
2. **Abfangen undefinierter Referenzen:** Normalerweise bricht der Compiler mit `Error: Undefined Type X` ab, wenn er ein `[decl, my_var, MissingClass]` findet. Für den Test-Modus aktivieren wir stattdessen einen `TestGenVisitor`: Sobald der Compiler bemerkt, dass ein Typ fehlt, spuckt er keinen Fehlerbruch aus, sondern injiziert kurzerhand einen neuen Dummy-Knoten in den internen C++ AST (Abstract Syntax Tree).
3. **Nativer Export:** Dank der Implementierung von `yyjson` (aus Phase 4) hat der AIML-Compiler ohnehin einen extrem schnellen JSON-Writer an Bord. Da *valid JSON = valid YAML* ist, kann der C++ Compiler den um die Stubs und `- def: [test_...]` Skelette erweiterten internen AST am Ende einfach als formatierten Quellcode auf die Festplatte dumpen!

**Das C++ CLI-Interface sähe dann fantastisch einfach aus:**
```bash
./out/aiml --generate-tests src/tul/model/decoder.aiml -o tests/test_decoder_minimal.aiml
```

Die Entwicklungsaufwand in C++ für diesen `TestGeneratorPass` läge vermutlich bei realistischen **200–400 Zeilen**. Der Compiler iteriert einmal über alle Nodes, erzeugt bei Fehlen on-the-fly Dummys im System-Heap, hängt `test_`-Routinen an und schreibt den Tree in die File zurück. 

**Fazit:** Der Compiler-Ansatz ist technisch überlegen, weil er die *Single Source of Truth* (das eigene Typsystem) optimal ausnutzt, ohne Tooling-Brüche in Python zu provozieren!
