# Spec 61: Implementierungspläne für Handle-SHARE, Return-Ownership und Deserialisierung

Stand: 2026-09-25. Gelesener AIML-HEAD: `a281adb` (Listenalias-Fall-through und Self-Move-Absicherung). **Plan, keine Umsetzung.** Historische Leak-Zahlen stammen aus den verlinkten Probes; vor Implementierung mit frisch gebautem Compiler verifizieren.

- [Round6-Befunde und Probes](../../9rooms_v2/leak_probes/round6_general/README.md)
- [Round5-Deserialisierungsbefund](../../9rooms_v2/leak_probes/round5_interpolation/README.md)
- [Spec 61](../spec/61_memory_management.md)
- [Bestehender Inline-Value-Modell-Plan](spec61_inline_value_model_release_plan_2026-09-24.md)

## Ausgangslage und gemeinsame Regeln

`a281adb` erhält bei Listenaliasen nun den notwendigen Fall-through-Drop. Sein DeepCopy-Schutz für einen lokalen Container, der in ein Containerfeld zurückgeschrieben wird, verhindert den beobachteten Self-Move-Crash. Er implementiert noch nicht das nach §5 geforderte SHARE. Diesen Schutz erst ersetzen, wenn echter Handle-Install und Self-Slot-/Alias-Tests gemeinsam grün sind. Finding 2/JsonStyle und Finding 5/Listenalias sind keine erneut zu implementierenden Arbeitspakete; ihre Tests bleiben Regressionen.

Verbindliche Regeln:

- Container sind überall pointergroße Handles (§5), auch in Modellfeldern. Gewöhnliche Zuweisung teilt denselben Container; die Quelle bleibt gültig.
- Secure-before-release und Self-Slot-No-op (§5.5). Ein echter MOVE leert seinen Quellbesitz zur Laufzeit vor dem Ziel-Drop.
- Ein gewöhnlicher String-/kopierbarer Modell-Return liefert dem Caller Besitz. `producer` garantiert zusätzlich Frische/äußere Unabhängigkeit und ist keine Voraussetzung für einen Drop (§4/§7/§8/§9.2).
- Modell-Copy ist rekursives Copy/Retain nach Feldklasse. Containerfelder bleiben geteilt; Deep-Copy nur bei ausdrücklichem Clone-Vertrag (§7).
- Borrow (`ref<T>`) von owned Ergebnis unterscheiden. LLVM-Pointertyp, Funktionsnamenssuffix oder Herkunft aus einem Call allein beweisen keinen Besitz.

## Plan 1 — Round6 Finding 4, danach Finding 1 und Finding 3

### Ziel und Lieferreihenfolge

Drei getrennte Implementierungs-Commits mit eigenen grünen Regressionen. **P1-B/C sind vorerst nicht implementierungsreif:** Zuerst P1-R (Revert-Aufarbeitung, Spec-Präzisierung und Client-Gate) erfüllen. P1-A erhält ebenfalls das unten genannte Verhaltens-Gate:

1. **P1-A: echtes Handle-SHARE** für `set: [box.items, src]` — begrenzt und zuerst lieferbar.
2. **P1-B: zweiseitiger String-Return-Vertrag** — Caller und Callee in einem atomaren Änderungsschritt.
3. **P1-C: zweiseitiger Value-Modell-Return-Vertrag** — nutzt die Ergebnis-Klassifikation aus P1-B und Copy/Retain/Drop aus dem bestehenden Modellplan.

P1-A benötigt P1-B/C nicht. P1-C setzt korrekte rekursive Retain-/Drop-Helfer voraus; fehlende Feldklassen zuerst in deren gemeinsamem Helper ergänzen, nicht pro Returnstelle Sonderkopien einführen.

### P1-R — zwingende Voraussetzung: alten Revert und Client-Verhalten absichern

**Nachgetragenes Review:** Der ursprüngliche Plan hat den früher verworfenen String-Return-Klassifikator und die erforderliche Client-Abnahme nicht ausreichend berücksichtigt. [Inline-Modell-Plan §10.1 B](spec61_inline_value_model_release_plan_2026-09-24.md) dokumentiert die Regression **Deck → Card-Editor trotz grüner Leak-/Sanitizer-Tests**. Im aktuellen `ast_compiler.cpp` ist `classifyOwnedStringReturn` weiterhin deaktiviert (`[[maybe_unused]]`, auskommentierte Aufrufstellen). Der separate Memory-Eintrag `[[owned-return-and-pop-clear-leaks]]` wurde für dieses Review nicht direkt gefunden; der Befund ist über das vorhandene Repository-Dokument und die deaktivierten Aufrufstellen bestätigt.

Der hier vorgeschlagene deklarationsbasierte, beidseitige Return-Vertrag ist konzeptionell etwas anderes als ein aus Funktionskörpern abgeleitetes Producer-Auto-Detect: Besitz und Frische werden getrennt. **Das belegt aber nicht, dass die frühere Regression ausgeschlossen ist.** Ein vollständiger Rebuild behebt nur gemischte Ownership-Konventionen, nicht fehlerhafte Lifetimes oder verändertes Client-Verhalten. Auto-Detect nicht nebenbei reaktivieren.

Vor P1-B/C:

1. Alten Versuch/Revert und betroffene Navigationsfunktionen rekonstruieren. Pro fehlgeschlagenem Pfad vorheriges und geplantes Retain-/Drop-Verhalten einschließlich Parameter, Member, Event-/Callback-Nutzung und Imports darstellen. Falls der alte Patch nicht rekonstruierbar ist, diese Evidenzlücke ausdrücklich offenlassen.
2. Wiederholbares echtes Client-Szenario auf dem bisherigen funktionierenden Stand aufbauen: Start/Login mit isolierten Testdaten → Deckliste → Deckeditor → Karte öffnen → Card-Editor → zurück; Auswahl, richtige Karten-/Deck-ID, Inhalte, Änderungen/Speichern und Rücknavigation prüfen. Mehrfach hin und zurück navigieren, asynchrone Antworten vollständig abwarten. Zusätzlich Lobby → Match → zurück, soweit die geänderten Rückgabepfade dort beteiligt sind.
3. Nachweisen, dass der Test den damaligen Fehler erkennt, möglichst gegen den archivierten fehlerhaften Build. Ist dieser nicht verfügbar, bleibt die historische Detektionsfähigkeit unbewiesen; ein gezielt fehlschlagender Navigations-/Identitätscheck validiert lediglich den Testmechanismus.
4. Im **selben Client-Szenario** Speicherentwicklung messen: feste Fixture, Warm-up, gleiche abgeschlossene Navigationszyklen, nach Abbau kurzlebiger Screens vergleichen. Lebende AIML-Allokationen/Referenzen bzw. allocator-live-bytes verwenden; WASM-Linear-Memory-Kapazität oder RSS allein beweisen keinen Leak, da der Speicher nicht zwingend schrumpft. Caches und absichtlich weiterlebende Screens als Besitzer dokumentieren. Für die untersuchten Pfade kein zyklusproportionales Restwachstum zulassen; Messmethode und etwaige Baseline-Störanteile vorab festlegen.
5. Browser-/WASM-Build des echten 9rooms-Clients mit `exodus-client.json` und passender frisch gebauter WASM-Runtime, danach obiges Szenario. Vorhandenes `aiml/run_wasm_tests.sh` ist ein zusätzlicher Node/WASM-Check, **kein Ersatz** für Browsernavigation, Rendering und asynchrone Clientintegration. Der konkrete Build-/Browser-Runner ist vor Umsetzung zu verifizieren; hier wird kein bereits existierendes automatisiertes UI-Gate behauptet.

**Spec vor P1-B/C:** §8 sagt ausdrücklich über kopierbare Value-/Container-Returns, dass String-/Container-/shared-Felder in den Caller geteilt werden und Callee-Locals regulär droppen. Das deckt den normalen Modellreturn. Für einen eigenständigen plain `string`-Return ist die bisherige Regel aus §4 plus §8 abgeleitet; eine ebenso ausdrückliche eigene Regelzeile fehlt im gelesenen Text. Vor Implementierung in Spec 61 explizit festhalten: „Ein `return: string` liefert dem Caller eine owning Stringreferenz; ein bestehender String wird geteilt, ein owned Temporary kann übernommen werden; ohne `producer` wird keine Frische garantiert.“ Ebenso Temporary-Drop/Weitergabe und den separaten Producer-Vertrag abgleichen. Das ist eine geplante Spec-Präzisierung, keine in diesem Review bereits vorgenommene Spec-Änderung. Keine automatische Gleichsetzung von owned und producer.

Bis diese Voraussetzungen erfüllt sind, bleibt die bestehende gezielte Annotationstrategie der operative Ansatz. Eine neue Producer-Annotation ist nur bei belegter Frische/Unabhängigkeit zulässig; sie darf einen bloßen geteilten Feldreturn nicht umetikettieren. P1-B/C bleiben der geplante systematische Fix, nicht ein freigegebener breiter Auto-Detect-Rollout.

**P1-A-Verhaltensrisiko:** Der Wechsel von bisherigem Move/DeepCopy zu SHARE ändert sichtbare Alias-Semantik. Betroffene Stores und nachfolgende Mutation der Quelle im Game **und Client** inventarisieren. Wer echte Unabhängigkeit benötigt, muss nach fachlichem Vertrag explizit `copy` oder `clone` verwenden; nicht sämtliche Aufrufer zur Konservierung eines Compilerfehlers automatisch klonen. Insbesondere Deck-/Card-Editor und die Self-Alias-Writeback-Fälle mit Inhalts-/Mutationsassertions prüfen. Ein reines 0-Byte-Gate genügt auch für P1-A nicht.

### P1-0 — Baseline und Regressionen vor Codeänderung

- Compiler/Runtime neu bauen; Commit, Flags, Binary-Hashes und Quellstand gemeinsam festhalten. Vorhandene Vorher-Logs nicht überschreiben.
- Round6 `list_member_set_10/1000`, `list_member_set_return_1000`, `list_member_set_read_1000`, `return_plain_10/1000`, `return_producer_1000`, `box_plain_10/1000`, `box_producer_1000` erneut messen. Historisch: 40 B/Set, 20 B/plain String-Return, 13 B/plain TextBox-Return; konkrete Allokatorgrößen sind keine dauerhaft festgeschriebene Semantik.
- Dauerhafte Tests in `tests/lifetime/` anlegen; freie Nummern bei Umsetzung wählen. Vorgeschlagene Namen: `container_member_share`, `plain_string_return_owned`, `plain_model_return_owned`. Analysevarianten/Logs bleiben in `9rooms_v2/leak_probes/`.
- Diese Fälle als offene RED-Fälle im bestehenden `tests/run_lifetime_leak_gates.sh` führen und mit dem jeweiligen Fix nach GREEN verschieben. Normale Lifetime-Suite darf offene Red-Probes nicht versehentlich als erwartete grüne Tests aufnehmen: bis dahin in einem nicht automatisch ausgeführten Unterordner ablegen oder explizit ausschließen.

