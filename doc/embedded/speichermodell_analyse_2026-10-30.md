# Embedded-Eignung des AIML-Speichermodells

Stand: 2026-10-30. Quellcodeanalyse des aktuellen lokalen Arbeitsstands, einschließlich vorhandener uncommitteter Änderungen. Keine Änderungen an Compiler oder Runtime; keine ARM-/Cortex-M0-Laufzeit-, Speicher- oder Echtzeitmessungen. Die Aussagen beschreiben implementierte Pfade und deren Bewertung, keine nachgewiesene Board-Unterstützung.

## Ergebnis

Das ursprüngliche Embedded-Ziel ist für den elementaren Sprachkern weiterhin erreichbar: primitive Zahlen-/Bool-Typen, reine Wertmodelle und feste Arrays sind grundsätzlich geeignet. Automatische Freigaben schließen Embedded nicht aus. Entscheidend sind versteckte Allokationen, begrenzbare Speicher- und Laufzeitkosten sowie die benötigte Runtime.

Die aktuellen ENV-Schalter ergeben allerdings kein abgesichertes Profil ohne Heap. Für kleine Controller und strenge Echtzeitanforderungen fehlen eine durchgängige Bare-Metal-Portierung und eine Compilerprüfung, die dynamische Allokationen auch transitiv ausschließt.

LLVM unterstützt die relevanten ARM-Ziele. Codegenerierung allein stellt jedoch weder Startup-Code noch Zielbibliotheken, Linker-Konfiguration oder eine geeignete AIML-Runtime bereit. Cortex-M0 gehört zur Armv6-M-Familie; ARMv7 bezeichnet andere Architekturvarianten und sollte nicht als gleichbedeutendes Ziel behandelt werden.

## 1. Die beiden Umgebungsvariablen

Der korrekte zweite Name lautet `AIML_MEM_V3`. Für `AIML_MEMV3` wurde keine Auswertung gefunden.

Beide Schalter sind im untersuchten Compiler effektiv standardmäßig **an**:

- `AIML_MEM_V3`: `CompilerContext::memV3 = true`; Auswertung im Konstruktor.
- `AIML_LIFETIME_DROPS`: Das Kontextfeld ist zunächst `false`, aber `Compiler::compile()` setzt es vor der Codegenerierung auf `true` und berücksichtigt anschließend die ENV-Variable.

Auswertewerte für **aus**: leerer String, `0`, `off`, `OFF`, `false`, `FALSE`. Andere gesetzte Werte aktivieren den jeweiligen Schalter. Nicht gesetzt bedeutet beim normalen Kompilieren daher bei beiden **an**.

Die Variablen beeinflussen den erzeugten Code beim Kompilieren. Eine spätere Änderung beim Start eines bereits erzeugten Programms stellt dessen Speichermodell nicht um. Zusammengehörige Kompilationseinheiten müssen konsistente Einstellungen verwenden.

**Dokumentationswiderspruch:** `doc/env_variables_overview.md` nennt für Lifetime-Drops noch „Default OFF“. Auch mehrere Kommentare enthalten ältere Aussagen. Für diese Analyse sind die ausführbaren Initialisierungen maßgeblich.

Quellen: [context.h](../../src/core/context.h), [Compiler::compile](../../src/core/ast_compiler.cpp), [ENV-Übersicht](../env_variables_overview.md).

### AIML_MEM_V3=1

Aktiviert die neueren Darstellungs- und Ownership-Pfade:

- `list`, `map`, `buffer` und `cascade` werden in den V3-Verwaltungspfaden über referenzgezählte Handles dargestellt. Zuweisungen können dieselbe veränderliche Ressource teilen.
- Strings werden in entsprechenden Zuweisungspfaden per Referenzzählung geteilt.
- `ptr<T>` steht für eindeutigen Besitz, `shared<T>` für gemeinsamen referenzgezählten Besitz und `ref<T>` für nichtbesitzenden Zugriff.
- Zusätzliche statische Prüfungen diagnostizieren erkennbare Ownership-Verstöße, beispielsweise Benutzung nach Besitzübergabe oder Freigabe. Dies ist keine vollständige Speichersicherheitsgarantie.
- Die YAML-Anweisung `free: [p]` wird als ausführbarer AST-Knoten erzeugt. Bei einer passenden `ptr<T>`-Box werden besessene Felder bei der Freigabe berücksichtigt.

