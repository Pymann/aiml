# Code-Statistiken: Funktionen und Klassen

Hier ist eine Einschätzung zur typischen Größe von Funktionen und Klassen in beliebigen Programmiersprachen. Diese Werte basieren auf gängigen Best Practices (Clean Code) sowie empirischen Untersuchungen großer Codebasen.

## 1. Funktionen / Methoden

Die Größe einer Funktion ist ein wichtiger Indikator für ihre Wartbarkeit und Verständlichkeit.

*   **Ideal:** 5 - 20 Zeilen (LOC) / **ca. 200 - 1000 Bytes**.
    *   Eine Funktion sollte genau **eine** Aufgabe erledigen (Single Responsibility Principle).
    *   Wenn eine Funktion nicht mehr auf einen Bildschirm passt, ist sie oft zu lang.
*   **Durchschnitt / Realität:** 20 - 50 Zeilen / **ca. 1000 - 2500 Bytes**.
    *   In vielen Legacy-Systemen oder komplexen Logik-Blöcken sind Funktionen oft etwas länger.
*   **Warnsignal (Code Smell):** > 50-100 Zeilen / **> 2.5 - 5 KB**.
    *   Solche Funktionen sind schwer zu testen und zu verstehen. Sie sollten meistens refactored werden.

**Faustregel:** Je komplexer die Logik (viele Verschachtelungen, `if`/`else`), desto kürzer sollte die Funktion sein.

## 2. Klassen

Die Größe von Klassen variiert stärker je nach Sprache (z.B. Java vs. Python vs. C++), aber es gibt grobe Richtwerte.

*   **Ideal:** < 200 Zeilen / **< 10 KB**.
    *   Eine Klasse sollte ein klares Konzept abbilden.
*   **Durchschnitt / Realität:** 200 - 500 Zeilen / **ca. 10 - 25 KB**.
    *   Dies ist oft akzeptabel, solange die Klasse kohärent ist (High Cohesion).
*   **Warnsignal (God Class):** > 1000 Zeilen / **> 50 KB**.
    *   Klassen, die zu viel wissen oder zu viel tun ("God Classes"). Sie verletzen oft das Single Responsibility Principle und sind schwer zu warten.

### Weitere Metriken für Klassen:
*   **Anzahl Methoden:** 
    *   Ideal: 10 - 20 Methoden (inklusive kleiner Helper/Getter/Setter).
    *   Zu viele öffentliche Methoden deuten auf ein unklares Interface hin.
*   **Anzahl Felder/Attribute:**
    *   Ideal: Wenige (z.B. < 7). Viele Felder deuten darauf hin, dass die Klasse eigentlich aus mehreren kleineren Klassen bestehen sollte.

## 3. Unterschiede nach Sprache

*   **Java / C#:** Neigen zu etwas mehr Boilerplate, daher sind Klassen oft etwas länger (Getter/Setter, Verbose Syntax).
*   **Python / Ruby:** Durch prägnantere Syntax oft kürzer für die gleiche Logik.
*   **C++:** Kann durch Header/Source-Trennung schwerer direkt vergleichbar sein, aber Implementierungsdateien folgen ähnlichen Regeln wie Java.

## Zusammenfassung

| Element | Ideal (Clean Code) | Akzeptabel | Refactoring empfohlen |
| :--- | :--- | :--- | :--- |
| **Funktion** | 5 - 15 Zeilen (~200B - 800B) | bis 50 Zeilen (bis 2.5KB) | > 80 - 100 Zeilen (> 4-5KB) |
| **Klasse** | < 200 Zeilen (< 10KB) | bis 500 Zeilen (bis 25KB) | > 800 - 1000 Zeilen (> 40-50KB) |