### P1-A — Handle-SHARE im Member-Store

**Änderungsstellen:** `src/core/ast_compiler_stmt.cpp` (`visitSetStmt`, Container-Decl/Release-Helfer), Expression-/Property-Auflösung, `src/core/ownership_analyzer.cpp`, `ownership_flow.h` und zugehörige Transferauswertung.

1. Zieltyp und geplante Slot-Auflösung vor dem bisherigen pauschalen `consumeLocalDrop` bestimmen. Einen konkreten Slotpointer nur dann über die RHS-Auswertung hinweg halten, wenn seine Lebensdauer und Adresse dabei nachweislich stabil bleiben. Für memV3-Class-3-Ziele einen expliziten `Transfer::Share` planen; die normale benannte Quelle behält ihren Besitzerstatus und ihren Drop. Den `DeepCopy`-Workaround aus `a281adb` nur für diese nun korrekt implementierten Stores ersetzen.
2. RHS einmal auswerten und als **Handlewert** normalisieren. Für benannte Container den Handle aus dem Slot laden; für Felder die bestehende Receiver-/Slot-Unterscheidung beachten. Besessene Call-/Read-Temporaries zusätzlich kennzeichnen. `at/get` eines String-/Container-Elements liefert gemäß §5.4/§5.7 einen owning Handle; dessen Retain muss genau einmal erfolgen, am Read oder bei seiner unmittelbar zusammengefassten Materialisierung. Ein bereits retained Read-Ergebnis im Install übernehmen, nicht nochmals increfen. Ein ignorierter Read muss seinen erworbenen Besitz ebenfalls droppen. Niemals einen `{size,cap,data}`-Wert oder dessen Adresse ungeprüft als Handle interpretieren.
3. Für `box.items` den Slot mit `resolvePropertyWritePtr(..., wantContainerSlot=true)` erhalten. In diesem memV3-Pfad ist ein Struct-Ergebnis ein Codegen-Vertragsfehler: Diagnose statt Fall-through in `Struct-content copy`.
4. Zuweisung über einen gemeinsamen typgesteuerten Handle-Install-Helper emittieren. Elementklassen, rekursive Deskriptoren und Strides aus bestehender Typklassifikation/DataLayout nutzen; keine fest verdrahtete Elementgröße 8.
5. Nach erfolgreichem Install aus diesem Pfad zurückkehren. Legacy-Memcpy und automatische DeepCopy-Zweige dürfen ihn nicht mehr übernehmen.

Vorgeschlagene neue Schnittstelle (Skizze, kein existierender Helper):

```cpp
struct ContainerResult {
    llvm::Value* handle;
    llvm::Value* sourceSlot;  // optional, identifies storage; not the referent
    ReferenceOwnership ownership; // BorrowedReference or OwnedTemporary
    ResolvedType type;
};

emitInstallContainerHandle(destinationSlot, result);
```

Install-Reihenfolge:

```text
RHS genau einmal auswerten; aufgelöste Slotidentität prüfen
identischer Quell-/Zielslot -> No-op
benannte/geborgte Referenz -> retain(newHandle)
owned Temporary -> Besitz sichern und Quellslot/Live-Flag leeren
release(oldHandle, vollständige Typmetadaten)
store newHandle in destinationSlot
übernommenes Temporary aus Cleanup konsumieren
```

P1-A darf einen unbekannten normalen Container-Call nicht allein wegen seiner Syntax als owned Temporary behandeln. Für unterstützte Callpfade das tatsächliche Return-Ownership normalisieren; ein noch nicht migrierter Legacy-Borrow braucht einen expliziten Retain-Adapter. Eine Änderung seines Return-Vertrags muss Caller und Callee gemeinsam erfassen. Die Aussage, P1-A sei unabhängig lieferbar, gilt für den benannten SHARE-Kern; zusätzliche Callpfade werden erst mit verifiziertem Boundary-Vertrag freigegeben.

Bei zwei verschiedenen Slots mit gleichem Handle bleibt Retain-vor-Release korrekt. Nicht Pointergleichheit der Referenten als Beweis für fehlende Besitzpflicht verwenden. Nicht bei jeder Zuweisung einen zusätzlichen Scope-Drop registrieren; der bestehende Ziel-Drop liest den aktuellen Slot. Bei allgemeiner Ausweitung auf `idx` Zielslot nicht über eine durch RHS ausgelöste Container-Reallokation hinweg zwischenspeichern.

**Abnahme P1-A:**