Referenzzählung ist kein nachverfolgender Garbage Collector. Besitzende Zyklen werden dadurch nicht automatisch eingesammelt.

### AIML_MEM_V3=0

Wählt ältere Darstellungs- und Verwaltungspfade. Die V3-Containerhandles und V3-Besitzprüfungen entfallen. Weitere Einstellungen, insbesondere `AIML_MODEL_REFCOUNT`, können das Verhalten zusätzlich beeinflussen.

**Dies entfernt dynamische Allokationen nicht:** Auch ältere Containerpfade verwenden `malloc`/`realloc` für Inhalte und Wachstum.

Besonders relevant: Die YAML-Anweisung `free: [p]` wird im Parser bei ausgeschaltetem V3 verworfen und bewirkt nichts. Ein FFI-Aufruf einer ausdrücklich deklarierten C-Funktion `free` ist davon zu unterscheiden.

Quelle: [statement_builder.cpp](../../src/core/ast/statement_builder.cpp), [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp), [list_impl.cpp](../../src/core/list_impl.cpp).

### AIML_LIFETIME_DROPS=1

Aktiviert wesentliche automatische Freigabe- und zugehörige Ownership-Pfade:

- Aufräumen lokaler besessener Ressourcen beim Scope-Ende, normalerweise in umgekehrter Deklarationsreihenfolge.
- Referenzzähler vermindern; beim letzten Besitzer Ressource freigeben.
- Aufräumen auf Rückgabepfaden unter Berücksichtigung des zurückgegebenen Besitzes.
- Verfolgung und Freigabe temporärer Ergebnisse.
- Zugehörige Besitzübergaben an Aufrufgrenzen, etwa bei `consumed` und unter V3 bei `ptr<T>`.
- Verschiedene Kopier-, Überschreib- und Parameter-Verwaltungspfade.

`lifetime: persistent` unterdrückt gewöhnliche lokale Scope-Drops. Modulglobale Werte erhalten keinen normalen Funktions-Scope-Drop. Nichtbesitzende Referenzen werden nicht als Eigentümer freigegeben. Nicht jeder Ressourcentyp oder Exit-Pfad ist durch diese Aufzählung als vollständig geprüft anzusehen.

### AIML_LIFETIME_DROPS=0

Unterdrückt wesentliche automatische Freigaben, ohne die zugehörigen Allokationen zu entfernen. Lokale Heap-Ressourcen und temporäre Ergebnisse können dadurch liegen bleiben. Stack-Speicher wird weiterhin nach den normalen Funktions-/Stackregeln abgebaut.

Der Schalter betrifft mehr als Blockende-Freigaben: Auch bestimmte Kopier-, Überschreib- und Besitzübergabepfade sind daran gekoppelt. Abschalten kann deshalb das Verhalten ändern und ist kein allgemeiner Wechsel zu sauberer manueller Speicherverwaltung.

Bei weiterhin aktivem V3 bleiben dessen Darstellung, Besitzdiagnosen und explizites `free:` grundsätzlich aktiv. Auch einzelne operationsbezogene Freigaben bleiben bestehen.

| MEM_V3 | LIFETIME_DROPS | Einordnung |
|---|---|---|
| 1 | 1 | Aktueller Standard: V3 mit automatischen Freigaben |
| 1 | 0 | V3 mit wesentlichen Lücken beim automatischen Aufräumen |
| 0 | 1 | Ältere Speicherpfade mit automatischen Drops; YAML-`free:` wirkungslos |
| 0 | 0 | Ältere Speicherpfade ohne automatische Drops; kein Profil ohne Heap |

Quellen: [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp), [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp), [ast_compiler.cpp](../../src/core/ast_compiler.cpp).

## 2. Elementare Datentypen

| Typgruppe | Darstellung und Verhalten | Embedded-Bewertung |
|---|---|---|
| Integer und `bool` | Direkte LLVM-Werte, keine eigene Heap-Allokation oder Referenzzählung | Grundsätzlich geeignet |
| `f32`, `f64` | Direkte LLVM-Gleitkommawerte, keine eigene Heap-Allokation | Geeignet, aber Rechenkosten und benötigte Hilfsfunktionen hängen vom Ziel ab |
| Reines Wertmodell | Felder inline als Struktur; lokale Deklaration legt Speicher an und initialisiert ihn mit null | Geeignet, wenn sämtliche Felder rekursiv ohne dynamische Ressourcen auskommen |
| `array<T:N>` | Feste Größe, zusammenhängende Elemente; lokal Stack-Speicher | Geeignet bei passendem Elementtyp und begrenztem Stack-Bedarf |
| `ptr<T>` | Eindeutiger Besitzer; Deklaration einer Zeigervariablen ist nicht selbst die Zielobjekt-Allokation; `{new: T}` verwendet Heap | Kontrollierbar, aber kein beliebiger nichtbesitzender Adresstyp |
| `dyn_array<T>` | Laufzeitgröße, Heap-Allokation | Nicht mit einem festen Array gleichzusetzen |

