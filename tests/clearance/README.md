# tests/clearance — acceptance probes for spec/65 (symbol clearance)

**Status 17.10.2026: `STAGE=guard` ist GRUEN** (4 positive, 14 negative
Proben). Die 6 Proben mit `# NEEDS: host` sind AUSSTEHEND — sie brauchen einen Wirt mit
Export-Tabelle (spec/65 Schritt 6); die Schreibseite (§5b) ist ebenfalls noch nicht gebaut.

Die Proben wurden **vor** dem Code geschrieben (Arbeitsprinzip 3: Spec und Tests zuerst) und
halten die Entscheidungen fest, die man spaeter leicht falsch macht. Schritt 2 und 3 sind
inzwischen gebaut und gegen den Korpus gemessen (847 Tests, keine Regression); der Rest steht
weiter als Anforderung ohne Umsetzung — und das ist hier vermerkt, statt von einem fehlenden
Test angedeutet zu werden.

```
bash tests/clearance/run.sh        # all
ONLY=03 bash tests/clearance/run.sh
```

## The two session headers

A clearance only exists at a boundary (F65-2), and the boundary is set by the **host**, never by
the source (F65-6). A probe therefore declares its session in its header, and the runner turns it
into environment:

| Header | Meaning |
|---|---|
| `# CLEARANCE: none` | trusted mode — the pass does not run at all (F65-1). **Not** the same as 1. |
| `# CLEARANCE: 1` | the admin console: every annotated level reachable, still restricted (F65-7) |
| `# CLEARANCE: 7` | an ordinary restricted session (`AIML_CLEARANCE`, F65-20) |
| `# STATIC-CLEARANCE: on` | the opt-in flow rule in a trusted build (`AIML_STATIC_CLEARANCE`, F65-17) |
| `# CREATE-LEVEL: 7` | the host permits this session to DEFINE symbols (`AIML_CREATE_LEVEL`, F65-26) |
| `# REPLACE-LEVEL: 7` | …and to REPLACE existing ones, i.e. hot-reload (`AIML_REPLACE_LEVEL`, F65-26) |

## The asymmetry at `0` (F65-22), which four probes guard together

`0` is the value a forgotten field holds, so the two sides of the scale treat it oppositely:
on a **symbol** it is the closed end (reachable by nobody), as a **clearance** it is invalid.
An uninitialised integer must fail, not pass.

| Probe | Pins |
|---|---|
| `neg/n13_clearance_zero_invalid` | clearance `0` is refused outright — never "admin" |
| `neg/n14_level_zero_unreachable` | level `0` is reachable by nobody, **not even** clearance 1 |
| `neg/n02_unannotated_is_zero` | an unannotated symbol is level `0`, so omission never grants |
| `05_admin_is_not_trusted` | …while clearance 1 does reach every *annotated* level |

## What each probe pins

| Probe | Requirement | Pins |
|---|---|---|
| `01_trusted_default` | F65-1, F65-17 | the single-binary case: annotations present, **nothing** enforced — including an edge the opt-in rule would reject |
| `02_clearance_allows` | F65-4 | the ordering `clearance <= level`, equality included |
| `03_grants_gateway` | F65-10 | `grants:` relaxes the flow rule inside one body only |
| `04_flow_check_clean` | F65-17, F65-5 | a monotone chain passes with the opt-in rule on |
| `05_admin_is_not_trusted` | F65-7 | clearance 1 reaches every annotated level… |
| `neg/n12_admin_still_restricted` | F65-7, F65-14 | …and is **still** a console: the dropped dialect applies |
| `neg/n01` | F65-4 | denial above clearance |
| `neg/n02`, `neg/n13`, `neg/n14` | F65-5, F65-22 | die `0`-Asymmetrie, siehe Tabelle oben |
| `neg/n03` | F65-9 | the detour: an open function reaching a restricted one |
| `neg/n04` | F65-6 | no self-certification |
| `neg/n05` | F65-13 | `extern` outside the host's export list |
| `neg/n06`, `neg/n07`, `neg/n08` | F65-14 | the three one-line bypasses: pointer cast, shell, store |
| `06_isolated_defines_freely` | F65-26, F65-27 | eine isolierte Sitzung definiert frei (siehe Schreibseite unten) |
| `07_level_syntax_everywhere` | F65-19 | `level` wird in ALLEN Formen angenommen |
| `neg/n09`, `neg/n10`, `neg/n11`, `neg/n17`, `neg/n18` | F65-17, F65-21 | Annotations-Syntax wird in **jedem** Build geprueft, auch im vertrauenswuerdigen |