- Set-/Set+Read-Probes: 0 definite/indirect, keine Invalid-Access-/Free-Fehler.
- `src.push` nach Zuweisung verändert `box.items.len`, und umgekehrt; jeweils über ursprüngliche Kapazität hinaus testen. Das beweist gemeinsame Header-/Wachstumssemantik.
- Gleicher Slot, gleicher Handle in verschiedenen Slots, Alias-Writeback `tmp=box.items; box.items=tmp`, gefülltes altes Ziel, wiederholtes Rebinding und temporäre RHS.
- `test_class_list_struct_scalar_writeback` und `56_list_share_alias_no_leak` bleiben grün; diese schützen den bereits gelieferten Commit.
- Dieselben Install-Regeln gezielt mit `list<string>`, `list<list<i32>>`, `map<string:string>` und `buffer` prüfen. Zusätzlich `list<ptr<Node>>` teilen: der Container bleibt ein sharebarer Handle, auch wenn seine Elemente move-only sind (§5.1). Die Boxen dürfen erst beim letzten Containerbesitzer freigegeben werden; keine Elementkopie und kein DeepClone beim SHARE. Releases müssen letzte Besitzer und Elemente korrekt behandeln.

### P1-B — gewöhnlicher String-Return mit einer Caller-Referenz

**Änderungsstellen:** `ast_compiler_stmt.cpp` (`visitReturnStmt`, String-Decl/Set), `ast_compiler_call.cpp` (freie Calls und Methoden), Funktions-/Interface-Metadaten in `context.h`, Ownership-/Temporary-Verwaltung. Auch Multi-File-Importe und indirekter/Interface-Dispatch müssen denselben Vertrag kennen.

**Entscheidung:** Interne AIML-Funktion mit Ergebnis `string` liefert grundsätzlich eine Caller-eigene Referenz; `producer` ist ein zusätzliches Unabhängigkeitsmerkmal. Builtins/FFI anhand ihres tatsächlichen Vertrages klassifizieren und gegebenenfalls mit einem Retain am Adapter normalisieren. Ein extern gelieferter unverwalteter `char*` ist kein beweisbar gültiger AIML-Stringhandle.

Eine auf dem aufgelösten Funktionssymbol basierende Ergebnisklassifikation einführen, z.B. `OwnedStringReference`, `OwnedValueAggregate`, `BorrowedReference`, `Scalar`. Diese Information muss über Deklarationen/Importe hinweg verfügbar sein. `stringProducerFns` bzw. `ownsReturnMatches(shortName, ...)` nicht mehr als alleinigen Eigentumsnachweis normaler Value-Returns verwenden. Gleichnamige Methoden dürfen keine fremden Verträge erben.

**Callee:**

- Nachgewiesen owning Interpolations-/Builtin-/Call-Temporary: die vorhandene Referenz zurückgeben und dessen Drop konsumieren. Besitz aus dem Ergebnis-Record nehmen, nicht pauschal aus `StringInterpolationExpr`: eine Interpolation ohne Konvertierung/Verkettung kann intern nur einen vorhandenen String weiterreichen und benötigt dann Retain wie ein Borrow.
- Benannter lokaler String, Parameter oder Modellfeld: vor Scope-Cleanup eine Referenz für das Ergebnis sichern. Korrekte einfache Referenzimplementierung: incref für Return, dann regulärer lokaler Drop. Bei geteiltem persistentem Besitzer bleibt dessen Referenz erhalten.
- Literal/immortales Leerstring: derselbe Vertrag mit no-op Retain/Release; keine notwendige neue Allokation.
- Ergebnis vor sämtlichen lokalen Drops sichern; Interpolations-Zwischenergebnisse und andere lokale Felder weiter freigeben. Die gegenwärtige Producer-Bedingung bei `retStrFieldOfOwnedModel` darf normale Returns nicht länger von notwendigem Retain/Owner-Cleanup ausschließen.

**Caller:**

- Ergebnis als besessenes String-Temporary registrieren, unabhängig von `producer`.
- `decl`/`set` übernimmt genau diese Referenz; kein zusätzliches incref wie derzeit bei vermeintlichem Borrow.
- Nicht gebundener oder nur transient benutzter Return wird nach Ende seiner Nutzung gedroppt. Bedingungs-/Returnausdrücke und verschachtelte Calls brauchen Cleanup auch ohne gewöhnliches Statement-Fall-through.
- Beide Seiten in einem Commit ändern. Nur die Caller-Registrierung einzuschalten kann bislang geliehene Returns überfreigeben; nur Callee-Retain einzuschalten kann neue Leaks erzeugen.

**Testmatrix P1-B:** frischer Return mit/ohne Producer, Literal, Parameter, lokaler String, Feld eines lokalen Modells, Feld eines länger lebenden Besitzers, verketteter Wrapper; Nutzung in Decl, Rebind, Interpolation, Log/Print, ignoriertem Call, Bedingung und erneutem Return. Methoden/Interfaces und zwei Dateien einschließen. Ein gewöhnlicher Feldreturn bleibt ohne Producer zulässig; nicht den Workaround als neue Sprachpflicht etablieren.

**Abnahme:** Plain- und Producer-Varianten beide 0 Byte; Inhalte korrekt; zurückgegebener String überlebt lokale Callee-Owner, während deren Geschwisterfelder freigegeben werden. Keine neue Producer-Garantie für geteilte Feldreturns behaupten.

### P1-C — gewöhnlicher Value-Modell-Return mit Caller-Drop

**Änderungsstellen:** `ast_compiler_stmt.cpp` (`modelOwnedInit`, Modell-Decl/Set/Return), `ownership_analyzer.cpp::freshOwned`, Ergebnis-Metadaten aus P1-B und gemeinsame rekursive CopyRetain-/Drop-Helfer. Rückgabe-ABI (Struct by value/indirekter Ergebnisbereich) berücksichtigen.

