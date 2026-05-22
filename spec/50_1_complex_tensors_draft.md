# DRAFT — Complex Tensors for AIML

Status: nicht final  
Reifegrad: Entwurf / Diskussionsbasis  
Diese Spec ist absichtlich **noch nicht fertig**. Sie beschreibt eine
moegliche Richtung fuer komplexe Tensoren in AIML, aber keine bereits
abgeschlossene oder voll implementierte Sprachfunktion.

## 1. Ziel

AIML soll komplexe Zahlen **zuerst** als numerisches Spezialwerkzeug im
Tensor-System unterstuetzen, nicht sofort als allgemeinen neuen
Primitive-Typ fuer die gesamte Sprache.

Die Leitidee ist:

- kein sofortiger First-Class-Skalartyp `complex`
- stattdessen ein kontrollierter Ausbau des bestehenden Tensor-Runtimes
- Fokus auf wissenschaftliche Modelle, Signalverarbeitung, FFT,
  Phasenmodelle, Rotationen und experimentelle ML-Architekturen

## 2. Warum kein sofortiger `complex`-Primitive-Typ

Ein allgemeiner Sprachtyp `complex64` oder `complex128` wuerde sofort viele
Folgesysteme beruehren:

- Literalsyntax
- Cast-Regeln
- Operator-Promotion
- FFI / ABI
- Persistenz
- Debug-Formatting
- Collections und Maps
- statische Typregeln

Das ist fuer AIML aktuell zu breit.

Die meisten realistischen Use-Cases liegen ohnehin im Tensor- und
Numerikbereich. Deshalb ist ein Tensor-zentrierter Einstieg deutlich
realistischer.

## 3. Grundidee

Die bestehende `tensor`-Runtime speichert aktuell `float32`-Daten.
Komplexe Tensoren koennen als Erweiterung dieses Systems modelliert werden.

Moegliche Zieltypen:

- `tensor` bleibt vorerst reell
- `ctensor` als eigener komplexer Tensortyp

oder spaeter:

- `tensor` mit `dtype`
- etwa `f32`, `f64`, `complex64`, `complex128`

Fuer einen ersten AIML-Schritt ist `ctensor` wahrscheinlich einfacher als
ein kompletter allgemeiner `dtype`-Umbau.

## 4. Vorlaeufige Empfehlung

### Phase 1

Ein neuer komplexer Tensortyp:

```yaml
- decl: [z, ctensor]
```

oder als Rueckgabewert von Fabrikfunktionen:

```yaml
- decl: [z, ctensor, { call: [tensor.complex_from_parts, re, im] }]
```

### Phase 2

Spaeter optionaler allgemeiner `dtype`-Ansatz:

```yaml
- decl: [z, tensor, { call: [tensor.zeros_dtype, "complex64", 8, 8] }]
```

Dieser zweite Schritt ist **nicht** Teil dieser ersten Draft-Spec.

## 5. Geplante Kernoperationen

Die folgenden Operationen waeren fuer eine erste komplexe Tensor-API der
wichtigste Umfang.

### 5.1 Konstruktion

```yaml
- decl: [z, ctensor, { call: [tensor.complex_from_parts, re, im] }]
- decl: [re, tensor, { call: [tensor.real, z] }]
- decl: [im, tensor, { call: [tensor.imag, z] }]
```

Semantik:

- `re` und `im` muessen gleiche Shape haben
- Ergebnis ist ein komplexer Tensor derselben Shape

### 5.2 Elementweise Operationen

```yaml
- decl: [zc, ctensor, { call: [tensor.conj, z] }]
- decl: [za, tensor,  { call: [tensor.abs_complex, z] }]
- decl: [zp, tensor,  { call: [tensor.phase, z] }]
- decl: [out, ctensor, { call: [tensor.cmul, a, b] }]
- decl: [out, ctensor, { call: [tensor.cadd, a, b] }]
- decl: [out, ctensor, { call: [tensor.csub, a, b] }]
```

Hinweis:

- `tensor.abs_complex(z)` sollte den Betrag liefern, also einen **reellen**
  Tensor
- `tensor.phase(z)` sollte die Winkel in Radiant liefern, ebenfalls als
  reeller Tensor

### 5.3 Skalar-Mischformen

```yaml
- decl: [out, ctensor, { call: [tensor.cmul_scalar, z, 0.5] }]
- decl: [out, ctensor, { call: [tensor.cadd_real, z, bias] }]
```

Noch offen:

- ob diese Mischformen wirklich noetig sind
- oder ob man sie am Anfang bewusst klein haelt

### 5.4 Mathematische Reduktionen

```yaml
- decl: [s, ctensor, { call: [tensor.csum, z, -1] }]
- decl: [m, ctensor, { call: [tensor.cmean, z, -1] }]
```

Wichtig:

Das sind echte komplexe Mittelwerte, also komponentenweise Mittelung von
Real- und Imaginaerteil. Kein Winkelmittel.

