# Offene AIML-Punkte aus der Arbeit an cai

Date: 2026-10-29
Status: backlog
Quelle: Projekt `Pymann/cai` (Kaskaden-KI in AIML). Ein Teil wurde von cai selbst gefunden
(`cai aiml lerne`: Versuchsprogramme gegen den echten Compiler), der Rest beim Bauen von cai.
Alle Punkte am 29.10.2026 auf `mem-v3-compiler` nachgeprüft; Probe jeweils angegeben.

Schon erledigt (nur zur Einordnung): hashmap<K:V> (spec/69), Normalisierung der Importpfade
(tests/import_norm), Spec `==`/`!=` -> `eq`/`neq` und `neg` -> `-` (d7766b0), Rückgabe geliehener
Container-Elemente (`return: { call: [x.get, i] }`, tests/test_hashmap_string_return.aiml).

## Hoch — still falsche Ergebnisse

### 1. `select` gibt es nicht, wird aber ohne Diagnose übersetzt und liefert 0

```yaml
- decl: [res, i64, { select: [false, 10, 20] }]   # res == 0, erwartet 20, kein Fehler
```
spec/04_1 §8 ist seit d7766b0 als „nicht umgesetzt“ markiert.  Entscheidung offen: streichen
(if/else reicht) oder umsetzen.  Mindestens: ein unbekannter Operator darf nicht still 0 liefern,
sondern muss ein Übersetzungsfehler sein — sonst erzeugt jeder Tippfehler dieselbe falsche Null.

### 2. `shl` / `shr` / `ushr` mit negativer Schiebeweite: undefinierter Wert

```yaml
- print: [{ shl: [6, -4] }]   # 0 als Einzelprogramm, -1595265824 in einem größeren Programm
```
C-Verhalten (UB) schlägt durch.  Vorschlag: Schiebeweite maskieren (`& 63`) oder bei < 0 / >= 64
einen Laufzeitfehler werfen, und das in spec/04_1 festhalten.

### 3. Listenliteral als Aufrufargument wird aufgefächert

```yaml
- decl: [got, i32, { call: [count_items, [1, 2, 3]] }]
# Module verification FAILED: Incorrect number of arguments passed to called function!
```
Auch `[]` als Argument (null Argumente).  Umgehung bisher: lokale Liste anlegen.  Die Meldung
kommt erst aus der LLVM-Verifikation; nötig ist ein echtes Listenargument oder eine klare
Fehlermeldung an der Aufrufstelle.

**OFFEN (nachgemessen 31.10.2026)** — der Fix zu Punkt 13 deckt das NICHT ab, er betraf nur `set`.
Die Ursache ist aber dieselbe Stelle: `OnSequenceStart` erkennt die innere Sequenz nur für `decl`
und `set`; bei `call` wandern die Elemente einzeln über `orderedCallArgs` in die Argumentliste und
werden dadurch aufgefächert.  Ein Fix gehört an dieselbe Stelle (innere Sequenz auch für `call`
erkennen → `BufferLiteralExpr` als EIN Argument) und braucht zusätzlich eine Entscheidung, in was
das Literal am Parameter materialisiert wird (temporäre Liste mit Besitzregel nach spec/61).

### 4. `Kv<string:i32>.get` auf einen fehlenden Schlüssel: SIGSEGV

```yaml
- decl: [store, Kv<string:i32>, { call: [db.open_kv, "probe_kv", { persist: false }] }]
- decl: [got, i32, { call: [store.get, "fehlt"] }]   # AIML CRASH (signal 11)
```
`Kv<string:string>` liefert in diesem Fall `""`.  Erwartet: 0 oder eine Ausnahme.

## Mittel — Spec und Compiler widersprechen sich

### 5. `and` / `or` mit mehr als zwei Operanden

spec/04_1 Z. 131/140: „Accepts 2 or more arguments: `{ and: [a, b, c] }`“.
Compiler: „Binary operator 'and' expects exactly 2 operands, got 3.“
Entweder umsetzen (links fold) oder die Spec korrigieren.

### 6. `io.read(io.stdin, n)` liest über das Zeilenende hinaus

spec/23 Z. 20: „Read 100 bytes or until EOF/Newline“.  Gemessen mit `printf 'ab\ncd\n'`:
`io.read(io.stdin, 100)` liefert `"ab\ncd\n"` (fread-Block).  cai liest deshalb zeichenweise.
Entweder zeilenweise lesen oder die Spec korrigieren (und ggf. ein `io.read_line` anbieten).

### 7. `neq` auf Vektoren fehlt

`{ eq: [a, b] }` auf vec2/3/4 geht, `{ neq: [a, b] }` meldet „Cannot apply eq/neq to struct type
without __eq__: vec3“.  spec/43_0 nennt vorerst `{ not: [{ eq: [a, b] }] }`.

### 8. `map<K:V>` hat kein `clear`

