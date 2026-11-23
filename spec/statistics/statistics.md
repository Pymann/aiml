# Code statistics: functions and classes

Here is an estimate of the typical size of functions and classes in arbitrary programming languages. These values are based on common best practices (Clean Code) as well as empirical studies of large code bases.

## 1. Functions / methods

The size of a function is an important indicator of its maintainability and comprehensibility.

*   **Ideal:** 5 - 20 lines (LOC) / **approx. 200 - 1000 bytes**.
    *   A function should do exactly **one** job (Single Responsibility Principle).
    *   If a function no longer fits on one screen, it is often too long.
*   **Average / reality:** 20 - 50 lines / **approx. 1000 - 2500 bytes**.
    *   In many legacy systems or complex logic blocks, functions are often somewhat longer.
*   **Warning sign (code smell):** > 50-100 lines / **> 2.5 - 5 KB**.
    *   Such functions are hard to test and to understand. They should usually be refactored.

**Rule of thumb:** The more complex the logic (deep nesting, `if`/`else`), the shorter the function should be.

## 2. Classes

The size of classes varies more by language (e.g. Java vs. Python vs. C++), but there are rough guidelines.

*   **Ideal:** < 200 lines / **< 10 KB**.
    *   A class should represent one clear concept.
*   **Average / reality:** 200 - 500 lines / **approx. 10 - 25 KB**.
    *   This is often acceptable, as long as the class is coherent (high cohesion).
*   **Warning sign (God class):** > 1000 lines / **> 50 KB**.
    *   Classes that know too much or do too much ("God classes"). They often violate the Single Responsibility Principle and are hard to maintain.

### Further metrics for classes:
*   **Number of methods:** 
    *   Ideal: 10 - 20 methods (including small helpers/getters/setters).
    *   Too many public methods point to an unclear interface.
*   **Number of fields/attributes:**
    *   Ideal: few (e.g. < 7). Many fields suggest that the class should really consist of several smaller classes.

## 3. Differences by language

*   **Java / C#:** Tend towards somewhat more boilerplate, so classes are often somewhat longer (getters/setters, verbose syntax).
*   **Python / Ruby:** Often shorter for the same logic thanks to more concise syntax.
*   **C++:** Can be harder to compare directly because of the header/source split, but implementation files follow rules similar to Java.

## Summary

| Element | Ideal (Clean Code) | Acceptable | Refactoring recommended |
| :--- | :--- | :--- | :--- |
| **Function** | 5 - 15 lines (~200B - 800B) | up to 50 lines (up to 2.5KB) | > 80 - 100 lines (> 4-5KB) |
| **Class** | < 200 lines (< 10KB) | up to 500 lines (up to 25KB) | > 800 - 1000 lines (> 40-50KB) |