### 5.5 Komplexes Skalarprodukt

```yaml
- decl: [score, ctensor, { call: [tensor.cdot, a, b] }]
```

Semantik:

- `cdot(a, b) = sum(conj(a_i) * b_i)`

Optional zusaetzliche Helpers:

```yaml
- decl: [score_re, tensor, { call: [tensor.cdot_real, a, b] }]
- decl: [score_im, tensor, { call: [tensor.cdot_imag, a, b] }]
```

## 6. Erweiterte Operationen

Diese Operationen wirken sinnvoll, sollten aber vermutlich **nicht** Teil
des minimalen ersten Schritts sein:

- `tensor.cmatmul`
- `tensor.fft`
- `tensor.ifft`
- `tensor.eig`
- `tensor.svd_complex`
- komplexes `layer_norm`
- komplexe Aktivierungen

Die Gefahr waere, die Runtime zu frueh zu gross zu machen.

## 7. Autograd

Status: **noch offen, nicht final**

Das ist einer der groessten offenen Punkte.

Fuer komplexe Tensoren gibt es mindestens drei denkbare Wege:

1. keine Gradienten im ersten Schritt
2. Gradienten ueber getrennte Real-/Imaginaer-Kanaele
3. explizite Wirtinger-orientierte Semantik

Empfehlung fuer AIML v1:

- komplexe Tensoren zuerst ohne vollstaendige Autograd-Garantie einfuehren
- oder nur fuer einen kleinen sicheren Ops-Kern:
  - `cadd`
  - `csub`
  - `cmul`
  - `conj`
  - `real`
  - `imag`
  - `csum`
  - `cmean`

Die finale Gradienten-Semantik ist **noch nicht beschlossen**.

## 8. Persistenz

Status: **noch offen, nicht final**

Ein moegliches Blob-Format waere:

- `ndim`
- Shape
- `dtype = complex64`
- rohe Daten als interleavtes
  - `re0, im0, re1, im1, ...`

Offene Fragen:

- eigener `ctensor.save` / `ctensor.load`-Pfad?
- oder allgemeine `tensor.save`-Erweiterung mit `dtype`?
- wie Hashing fuer komplexe Tensoren aussehen soll

## 9. Print / Debug

Status: **noch offen, nicht final**

Noetig waeren klare Regeln fuer:

- `tensor.print`-Darstellung
- Rundung
- Anzeige als
  - `(re, im)`
  - oder `a + bi`

Empfehlung:

- intern interleavt speichern
- fuer User-Ausgabe lesbare `a + bi`-Darstellung

## 10. Minimaler erster Scope

Wenn AIML komplexe Zahlen aufnimmt, sollte der erste Scope klein bleiben:

- `ctensor`
- `tensor.complex_from_parts`
- `tensor.real`
- `tensor.imag`
- `tensor.conj`
- `tensor.cadd`
- `tensor.csub`
- `tensor.cmul`
- `tensor.csum`
- `tensor.cmean`
- `tensor.cdot`

Das waere schon genug fuer:

- komplexe Resonanzmodelle
- holographisches Binding
- Phasenrotationen
- wissenschaftliche Visualisierung
- viele Signalverarbeitungs-Experimente

## 11. Was bewusst noch nicht Teil dieser Spec ist

Folgende Dinge sind bewusst ausgeklammert:

- allgemeiner `complex64`-Sprach-Primitive-Typ
- komplexe Literale wie `3 + 4i`
- Operator-Ueberladung fuer `+`, `*`, `/` auf Skalaren
- Maps oder Lists komplexer Skalare als Basiskonzept
- volle komplexe Autograd-Abdeckung
- komplette FFT-/DSP-Standardbibliothek

## 12. Empfehlung

Wenn AIML komplexe Zahlen einfuehrt, dann am besten:

1. zuerst als **Draft-Spec fuer komplexe Tensoren**
2. dann als kleine Runtime-Erweiterung ohne Big Bang
3. erst viel spaeter als allgemeiner Sprach-Primitive-Typ, falls wirklich
   noetig

## 13. Offene Fragen

- Soll der erste Typ `ctensor` heissen oder lieber direkt `tensor` mit
  zukuenftigem `dtype` vorbereiten?
- Soll komplexe Autograd-Unterstuetzung im ersten Schritt fehlen oder nur
  eingeschraenkt vorhanden sein?
- Soll `cdot` ein komplexes Ergebnis liefern oder standardmaessig ein
  reelles Resonanz-Readout anbieten?
- Wie stark soll die API wissenschaftlich sein und wie stark eher
  ML-pragmatisch?

## 14. Klarer Hinweis

Diese Spec ist **nicht final**.

Sie ist ein Arbeitsentwurf, um zu entscheiden:

- ob AIML komplexe Zahlen ueberhaupt aufnehmen soll
- und wenn ja, in welcher kleinsten vernuenftigen Form