**Entscheidung:** Kopierbare Modell-Returns sind eigene Value-Kopien, deren RC-Felder geteilt werden (§7/§8). Zunächst eindeutigen CopyRetain-Referenzpfad implementieren; Move-Elision erst anschließend, sofern das vollständige Besitzverhalten identisch bleibt.

1. Callee baut für ein **kopierbares** Modell einen eigenständigen Ergebniswert durch member-wise Copy/Retain auf, **bevor** seine Locals droppen. Scalars kopieren; String-/Container-/shared-Felder retainen; verschachtelte Aggregate und Arrays rekursiv behandeln. `ref`-Felder nur als schwache Zeiger kopieren. Class-5-Ressourcen (`chan`, `lock`) und `shared<T>` mit ihrem atomaren Retain/Release behandeln; Class-6-Singletons und echte borrowed Handles nicht rekursiv freigeben (§2.1/§6.1).
2. Caller übernimmt den vollständigen owning Ergebniswert. `modelOwnedInit` und Flow-Analyse müssen normale Modellwert-Calls einschließen; keine Beschränkung auf `owns_return`/Producer. Wirkliche `ref<Model>`-Returns bleiben non-owning.
3. Decl mit Initialwert: kein unnötiges, anschließend überschriebenes Default-Heap anlegen. Falls schon initialisiert, vorhandenen Besitz vor Ersetzung korrekt droppen. Spätere `set`-Zuweisung: neues Ergebnis vollständig sichern, alten Wert droppen, neuen übernehmen.
4. Modell-Temporaries typisiert verwalten. Ein ignorierter oder nur als Argument verwendeter Modellreturn benötigt rekursiven Drop; die reine String-Temp-Liste genügt dafür nicht. Bei erneutem Return den Ergebnisbesitz korrekt weiterreichen. Im CopyRetain-Referenzpfad wird ein owning RHS-Temporary nach Aufbau der neuen Ergebnisreferenzen genau einmal rekursiv gedroppt; bei tatsächlicher Adoption wird stattdessen sein Cleanup konsumiert. Diese Unterscheidung auch für `return make_model()` und `return clone(...)` testen, sonst kann der Zwischenwert trotz korrekt gedropptem Endergebnis leaken.
5. Move-only Modelle nicht durch CopyRetain duplizieren: separater MOVE mit Runtime-Empty der owning Felder bzw. geprüftem Drop-Flag. Borrow/Move-only-Negativtests erhalten.

**Testmatrix P1-C:** TextBox-Minimalprobe; Modell mit String und Listenfeld; Return eines Parameters; Return eines lokalen Wertes; mehrere Returnpfade; direktes Feld-/Argument-Forwarding; ignoriertes Ergebnis; Rebinding; verschachtelte Modelle/Arrays; Alias überleben lassen; move-only Modell und echter Borrow-Return als Gegenkontrollen. Beim kopierbaren Modell sind Scalarfelder unabhängig, Containerfelder müssen dieselbe Mutation sehen. Deep-Copy wäre trotz Leak-Freiheit falsch.

**Abnahme:** `box_plain_*` und `box_producer_1000` jeweils 0 Byte. Ein eigener Caller-Drop darf nicht einen vom Callee ungesichert geliehenen Modellgraph freigeben. Deshalb Callee- und Caller-Änderungen auch hier zusammen liefern.

## Plan 2 — Round5 Finding B: Deserialisierung → Value-Modell (zurückgestellt)

### Status, Ziel und Abgrenzung

Separat ausführbarer Plan; **weiterhin zurückgestellt**, bis er zur Implementierung ausgewählt wird. Das Dokumentieren hebt die Zurückstellung nicht auf. Ziel ist die sichere Übernahme von `sys.deserialize(json, "Model")` in einen inline gespeicherten Modellwert, einschließlich wiederholter Zuweisung und Freigabe des internen Ergebnisblocks.

Historische Kontrolle: `original_100` und `deser_only_100` verlieren beide 28.900 Byte; Originalfall 289 Byte (48 direkt, 241 indirekt). Interpolation ist nicht die Ursache dieses Befunds. Neue Baseline nötig: alte Messung nicht als Beweis für jeden späteren Build behandeln.

**Änderungsstellen:** `serialize_impl.cpp::emitDeserializeStruct` (malloc-Ergebnisblock), `SerializeImpl::deserialize`, `ast_compiler_call.cpp` (`sys.deserialize`-Dispatch), `ast_compiler_stmt.cpp` (Decl/Set mit Heap-Pointer-RHS), typed Temporary-/Ownership-Metadaten.

Der aktuelle äußere Ergebnisblock ist ein interner malloc-Träger für Modellfelder, nicht automatisch ein öffentliches `ptr<Model>` oder RC-Modell. Seine Freigabe muss aus dieser konkreten Allokationsherkunft folgen. Geliehene KV-/View-Pointer im benachbarten Zuweisungspfad niemals als solche Ergebnisblöcke freigeben.

### P2-0 — eigener Vertrag und rote Tests