A negative probe that is rejected for the *wrong* reason counts as failed — the runner compares
the identifier from spec/65 §10 (F65-21), not merely the exit code. Otherwise a typo anywhere
would make this lab look green.

## Still missing

**F65-18** ("if nothing is annotated and no clearance is set, the pass is not constructed") is the
acceptance test that F65-1 is real, and it cannot be written as a probe — it is a statement about
the compiler, not about a program. It needs an observable: a debug counter or a log line that must
be **absent**. To be added with the implementation, together with a compile-time measurement on an
unannotated corpus file.

## Die Schreibseite (§5b) — und was das Labor daran nicht pruefen kann

Lesen laesst sich mit einer einzelnen Probe pruefen: die clearance allein entscheidet. **Schreiben
nicht**, denn Hinzufuegen, Ersetzen und Kollidieren brauchen einen *Wirt* mit Export-Tabelle, gegen
den geschrieben wird, und `aiml file.aiml -r` hat keinen.

Was das Labor heute abdeckt:

| Probe | Anforderung | Haelt fest |
|---|---|---|
| `06_isolated_defines_freely` | F65-26, F65-27 | eine isolierte Sitzung definiert ihre eigenen Symbole **frei**, und kann die eigene Schoepfung rufen, weil ihr Level die clearance ist |
| `neg/n16_level_in_restricted_source` | F65-27, F65-6 | `level:` in eingeschraenkter Quelle ist Selbstbeglaubigung, keine Deklaration |

**F65-26 war zuerst falsch, und die Proben haben es aufgedeckt.** Der Entwurf verlangte
`create_level` fuer jede Definition; weil jedes aiml-Programm ein `- def: main` braucht (spec/08),
haette die Vorgabe `0` damit *jedes Skript* verboten. Die Fassung von 17.10.2026 trennt: eigene,
isolierte Definitionen sind frei — geregelt ist nur, was in die **Tabelle des Wirts** gelangt.
Die frueher hier stehende Probe `n15_create_forbidden` ist entfallen; ihr Nachfolger braucht einen
Wirt und steht unten.

**Die Wirtsvorrichtung gibt es seit 17.10.2026** (`09_host_exports_callable`): ein Wirt in einer
Datei, der eigene `def`s per `sys.export_symbol` mit Adresse und Level anmeldet und ein Buendel
in einer eingeschraenkten Sitzung dagegen laufen laesst. Damit sind die Anforderungen der
Schreibseite gemessen statt nur spezifiziert — `# NEEDS: write`:

- **F65-26 / `clearance-create`** — ein Symbol in die Tabelle des Wirts legen (nur im geteilten
  Kontext moeglich, F65-31) ohne `create_level`. Der Waechter ist gebaut, aber **heute nicht
  erreichbar**, und das ist ein Befund ueber die Sprachmittel, nicht ueber die Anforderung:
  `sys.bundle` nimmt den NAMEN einer Funktion, die der Wirt selbst uebersetzt hat, also traegt
  jedes Buendel einen Namen, den der Wirt SCHON HAT — und landet im ERSETZEN-Zweig. Gemessen
  17.10.2026 an `fresh_helper`. Die beiden Proben (`11_create_allowed`, `neg/n20`) stehen
  darum auf `# NEEDS: dynload` und melden AUSSTEHEND, bis spec/29_2 §4.3 existiert.
- **F65-28 / `clearance-replace`** — einen Rumpf fuer ein Wirtssymbol liefern ohne
  `replace_level`: `neg/n21`. Dort ist `create_level` ERTEILT und `replace_level` nicht — das
  ist der Punkt: die beiden Rechte sind getrennt, und das eine erteilt nicht das andere.
- **F65-29** — ein `version:` in ankommendem Code ist Eingabe, nicht Berechtigung
  (spec/29_2 §4.2): `neg/n23` mit `version: 99` und ohne `replace_level`. Die Reihenfolge der
  Pruefungen IST der Inhalt: zuerst die Berechtigung, dann entscheidet die Version.
- **F65-30 / `clearance-collision`** — ein Name, den der Wirt schon hat, muss ein Fehler sein;
  die Skip-Logik gaebe dem Skript stillschweigend die Funktion *des Wirts*: `neg/n22`, mit
  erteiltem `replace_level`, damit nicht die Berechtigung die Aussage traegt.