„Unknown map method: clear on map<string:i32>“.  `hashmap` hat `clear`.  Für `map` entweder
nachziehen oder in spec/10 als fehlend nennen.

## Niedrig — Ergonomie / Design

### 9. Eigentümerschaft nach Kurznamen (`ownsReturnMatches`)

Behoben ist nur der Rückgabefall für string-Elemente (`isBorrowingAccessor`, Typ des Empfängers).
Weitere Stellen entscheiden noch allein über den Kurznamen der aufgerufenen Funktion, ob ein
Ergebnis frisch oder geliehen ist (ast_compiler_stmt.cpp: decl von Modellen/shared ~Z. 1622 und
~Z. 1858; ast_compiler_call.cpp ~Z. 2244 beim push).  Eine Nutzerfunktion mit gleichem Kurznamen
(`get`, `at`, `first`, `last`) kann dort dieselbe Verwechslung auslösen.  Vorschlag: überall
`isBorrowingAccessor` bzw. den Empfängertyp verwenden.

### 10. Keine Maskierung für `${` in Zeichenketten

Ein Programm, das AIML-Quelltext erzeugt (cai), kann `"${x}"` nicht als Text schreiben; auch
verschachtelte Anführungszeichen in `${ { … "x" … } }` zerbrechen das YAML.  Vorschlag: `$${` als
wörtliches `${`.

### 11. Methodenaufruf auf einem Aufrufergebnis

`{ call: [{ call: [f] }.len] }` ist kein gültiges YAML/AIML; es braucht immer eine Zwischenvariable.
Ggf. eine Kettenform (`{ call: [f], then: len }`) oder ein `pipe`.

### 12. Borrow-Checker: `ptr<T>`-Member in einen anderen Member kopieren

Wird nicht beanstandet (nur lokal), führt aber zu doppeltem Besitz.  cai nutzt deshalb `shared<T>`.

### 13. Listenliteral an ein Member zuweisen: Absturz

```yaml
- class:
    name: Holder
    members:
      - [names, list<string>, null, { access: public }]
    methods:
      - def:
          name: fill
          args: []
          body:
            - set: [<.names, ["Knoten", "Text"]]
```

`holder.fill` stürzt ab (SIGSEGV in `aiml_list_handle_incref`).  Gefunden in cai (`AimlGraphLearner.setup`).
Umgehung: eine lokale Liste anlegen (`decl: [names, list<string>, ["Knoten", "Text"]]`) bzw. eine
Funktion, die den Eintrag liefert.  Verwandt mit Punkt 3.

**ERLEDIGT 31.10.2026.**  Es betraf nicht nur Member, sondern **jedes** `set` mit Listenliteral,
auch bei einer gewöhnlichen lokalen Variablen (so auch als Punkt 36 in cais `NOTES.md` gemeldet).
Ursache lag im Parser, nicht im Codegen: `StatementBuilder::OnSequenceStart` erkannte die innere
Sequenz nur für `decl`.  Bei `set` blieb `expectingValue` stehen, `OnScalar` verbrauchte das
**erste** Listenelement als skalaren Wert und kehrte früh zurück — der Rest fiel still weg, und
der Skalar landete im Container-Slot (Müll in `.len`, dann SIGSEGV).  `set: [x, []]` lieferte aus
demselben Grund gar keinen Wert ("Failed to evaluate value expression").
Fix: der Parser baut für `set` ein `BufferLiteralExpr`, `visitSetStmt` löst es in `ziel.clear` +
ein `ziel.push` je Element auf — über die gewöhnliche Methoden-Dispatch, damit Member,
memV3-Handles, String-Besitz und Zahlumwandlung ohne zweiten Pfad gelten.
Spec: spec/10 (Semantik: ERSETZEN, `[]` leert) und spec/02 (`set` nimmt genau zwei Elemente;
ein Container nimmt keinen Skalar).  Tests: `tests/test_set_list_literal.aiml` (6 Fälle inkl.
Member), `tests/test_set_arity_error.aiml`, `tests/test_set_container_scalar_error.aiml`.
Dabei fiel auf, dass die vorhandene Prüfung gegen `set: [liste, idx, wert]` **toter Code** war
(sie verlangte `!exprArg`, das `expectingValue` immer belegte) — jetzt zählt der Parser die echten
Skalare und bricht ab.  Nachgewiesen: Korpus 997 Tests ohne neue Fehlschläge, 9rooms Game +
Client bauen ohne einen einzigen Verstoß, Leck-Gate 180/180 bei 0/0.

## Testsuite (zur Kenntnis)

Auf mem-v3-compiler schlagen 12–13 Tests unabhängig von diesen Punkten fehl, u. a.
`test_bundle_*`, `test_pipeline_*`, `test_netfs_smb`, `test_widget_model_bind`, `tests/venv/run.sh`
(verschobenes venv, F68-32 / F68-1).  Gemessen jeweils auch auf dem Stand vor den cai-Änderungen.