- Bestehende Round5-Probes frisch ausführen; Originaltest, Deserialisierung ohne Interpolation und reine manuelle Aufbaukontrolle beibehalten.
- Neue Regression `deserialize_value_model_replace` in der Lifetime-Suite vorbereiten: direkte Decl-Initialisierung, getrenntes Decl+Set, tausend Rebindings derselben Variable, nested Strings/Listen, leeres Modell, Nutzung nach Übernahme, normaler Scope-/Return-Austritt.
- JSON-RHS aus einem Stringfeld des bisherigen Zielmodells testen. Die alte Zielbelegung darf erst nach vollständiger Auswertung/Deserialisierung freigegeben werden. Die ursprüngliche JSON-Eingabe muss dabei weiterhin so lange leben, wie der Decoder sie liest; ihre Herkunft ist kein Move-Signal.
- Bestehendes Verhalten bei ungültigem JSON separat erfassen; diesen Ownership-Fix nicht mit einer neuen Parse-Fehler-Semantik verbinden. Erfolgreich angelegte Teilobjekte müssen auch auf vorhandenen Fehlerpfaden freigegeben werden.

### P2-A — Ergebnisherkunft typisiert verfolgen

Vorgeschlagener Ausdrucksergebnis-Typ `OwnedDecodedValue` enthält Modelltyp/Deskriptor, Pointer auf den internen malloc-Träger, Cleanup-Zustand und Allokationsart. Registrierung genau am `sys.deserialize`-Dispatch für die betroffene Value-Modell-Variante; primitive Deserialisierung, container-root, `ptr<T>` und RC-Modell-Pfade gesondert klassifizieren.

Kein globales `lastCallWasDeserialize`-Flag: verschachtelte Expressions und mehrere Ergebnisse müssen unabhängig bleiben. Nicht pauschal alle Pointer-RHS oder alle `producer`-Ergebnisse als malloc-Träger markieren. Wenn P1-C bereits typisierte Modell-Temporaries liefert, diese Infrastruktur wiederverwenden; andernfalls einen begrenzten typisierten Ergebnis-Record implementieren. P2 ist nicht auf P1-Bs String-ABI-Änderung angewiesen.

### P2-B — Referenzimplementierung für kopierbare Modelle: CopyRetain, danach Ergebnis freigeben

Vor Auswahl dieses Pfads die rekursive Kopierbarkeit nach §7 bestimmen. Er ist **ausschließlich für kopierbare Modelle** zulässig und zuerst für die bestehenden Round5-Fälle zu implementieren und zu testen. Ein direktes `ptr`-/Class-4-Feld oder move-only Inline-Aggregat schließt CopyRetain aus. Ein `list<ptr<T>>`-Feld dagegen ist selbst ein sharebarer Container und macht das umgebende Modell nicht move-only (§5.1/§7).

Für vom Deserializer unterstützte move-only Modelle direkt P2-C als notwendigen Transferpfad implementieren; dort ist er keine bloße Optimierung. Nicht durch implizites Clone ersetzen und nicht bislang zulässige Modelltypen nur für den Leak-Fix pauschal verbieten. Welche Ressourcentypen überhaupt deserialisierbar sind, richtet sich nach dem bestehenden Serialisierungsvertrag, nicht allein nach Spec 61.

Ablauf für den kopierbaren Fall:

```text
1. Vollständiges owned Deserialisierungsergebnis auswerten.
2. Dessen Modellfelder in unabhängigen Zwischenwert CopyRetain-en (§7).
3. Falls Ziel bereits initialisiert: dessen bisherige owned Felder droppen.
4. Zwischenwert in Ziel übernehmen; Ziel besitzt dessen Referenzen.
5. Deserialisierungsergebnis rekursiv droppen und seinen malloc-Träger freigeben.
6. Temporary-Cleanup konsumieren; Ziel hat genau einen Scope-Drop.
```

Die CopyRetain-Referenzen sichern die neuen Felder gegen Schritt 5 ab. Dies erzeugt keine tiefen Containerkopien. Bei frischer Decl-Initialisierung den alten Ziel-Drop weglassen, wenn das Ziel nachweislich noch uninitialisiert ist. Bei vorherigem `decl state` dessen Default-Container dagegen wirklich freigeben. Ohne wirksamen späteren Ziel-Drop bleibt der neue Graph trotz freigegebenem Träger und korrektem Rebind undicht.

### P2-C — Transfer für move-only Modelle; optionale Optimierung für kopierbare Modelle

Für unterstützte move-only Decode-Ergebnisse ist dieser Pfad zwingend; für kopierbare Modelle erst nach grünem Referenzpfad optimieren. Voraussetzung: frisches, exklusives Compiler-Temporary; kein beobachtbarer Quellowner und keine Quelle innerhalb des alten Zielgraphen.

```text
1. Ergebnis auswerten; Felder in gesicherten Transferwert aufnehmen.
2. Quellbesitz zur Laufzeit leeren (typgerechte Sentinels oder Runtime-Drop-Flag).
3. Alten initialisierten Zielwert droppen.
4. Gesicherten Transferwert ins Ziel verschieben.
5. Nur den leeren malloc-Träger freigeben; Temporary-Cleanup konsumieren.
```

Keine rekursive Freigabe der schon übertragenen Felder in Schritt 5. Kein `free` eines Stringhandles. `memset(0)` ist kein universelles Empty für Ressourcen; Class-4-Felder brauchen ihre jeweiligen Invalid-Zustände. Diese Optimierung ändert keine normale Modell-Copy-Semantik und konsumiert keine benannte Uservariable.

Eine spätere Deserialisierung direkt in einen Ergebnisbereich wäre ein weiterer separater Optimierungsschritt. Nicht direkt ins noch lebende Ziel dekodieren: Das würde die Secure-before-release-/Fehlerpfad-Garantien umgehen.