- **F65-32** — die Richtlinie ist lesbar, nie schreibbar: `12_policy_readable` (die drei Leser)
  und `neg/n24` (ein Griff nach einem Setzer nennt den Grund, statt wie ein Tippfehler
  auszusehen). Die Verschaerfungsregel ist `neg/n25`: die Umgebung kappt ein verlangtes
  `replace_level`. Dass es immer das STRENGERE gewinnt, egal aus welcher Quelle, steht im
  Kopf jener Probe mit der zweiten, inline gemessenen Richtung — eine Probe allein
  unterscheidet „gekappt" nicht von „Aufruf ignoriert".

**F65-34 / `clearance-writedown`** (Schritt 8, `# NEEDS: writedown`) hat drei Proben:
`13_writedown_clean` fuer die erlaubten Richtungen (gleich und strenger), `neg/n26` fuer das
Beispiel des Kapitels und `neg/n27` fuer die naheliegendste Umgehung — ein unannotiertes `tmp`
in der Mitte. `n27` ist die wichtigere der beiden Negativen: wenn eine Lokale ohne `level:` die
Herkunft vergaesse, waere die Regel mit einem Zwischenschritt erledigt.
Die sechs benannten Grenzen des Durchlaufs stehen in spec/65 §9 Schritt 8 — darunter die, dass
eine unannotierte Huellfunktion wascht und dass eine Schleifen-Rueckwaertskante einen Fluss
verdecken kann.

Zwei Anforderungen bleiben spezifiziert und UNGEPRUEFT, und das steht hier, statt von einem
fehlenden Test angedeutet zu werden:

- **F65-27** (das Level eines angelegten Symbols IST die Freigabe der Sitzung) haengt am
  selben Punkt: ohne §4.3 wird ein angelegtes Symbol nie aufrufbar, also ist sein Level nicht
  beobachtbar. Wer §4.3 baut, loest beide Proben ein — und muss F65-27 dort einloesen.
- **F65-31** (eingeschraenkte Sitzungen laufen isoliert; ein AOT-Wirt kann ueberhaupt nicht
  hot-reloaden, weil `context = false` dort `-2` liefert) ist eine Eigenschaft des
  Wirts-Aufbaus. Baulich erfuellt: `aiml_exec_code` kehrt fuer den isolierten Kontext um,
  BEVOR die Schreibseite ueberhaupt erreicht wird — die Pruefungen sitzen hinter dieser
  Abzweigung, nicht davor.

## STAGE: warum dieses Labor nicht einfach „rot" ist

Spec-first heisst: die Proben stehen, bevor es Code gibt. Ein Labor, das deshalb dauerhaft rot
leuchtet, sagt aber nichts mehr — man liest es nach drei Tagen nicht mehr. Darum nennt jede Probe
im Kopf den Umsetzungsschritt, den sie braucht, und der Runner bewertet nur den erreichten:

```
# NEEDS: step2     Syntax annehmen + `level`/`grants` validieren (immer an, F65-17/F65-21)
# NEEDS: step3     opt-in Flusspruefung (AIML_STATIC_CLEARANCE, F65-9/F65-17)
# NEEDS: session   eingeschraenkte Sitzung (F65-4 … F65-22)
# NEEDS: guard     Waechter ohne Wirt: Builtins, Dialekt, `extern` (F65-13/F65-14/F65-35)
# NEEDS: exports   Export-Tabelle des Wirts, Erwartung ist eine Diagnose (F65-23)
# NEEDS: host      Wirt mit AUFRUFBAREN Symbolen: Signatur + Adresse (F65-24/F65-37)
# NEEDS: write     die Schreibseite: create/replace/Kollision/Leser (F65-26 … F65-32)
# NEEDS: writedown die Write-down-Regel (F65-34), Wertfluss IM Rumpf
# NEEDS: dynload   braucht spec/29_2 §4.3 — nicht dieses Kapitel

STAGE=step2 bash tests/clearance/run.sh     # Vorgabe
STAGE=writedown bash tests/clearance/run.sh # alles
```

Proben oberhalb von `STAGE` melden **AUSSTEHEND** und zaehlen nicht als Fehlschlag.

### Was step2 prueft

| Probe | Pins |
|---|---|
| `01_trusted_default` | Annotationen vorhanden, **nichts** durchgesetzt (F65-1) |
| `07_level_syntax_everywhere` | `level` wird in ALLEN Formen angenommen: `decl` 4. Position (mit/ohne `lifetime`, mit `null`-Initialisierer), `def`-Schluessel, `class` + Mitglied, `model` + Feld, `enum`, `interface`, `classgroup` |
| `neg/n09`, `neg/n10`, `neg/n17` | `invalid-level` — Nicht-Ganzzahl, ausserhalb 0…255, an der 4. Position eines `decl` |
| `neg/n18` | dieselbe Pruefung am Klassenmitglied (derselbe Modifier-Block, spec/03) |
| `neg/n11` | `invalid-grants` — `grants:` ueber `level:` |

