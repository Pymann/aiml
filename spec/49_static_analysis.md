# Kapitel 49 — Static Analysis

> **Leitgedanke:** AIML enthaelt bewusst harte statische Strukturregeln, damit
> Code fuer Menschen und KI klein, lesbar und bearbeitbar bleibt.
> Diese Regeln wirken beim Kompilieren, nicht zur Laufzeit.

---

## 1. Ziel

Die Static-Analysis-Regeln in AIML sollen vor allem drei Dinge sichern:

- kleine, bearbeitbare Dateien
- kleine, bearbeitbare Funktionen
- lokale, explizite Ausnahmen statt globaler Abschaltungen

Der aktuelle Compiler-Stand enthaelt in diesem Kapitel genau die derzeit
implementierten Strukturregeln und den dazugehoerigen Escape-Hatch.

---

## 2. Aktuell implementierte Regeln

Der Compiler unterstuetzt derzeit diese statischen Strukturpruefungen:

| Regel | Schwelle | Ergebnis |
|------|----------|----------|
| Dateigroesse | `1000+` Zeilen | Warning |
| Dateigroesse | `1500+` Zeilen | Compiler-Error |
| Funktionsgroesse | `301+` Zeilen | Compiler-Error |

Es gibt derzeit keine weitere `static_off`-gesteuerte Strukturregel ausser
der Funktionsgroessenpruefung.

---

## 3. Dateigroesse

### 3.1 Warning ab 1000 Zeilen

Eine AIML-Datei mit `1000` oder mehr Zeilen erzeugt eine Compiler-Warnung.

Beispielmeldung:

```text
...: AIML file large (1000 lines). 1000+ lines should be split.
```

### 3.2 Hard Error ab 1500 Zeilen

Eine AIML-Datei mit `1500` oder mehr Zeilen erzeugt einen Compiler-Fehler.

Beispielmeldung:

```text
...: AIML file too large (1500 lines). Maximum is 1499 lines; 1500+ is a hard error.
```

### 3.3 Messmethode

Die Dateigroesse wird als einfache physische Zeilenanzahl der Quelldatei
gezaehlt.

Der Compiler liest dafuer die Datei direkt ein und zaehlt jede durch
`getline(...)` gelesene Zeile.

### 3.4 Geltungsbereich

Die Dateiregel gilt nicht nur fuer die Entry-Datei, sondern auch fuer:

- direkt importierte AIML-Dateien
- im Compile-Kontext geladene Sibling-Dateien

Damit werden auch grosse importierte Module sichtbar und nicht nur das
Top-Level-Entrypoint-File.

---

## 4. Funktionsgroesse

### 4.1 Hard Error ab 301 Zeilen

Eine Funktion oder Methode mit `301` oder mehr Zeilen erzeugt einen
Compiler-Fehler.

Beispielmeldung:

```text
...:12: AIML function 'render_board_screen' too large (301 lines). Maximum is 300 lines; 301+ is a hard error.
```

Es gibt aktuell keine Warning-Stufe fuer Funktionsgroessen.

### 4.2 Geltungsbereich

Die Funktionsregel gilt fuer:

- freie Funktionen (`def`)
- Methoden innerhalb von Klassen

### 4.3 Messmethode

Die Funktionsgroesse wird als Quellspanne berechnet:

- Start: `sourceLine` der Funktion
- Ende: letzte erkannte Quellzeile innerhalb des Body

Die Spanne ist also:

```text
endLine - sourceLine + 1
```

### 4.4 Verschachtelte Bloecke

Bei der Ermittlung des Funktionsendes werden derzeit rekursiv diese
verschachtelten Statement-Typen einbezogen:

- `if`
- `while`
- `try/catch`

Damit zaehlt nicht nur die letzte Top-Level-Anweisung im Body, sondern auch
die tiefste bekannte Quellzeile in verschachtelten Kontrollflussbloecken.

### 4.5 Nicht-Ziel der Messung

Die aktuelle Regel ist eine pragmatische AST-basierte Groessenpruefung.
Sie ist keine vollstaendige Source-Span-API mit exakter Endmarke fuer jeden
AST-Knoten.

Das bedeutet:

- die Regel ist bewusst praktisch statt perfekt
- sie dient als strukturierende Hartgrenze
- sie ist nicht als formatierungsgenaue Pretty-Print-Metrik gedacht

---

## 5. `static_off`

`static_off` ist der aktuelle Escape-Hatch fuer einzelne statische Regeln auf
Funktions- oder Methodenebene.