### P2-D — Cleanup und Gegenkontrollen

- Nicht konsumiertes Decode-Temporary: rekursiver Felddrop plus Freigabe des äußeren malloc-Blocks.
- Im CopyRetain-Pfad: Decode-Temporary vollständig droppen und dessen Cleanup danach austragen; Ziel hält die separat retained Referenzen. Im Transferpfad: keine rekursive Freigabe der übertragenen Felder durch die Quelle, nur den geleerten äußeren Träger freigeben. Beide Varianten brauchen einen eindeutigen konsumierten Cleanup-Zustand.
- Unveränderte KV-/Borrow-Quellen: kein `free` des Quellblocks; bei normaler Modellmaterialisierung CopyRetain statt automatischem Deep-Copy.
- Wiederholter Set auf gefülltes Ziel: alte Liste inklusive Elemente freigeben, legitime fremde RC-Aliase erhalten.
- Normale Returns aus der umgebenden Funktion und ignorierte Decode-Ergebnisse testen. Falls bislang nicht unterstützte Ausdrucksformen auftreten, explizit diagnostizieren statt untypisiert freizugeben.

**Abnahme Plan 2:** Originaltest und Deserialize-only-Probes jeweils 0 definite/indirect; keine Invalid-Reads/Writes/Frees. Rebind-Schleife steigt nicht mit der Aufrufzahl, Inhalte/nested Daten bleiben korrekt. Ein Alias auf alte RC-Felder überlebt Rebind, bis seine letzte Referenz droppt. Heap-Pointer-/Borrow-/RC-Modell-Kontrollen behalten ihren eigenen Release-Vertrag. Erst danach den neuen Regressionstest nach GREEN verschieben.

## Gemeinsame Integration und Definition of Done

Je Paket zuerst fokussierte positive/negative Tests und die passenden Probes ausführen, danach die relevanten vorhandenen Gates. Für die finalen integrierten Compileränderungen aus `aiml/`:

```sh
AIML_MEM_V3=1 AIML_LIFETIME_DROPS=1 bash tests/lifetime/run_lifetime.sh
bash tests/run_lifetime_leak_gates.sh
bash tests/run_memory_sanitizers.sh
bash run_aot_tests.sh
bash run_wasm_tests.sh
bash run_all_tests.sh --no-build
```

**Zusätzlich verpflichtend:** echter 9rooms-WASM-Client-Build und kombiniertes Browser-Verhaltens-/Speicher-Gate aus P1-R. Die obigen nativen und Node/WASM-Skripte decken dieses Gate nicht ab. Nicht ausführbare Clientprüfung bedeutet fehlende Abnahme für P1-B/C, keinen impliziten Erfolg.

Vor `--no-build` muss der Build tatsächlich die Änderungen enthalten. Den allgemeinen Korpus nur einmal gleichzeitig starten; keine parallelen Läufe mit gemeinsamen Testpfaden. Bestehende bekannte Fehler am gleichen Baseline-Stand bestätigen und getrennt ausweisen, statt neue Fehler damit pauschal zu entschuldigen. Memory-Sanitizer-Erfolg allein ist keine Leak-Abnahme: das harte Leak-Gate verlangt 0 definite + 0 indirect und keine Valgrind-Zugriffsfehler.

- Bei Return-Vertragsänderungen alle abhängigen AIML-Objekte/Module neu bauen. Keine Mischung von alten Borrow-Konventionen und neuen owned Return-Konventionen; physisch gleicher Pointer-/Struct-ABI bedeutet nicht kompatiblen Ownership-Vertrag. Buildcache-/Interface-Versionierung prüfen und nötigenfalls invalidieren.
- Laufende Game-/HTTP-/Media-Server nicht automatisch neu starten. Erst Compiler- und Testabnahme; Integration der Dienste als eigener Schritt.
- Pro Paket neue Logs mit Compiler-/Runtime-Hashes, Snapshot der Tests und Ergebnistabelle ablegen. Keine unbewiesene Übertragung historischer Leak-Zahlen auf den neuen Build.
- Erfolg bedeutet nicht nur 0 Byte: beobachtbare SHARE-Semantik, gültige Borrow-Lifetimes, keine Double-frees und korrekte Source-Gültigkeit sind Teil der Abnahme.

**Empfehlung:** P1-A mit Verhaltensabnahme zuerst liefern. P1-B und P1-C erst nach erfülltem P1-R und expliziter Spec-Präzisierung, dann jeweils beidseitig geschlossen. Plan 2 bleibt ein eigenständiges Folgepaket; seine typisierte Temporary-Verwaltung möglichst mit P1-C teilen, ohne die drei Round6-Fixes bis zur Deserialisierungsreparatur zu blockieren.


## Erneutes Review gegen Spec 61 — 2026-09-25

Gegen die vollständigen einschlägigen Besitzregeln geprüft; Spec-SHA-256: `0b5194fe5b0af1fa79fb64b9e82b35b0f547af60173406713d13f94daa43903a`. Code-Referenzstand `a281adb`. Die Spec wurde nicht verändert. Die oben stehenden Arbeitspakete enthalten die folgenden Korrekturen bereits.