### ⚠️ Das Labor liegt im Korpus von `run_all_tests.sh`

`run_all_tests.sh` sammelt mit `find tests/ -name "*.aiml"` und schliesst nur `lifetime/neg` aus.
`tests/clearance/` **und** `tests/closures/` landen damit im Korpus, obwohl beide
Spec-vor-Code-Labore mit eigenem Runner sind — die Suite kann dadurch nicht gruen werden, und
negative Proben werden dort nach Rueckgabewert beurteilt statt nach ihrer `EXPECT-ERROR`-Kennung
(`n04`/`n05` erscheinen deshalb als „OK", obwohl sie angenommen wurden, was im Labor ein
Fehlschlag ist). Vorschlag: beide Verzeichnisse wie `lifetime/neg` ausschliessen. Heute **nicht**
getan, weil das die Vergleichszahlen einer laufenden Messung verschoben haette.

## Wie dieses Labor zur Hauptsuite steht (17.10.2026)

Das Labor ist ein **Treiber** nach der vorhandenen Konvention `tests/<dir>/run.sh`, wie
`pipeline_purity/`, `pipeline_graph_neg/`, `lint_globals/` und `bundle_socket/`:

- `run_all_tests.sh` ruft `tests/clearance/run.sh` **als einen Eintrag** und uebernimmt dessen
  Rueckgabewert. Bewertet wird damit nach der `EXPECT-ERROR`-Kennung aus spec/65 §10, nicht
  nach dem Rueckgabewert einer einzelnen Probe.
- Jede Probe traegt `# Requires: driver`; die Hauptsuite ueberspringt sie damit **sichtbar**
  („laeuft ueber run.sh seines Verzeichnisses") statt sie stillschweigend wegzufiltern.

Vorher lagen die Proben einzeln im Korpus. Das war aus zwei Gruenden falsch: negative Proben
wurden nach Rueckgabewert beurteilt (eine angenommene Probe erschien als „OK", obwohl sie im
Labor ein Fehlschlag ist), und die noch nicht gebauten Stufen erzeugten dauerhaft rote Zeilen,
die niemand mehr liest. Ausgeschlossen wurde **nichts** — die Proben laufen weiter bei jedem
Korpuslauf, nur durch ihren eigenen Runner.

## Die Export-Tabelle als Vorrichtung (Stufe `exports`, 17.10.2026)

Sechs Proben brauchten einen Wirt. Beim Durchdenken zerfielen sie in zwei Haelften, und das
war der Schnitt fuer Schritt 6:

| | braucht | Proben |
|---|---|---|
| `# NEEDS: exports` | nur **Name und Level** des Wirts — die Erwartung ist eine Diagnose | `neg/n01`, `neg/n02`, `neg/n14`, `08_extern_admitted` |
| `# NEEDS: host` | **aufrufbare** Symbole (Signatur + Adresse, F65-24) | `02`, `03`, `05` |

`host_exports.txt` ist das Angebot des Wirts, eine Zeile je Symbol (`Name|Level`), und der
Runner reicht sie ueber `# EXPORTS:` als `AIML_EXPORTS` herein. Signatur und Adresse fehlen
darin **bewusst**: sie braucht erst das Aufrufen, und dafuer muss ein echter Wirt sie anmelden.

Dass die Tabelle per Umgebung kommt, ist kein Widerspruch zu F65-32 („env darf nur
verschaerfen"): dort geht es um die Grenzen der SITZUNG (clearance, create_level,
replace_level). Ein Eintrag in der Export-Tabelle ist das **Angebot des Wirts**, und anzubieten
ist sein Vorrecht — wer `AIML_EXPORTS` setzt, ist der Wirt, dieselbe Begruendung wie bei
`AIML_CLEARANCE`.

Die drei `host`-Proben tragen jetzt einen Vermerk **UMBAU NOETIG**: sie definieren ihre Symbole
noch selbst mit `level:`, was seit F65-27 Selbstbeglaubigung ist. Sie sind bewusst nicht
„repariert" worden — eine Probe ohne Wirt kann die Aussage „ein Symbol, das ich nicht erreichen
darf" nicht treffen, und ein stiller Umbau haette den fehlenden Wirt verdeckt.