### Primitive

Primitive Zahlen- und Bool-Werte aktivieren keine dynamische Speicherverwaltung allein durch ihre Verwendung. Auch bei eingeschaltetem V3 und Lifetime-Drops entsteht daraus keine Referenzzählung.

`string` gehört in dieser Bewertung ausdrücklich nicht zur Gruppe der einfachen Zahlen-/Bool-Werte: Dynamisch erzeugte Strings verwenden eigene Runtime- und Heap-Pfade.

### Modelle

Ein einfaches Wertmodell wird durch V3 **nicht pauschal zum referenzgezählten Heap-Objekt**. Enthält es nur primitive Werte, feste Arrays geeigneter Elemente und weitere reine Wertmodelle, bleibt seine Darstellung struktartig.

Die Bedingung gilt rekursiv: Ein enthaltenes Modell mit dynamischen Feldern bringt deren Verhalten mit. Containerfelder können bereits bei der Initialisierung Allokationen auslösen. Initialisierer und aufgerufene Funktionen können unabhängig vom sichtbaren Ergebnistyp dynamisch allokieren.

Ein besitzendes `ptr<T>`-Feld benötigt selbst nur einen Zeigerplatz, kann aber Verantwortung für eine separat allokierte Ressource tragen. Es ist daher von einem vollständig ressourcenfreien Wertmodell zu unterscheiden.

### Feste Arrays

`array<T:N>` hat eine zur Compile-Zeit bekannte Kapazität und benötigt für seinen eigenen Speicher keine Heap-Allokation. Der untersuchte Schreibzugriff berechnet eine Elementadresse und speichert den Wert direkt.

Grenzen:

- Im untersuchten Zugriffspfad wird keine automatische Bereichsprüfung erzeugt. Ungültige Indizes können Speicher beschädigen.
- Ein festes Array macht die Ressourcen seiner Elemente nicht automatisch statisch. Arrays aus rein numerischen Werten oder reinen Wertmodellen sind die klare Ausgangsbasis.
- Große lokale Arrays können den Stack überlasten.
- Arrays mit besitzenden Zeigern benötigen eine gesonderte Prüfung ihrer Element-Ownership; die feste Kapazität garantiert keine vollständige automatische Elementverwaltung.

Quellen: [Typauflösung](../../src/core/context.h), [Deklaration und Array-Schreibzugriff](../../src/core/ast_compiler_stmt.cpp), [Array-Spezifikation](../../spec/52_arrays.md).

### ptr und nichtbesitzende Adressen

Unter V3 bedeutet `ptr<T>` eindeutigen Besitz, einschließlich Besitzübergabe und möglicher automatischer Freigabe. `{new: T}` allokiert ein Objekt über `calloc`; bei passenden lokalen Besitzern wird dessen Freigabe registriert.

Eine Adresse auf Stack-Speicher, statischen Speicher oder ein Hardware-Register darf nicht versehentlich als besitzende Heap-Box behandelt und freigegeben werden. Für nichtbesitzende Zugriffe sind semantisch `ref<T>` beziehungsweise ausdrücklich dafür vorgesehene rohe Adresszugriffe passend. Deren konkrete Eignung für MMIO ist gesondert zu prüfen: Zeigerzugriff allein garantiert insbesondere noch keine korrekte volatile-/Registerzugriffssemantik.

Quellen: [visitNewExpr](../../src/core/ast_compiler_expr.cpp), [Scope-Drop-Registrierung](../../src/core/ast_compiler_stmt.cpp).

## 3. Warum automatische Freigabe Embedded nicht ausschließt

Automatische Freigabe am Scope-Ende ist mit Embedded grundsätzlich vereinbar, ähnlich C++-Destruktoren. Ein bekannter Freigabezeitpunkt garantiert allerdings keine konstante Ausführungsdauer: Die letzte Referenz auf einen großen Objektgraphen kann viele weitere Freigaben auslösen.

Die wesentlichen Prüfgrößen sind:

- Maximale RAM- und Stack-Belegung.
- Begrenzbare Laufzeit von Operationen und Freigaben.
- Implizite Allokationen, Wachstum und Fragmentierung.
- Definiertes Verhalten bei Speichererschöpfung oder Kapazitätsüberschreitung.
- Plattformabhängigkeiten der verwendeten Runtime.

Statische Speicherbereiche und feste Kapazitäten sind für strenge Grenzen besonders geeignet. Dynamische Speicherverwaltung ist nicht für jedes Embedded-System ausgeschlossen; FreeRTOS unterstützt beispielsweise ausdrücklich statische und dynamische Allokation.

Externe Einordnung: [FreeRTOS: Static vs Dynamic Memory Allocation](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/09-Memory-management/03-Static-vs-Dynamic-memory-allocation), [Embedded Rust Book: Collections](https://docs.rust-embedded.org/book/collections/index.html).

## 4. Konkrete Grenzen des aktuellen Gesamtstands

Diese Befunde betreffen die Gesamtportierung, nicht die grundsätzliche Darstellung reiner Werttypen:

1. **Versteckter Heap bei V3-Containern:** Schon `aiml_list_handle_new()` ruft `calloc` für eine leere Liste auf. Ausschalten der Drops verhindert diese Allokation nicht.
2. **Hosted-Abhängigkeiten der String-Runtime:** Der Nicht-WASM-Pfad von `header_readable()` verwendet `sysconf` und `mincore`. Für Bare Metal ist eine andere Implementierung oder ein passend ausgeschlossener Runtime-Teil erforderlich.
3. **Direkter Objekt-Ausgabepfad:** `--target` und `setTargetTriple()` sind vorhanden. `compileToObject()` setzt das Ziel jedoch erneut auf das Host-Triple. Andere Ausgabepfade sind davon zu unterscheiden; LLVM-Zielunterstützung allein behebt diese AIML-Lücke nicht.
4. **Kein nachgewiesenes geschlossenes Profil ohne Heap:** Die beiden ENV-Schalter garantieren nicht, dass Funktionen einschließlich ihrer Aufrufketten und Feldinitialisierungen ohne dynamische Allokationen auskommen.
5. **Kein Cortex-M0-Nachweis in dieser Analyse:** Weder erfolgreicher Board-Boot noch ABI-, MMIO-, Interrupt-, Stack- oder Laufzeitverhalten wurden hier verifiziert. Die weitreichenden Aussagen in `spec/41_os_support.md` sind deshalb nicht als Implementierungsnachweis übernommen worden.

Quellen: [string_runtime.cpp](../../src/sys/string_runtime.cpp), [compiler.cpp](../../compiler.cpp), [OS-/Bare-Metal-Spezifikation](../../spec/41_os_support.md). Hintergrund: [Clang Cross-Compilation](https://clang.llvm.org/docs/CrossCompilation.html), [Armv6-M/Cortex-M0-Ziel](https://doc.rust-lang.org/rustc/platform-support/thumbv6m-none-eabi.html).

## 5. Folgerung für das Zielbild

Für den eingegrenzten Kern aus primitiven Werten, reinen Wertmodellen und festen Arrays ist das Embedded-Zielbild erhalten. Die automatische Ownership-Verwaltung muss dafür nicht grundsätzlich entfernt und V3 nicht durch die älteren Speicherpfade ersetzt werden.

Für ein belastbares kleines Embedded-Profil wären als nächste Entwicklungsschritte sinnvoll:

1. Ein Compilerprofil, das dynamische Allokationen auch transitiv durch Aufrufe, Initialisierer und Felder als Fehler erkennt.
2. Container und Strings mit fester Kapazität und definiertem Verhalten bei Überlauf, falls mehr als Arrays benötigt wird.
3. Bei erlaubtem dynamischem Speicher ausdrücklich wählbare Pools/Arenen mit dokumentierten Grenzen.
4. Ein durchgängiger Cross-Compile-Pfad und eine minimale Bare-Metal-Runtime.
5. Ein reproduzierbarer Nachweis auf einem konkreten Ziel mit RAM-/Flash-, Stack- und Laufzeitmessungen.

Dies sind Vorschläge, keine bereits vorhandenen Garantien oder implementierten Modi. Für ein strikt heapfreies Programm müssen außerdem die aufgerufenen Operationen geprüft werden; eine reine Liste seiner Variablentypen reicht als Beweis nicht aus.