| Reviewpunkt | Spec-Grundlage | Ergebnis / Änderung |
|---|---|---|
| Echtes Container-SHARE und Reihenfolge | §5, §5.5 | Konform; Slotlebensdauer während RHS-Auswertung präzisiert. Kein Move/DeepCopy einer benannten Class-3-Quelle. |
| SHARE bei move-only Elementen | §5.1, §7 | Test ergänzt: `list<ptr<T>>` bleibt sharebar. Container-Sharing dupliziert keine Elemente. |
| Besitz von `at/get`-Ergebnissen | §5.4, §5.7 | Präzisiert: Container-/String-Read erhält eine owning Referenz; vorhandenes Retain übernehmen, nicht doppeln. |
| Normale String-/Modell-Returns | §4, §7, §8, §9.2 | Owned Caller-Ergebnis ohne Producer ist korrekt. Producer bleibt zusätzliche Frischeverpflichtung. Bei Plain-Container-Calls P1-A-Abhängigkeit eingegrenzt. |
| Weiterreichen eines Modell-Temporaries | §6.1, §7, §8 | Cleanup des Zwischenwerts ausdrücklich getrennt nach CopyRetain und Adoption. |
| CopyRetain des Decode-Ergebnisses | §7, §8 | **Wesentliche Lücke korrigiert:** P2-B nur für kopierbare Modelle; P2-C für unterstützte move-only Modelle verpflichtend. |
| Rekursive Memberbehandlung | §2.1, §6.1, §7 | Class 5 atomar retainen/droppen; Class 6 und Borrow-Handles nicht besitzen. |
| Deserialisierungs-Träger und Ziel-Drop | §6.1, §7; konkrete Allokation im aktuellen Code | Interner malloc-Block ist Implementierungsdetail. CopyRetain- und Transfer-Cleanup getrennt; keine Quellfreigabe geliehener KV-Blöcke. |

**Zusätzliche Abnahmetests aus dem Review:**

- Nested-container `at/get` als RHS, als ignorierter Ausdruck und als weitergereichter Return: kein fehlendes oder doppeltes Retain.
- String-Return einer Interpolation, die intern nur einen bestehenden String weiterreicht, einschließlich leerem Literal/leerem Ergebnis.
- `return make_model()` mit owning Zwischenwert: nach Callee-/Caller-Cleanup kein Restgraph.
- Soweit vom Decoder unterstützt: Modell mit direkt owned `ptr`-Feld (MOVE notwendig) gegenüber Modell mit `list<ptr<T>>`-Feld (CopyRetain des Containers zulässig); beide ohne Doppelbesitz der Elemente.
- Negative Borrow-Return-Gegenprobe: `ref<Model>` auf einen Callee-local wird weiterhin abgelehnt (§6.2/§8). Ownership-Normalisierung darf diese Diagnose nicht umgehen.

**Grenze des Reviews:** Spec 61 legt die Speicherregeln fest, aber weder die konkrete Temporary-Datenstruktur noch sämtliche FFI-/JSON-Typ- und Fehlerverträge. Deren technische Ausgestaltung ist als Implementierungsentscheidung ausgewiesen und muss mit dem tatsächlichen Backend-/Builtin-Vertrag verifiziert werden. Der korrigierte Plan ist mit den geprüften Regeln vereinbar; diese Aussage ist keine Bestätigung einer bereits implementierten oder getesteten Lösung. Plan 2 bleibt zurückgestellt.


### Reviewkorrektur: Freigabestatus

Die vorangehende Aussage „mit den geprüften Regeln vereinbar“ bewertet die gewünschte Speichersemantik, nicht die Sicherheit des breiten Rollouts. Nach Einbeziehung des historischen Client-Reverts gilt: **P1-B/C zurückhalten bis P1-R erfüllt ist.** Die explizite plain-String-Spec-Regel und echte Client-/WASM-Verhaltens- plus Speicherabnahme sind offene Voraussetzungen. P1-A bleibt vorrangig, benötigt jedoch ebenfalls Verhaltensnachweise. Plan 2 bleibt unverändert zurückgestellt.


## Umsetzungsnachtrag 2026-09-25: P1-A

P1-A ist im C++-Code umgesetzt: benannte Container-RHS als Handle normalisiert, `Transfer::Share`, Quelle behält Scope-Drop, Retain-new vor rekursivem Release-old und Store. Der zusätzliche Map-/Buffer-Fall-through-Drop wurde an die registrierten Handle-Drops angeglichen. Legacy-Struct-Builtins behalten ihren bisherigen Pfad.

Neue Regressionstests: `tests/lifetime/61_member_handle_share.aiml` und `62_member_share_recursive_release.aiml`, beide im Leak-Gate und in der WASM-Testliste. Ursprünglicher List-Member-Leak: 40.000 → 0 B pro 1.000 Aufrufe. Erweiterter Test bewies außerdem 184.000 B Map-Local-Verlust im ersten SHARE-Zwischenstand; nach Korrektur ebenfalls 0. Neun harte Leak-Gates und sieben WASM-Tests bestanden. Vollständige Belege und Grenzen: [Probe-Bericht](../../9rooms_v2/leak_probes/round6_share_fix/README.md).

P1-B/P1-C bleiben offen; die Return-Probes sind weiterhin rot. Kein allgemeiner Klassifikator reaktiviert und kein neuer Return-Vertrag eingeführt. Der Nutzer verwendet `run_web.sh` mit Client-Heap-Logging; das ersetzt noch keinen bestätigten Navigationsvergleich. Round5-B/P2 bleibt zurückgestellt.