### 5.1 Syntax

`static_off` ist ein optionales Feld innerhalb von `def`.

```aiml
- def:
    name: render_huge_legacy_block
    return: void
    static_off: ["lines"]
    body:
      - call: [print, "legacy"]
```

Dasselbe gilt fuer Methoden:

```aiml
- class:
    name: BigScreen
    methods:
      - def:
          name: render
          return: void
          static_off: ["lines"]
          body:
            - call: [print, "ok"]
```

### 5.2 Typ

`static_off` ist eine `list<string>`.

Jeder String bezeichnet genau eine statische Compiler-Regel, die fuer diese
eine Funktion deaktiviert werden soll.

### 5.3 Aktuell definierte Option

Derzeit ist genau eine Option implementiert:

- `"lines"`

`"lines"` deaktiviert die Funktionsgroessenpruefung fuer die betreffende
Funktion oder Methode.

### 5.4 Semantik

- `static_off` wirkt nur lokal auf die einzelne `def`
- `static_off` wirkt nicht dateiweit
- `static_off` wirkt nicht projektweit
- `static_off` veraendert keine Runtime-Semantik
- `static_off` veraendert keinen IR- oder Codegen-Pfad
- `static_off` beeinflusst nur statische Compiler-Diagnostics

### 5.5 Unbekannte Eintraege

Unbekannte `static_off`-Eintraege sind aktuell erlaubt, haben aber keine
Wirkung.

Beispiel:

```aiml
static_off: ["unknown_rule"]
```

Das kompiliert weiterhin, deaktiviert aber derzeit keine bekannte Regel.

Damit bleibt der Mechanismus vorwaertskompatibel fuer spaetere statische
Checks.

---

## 6. Beispiele

### 6.1 Datei im Warning-Bereich

Eine Datei mit `1200` Zeilen:

- kompiliert weiter
- erzeugt aber eine Warning

### 6.2 Datei im Error-Bereich

Eine Datei mit `1500` Zeilen:

- kompiliert nicht mehr
- erzeugt einen Hard Error

### 6.3 Funktion ohne Ausnahme

Eine Funktion mit `301` Zeilen:

- kompiliert nicht mehr
- erzeugt einen Hard Error

### 6.4 Funktion mit `static_off: ["lines"]`

Eine Funktion mit `301+` Zeilen und

```aiml
static_off: ["lines"]
```

wird trotz ihrer Groesse nicht durch die Funktionslinienregel blockiert.

Die Dateigroessenregeln bleiben davon unberuehrt.

---

## 7. Prioritaet der Regeln

Die Regeln sind unabhaengig voneinander:

- `static_off: ["lines"]` deaktiviert nur die Funktionsgroessenpruefung
- die Dateigroessenregeln bleiben weiterhin aktiv

Beispiel:

- Datei hat `1600` Zeilen
- Funktion darin hat `static_off: ["lines"]`

Ergebnis:

- die Funktion wird von der Funktionsregel verschont
- die Datei scheitert trotzdem an der Dateigroessenregel

---

## 8. Empfehlungen

- grosse Dateien aufsplitten statt Ausnahmen zu setzen
- grosse Funktionen aufbrechen statt `static_off` routinemaessig zu verwenden
- `static_off: ["lines"]` nur fuer bewusst akzeptierte Legacy- oder
  Migrationsfaelle nutzen
- neue Funktionen sollten die 300-Zeilen-Grenze grundsaetzlich einhalten

---

## 9. Nicht-Ziele

Dieses Kapitel beschreibt keine allgemeinen Typ-, Parser- oder
Semantikfehler wie:

- unbekannte Typen
- falsche Operator-Arity
- undefinierte Variablen
- ungueltige Member-Zugriffe

Solche Compilerfehler existieren ebenfalls, gehoeren aber nicht zu den hier
gemeinten Struktur- und Static-Analysis-Regeln.

---

## 10. Implementierungsstand

Der aktuelle Compiler-Stand implementiert:

- Datei-Warning ab `1000` Zeilen
- Datei-Hard-Error ab `1500` Zeilen
- Funktions-Hard-Error ab `301` Zeilen
- `static_off` als `list<string>` auf `FunctionDecl`
- die konkrete `static_off`-Option `"lines"`

Weitere statische Regeln koennen spaeter ueber zusaetzliche
`static_off`-Keys oder weitere feste Compiler-Checks ergaenzt werden.
