# Spec 61 — Implementierungsplan für Container-Leaks und Speicherfehler

Stand: 2026-09-29. **Implementierungsplan; Regressionstests im Review ergänzt, Compiler-/Runtime-Fixes noch offen.** Gelesener AIML-HEAD: `be736b3`, mit bereits vorhandenen uncommitteten Änderungen. Maßgeblich für diesen Auftrag ist die aktuelle [Spec 61](../spec/61_memory_management.md), einschließlich ihres DRAFT-Status; der ältere v2-Entwurf mit genereller Modell-Referenzsemantik ist nicht die Grundlage.

## 1. Ziel und belegter Ausgangspunkt

Sechs Befunde aus der freien Leak-Suche schließen: vier Leak-Klassen sowie `list<string>.insert`-UAF und der Scope-Abbau-Crash einer Map mit verschachtelten Containerwerten. Danach müssen die betroffenen Operationen die Besitzregeln der Spec erfüllen, richtige Inhalte liefern und ohne verlorene Allokationen oder ungültige Speicherzugriffe enden.

Die Untersuchung nutzte den vorhandenen Compiler und `libaiml`, native AOT-Repros und Valgrind mit `AIML_MEM_V3=1`, `AIML_LIFETIME_DROPS=1`. Kein frischer Compilerbuild, kein WASM-Nachweis. Vor Implementierung daher P0 ausführen. Die angegebenen Bytezahlen sind historische Messungen auf x86_64, keine ABI-Vorgaben.

Artefakte der Untersuchung: `/tmp/aiml-leak-audit-20260929/REPORT.md`, `results.json`, `provenance.json`; pro Repro `main.aiml` und vollständiges `valgrind.log`. `/tmp` ist nicht dauerhaft: P0 übernimmt die Quellen und Befundbelege ins Projekt.

| ID | Befund / vorhandene Repros | Ergebnis bei 200 Durchläufen | Spec-Vertrag |
|---|---|---:|---|
| B1 | `map_nested_control`, `map_nested_self_put` | Invalid read + SIGSEGV beim Scope-Abbau | §5, §5.7: Werte sind Handles; konsistente Strides/Deskriptoren |
| B2 | `list_string_insert` | 200 Invalid reads beim Listenabbau | §4, §5.4: Insert besitzt eine eigene Stringreferenz |
| B3 | `map_shared_control` | 3.200 B direkt + 3.800 B indirekt verloren | §5, §6.2: Shared-Values unabhängig vom Keytyp decrefen |
| B4 | `map_nested_remove`, `map_model_remove` | 8.000 B bzw. 3.800 B insgesamt verloren | §5.4, §5.7, §7: Entfernen gibt den Wert nach seiner Klasse frei |
| B5 | `map_model_replace` | 3.400 B direkt verloren | §5.5, §7: Quelle sichern, altes Modell droppen, neuen Wert installieren |
| B6 | `list_pop_bound`, `list_pop_arg` | jeweils 3.490 B direkt verloren | §5.4, §8: Pop überträgt Besitz auf das Ergebnis |

Gegenproben: `map_model_control`, `map_string_control`, `map_shared_string_key_control`, `list_nested_control`, `list_pop_discard` waren 0 definite / 0 indirect und ohne Valgrind-Fehler. Die Map ohne `remove` ist wegen B1 ausdrücklich keine grüne Gegenprobe für B4. `list_nested_remove_value` wurde vom Compiler abgelehnt und zählt nicht als Leak.

## 2. Verbindliche Implementierungsregeln

1. **Typ bestimmt Besitz (§2.1, §4–§7).** Strings, Containerhandles und `shared<T>` werden geteilt; `ptr<T>` wird bewegt; `ref<T>` bleibt nicht-owning; Value-Modelle behalten Inline-Wertsemantik mit rekursiven Feldoperationen. Keine globale Umstellung von Modellen auf Referenzsemantik.
2. **Ein Ergebnis besitzt genau die Referenzen, die seine Freigabe später abgibt.** Benannte kopierbare Quellen behalten ihren Drop. Ein tatsächlich übernommenes owned Temporary verliert seine Cleanup-Pflicht genau einmal. `producer` bezeichnet zusätzliche Frische und ist nicht Voraussetzung für Ownership eines `pop`-Ergebnisses (§9.2).
3. **Secure-before-release (§5.5).** RHS einmal auswerten, neue Referenz sichern bzw. Movequelle zur Laufzeit leeren, dann altes Ziel freigeben, dann speichern. Slotidentität und gleiche Handlewerte in unterschiedlichen Slots unterscheiden (§5.7). Kein pauschales `oldHandle == newHandle → skip release`, wenn bereits eine neue Referenz übernommen wurde.
4. **Gemeinsame Slot-Klassifikation (§5.7).** Physischer LLVM-Speichertyp, Stride, Retain/Release und Serializer-Deskriptor stimmen überein. Containerwert ist ein Pointer-Slot, nicht der Inline-Header und nicht die Adresse eines Handle-Slots. Pointergröße aus LLVM DataLayout, niemals fest `8` für wasm32.
5. **Rekursive Freigabe (§6.1, §7).** Inline-Modell: Felder freigeben, nicht den Inline-Slot selbst. Shared-Objekt: decref. Unique-Box: Release der Felder und Shell. Weak-Ref: kein Release. Verschachtelter Container: decref mit korrektem innerem Deskriptor.
6. **Keine Kaschierung.** Kein Abschalten von Drops, keine zusätzlichen `persistent`-Annotationen, keine pauschalen Deep-Copies/`clone`, keine Valgrind-Suppression für diese Befunde. Stringkopien im berührten memV3-Store teilen nach §4; `str_dup` ist kein allgemeiner Ownership-Fix.

Die Arbeit schließt die sechs Befunde und die direkt gemeinsam verwendeten Store-/Release-Pfade. Sie eröffnet keine neue GC-/Zyklenarchitektur und ändert weder die Regeln für Container-Keys noch die Semantik von `get_copy`/`clone`. Legacy-OFF/v2-Pfade werden getrennt gehalten und dürfen nicht versehentlich das memV3-Layout erhalten.

## 3. Arbeitspakete und Reihenfolge

Empfohlene Folge: **P0 → P1 → P2 → P3 → P4 → P5 → P6**. P2 und der eng isolierte P3 hängen fachlich nur von P0 ab; die Reihenfolge priorisiert die Safety-Fehler. Gemeinsame Helper entstehen nur in der für diese Operationen benötigten Form, kein vorgezogener Komplettumbau der Compiler-IR.

### P0 — Baseline und dauerhafte Regressionen

**Dateien:** neue Probes in `tests/lifetime/`, `tests/run_lifetime_leak_gates.sh`, bei Bedarf `tests/run_memory_sanitizers.sh`; Befundquellen/-logs unter einem neuen `doc/analysis/container_leak_audit_2026-09-29/`-Verzeichnis.

- Compiler und native Runtime aus demselben aktuellen Quellstand bauen, dann die Repros erneut AOT kompilieren/linken. HEAD, Arbeitsbaum-Diff, Flags und Binary-Hashes gemeinsam aufzeichnen; historische Logs behalten.
- Alle sechs Befunde und die fünf sauberen Gegenproben übernehmen. Aussagekräftige Namen verwenden; Nummern erst bei Umsetzung nach Sichtung freier Plätze vergeben. Bereits vorhandene Probes 86/87 und ihre Änderungen erhalten.
- Jeden Repro um Inhalts-/Lebensdauerassertions ergänzen: Map nach Entfernung kürzer, nicht entfernte Werte intakt, neues Modell nach Ersatz sichtbar, Pop gibt den erwarteten Text zurück, Insert bewahrt Reihenfolge und Inhalt auch nach Ende des Quellscopes.
- Neue offene Tests zunächst in einem vom normalen Lifetime-Glob nicht erfassten Unterordner halten und im Leak-Gate explizit als RED melden. Mit dem jeweiligen Fix nach GREEN und in die reguläre Auswahl übernehmen. Ein grüner Gesamtstatus darf die noch offenen Befunde nicht als behoben ausweisen.
- Die derzeitigen GREEN-Fälle sowie den nativen Lifetime-/Safety-Stand festhalten. Fremde, vorher bestehende Fehler getrennt dokumentieren; keinen vorhandenen GREEN-Fall nach RED verschieben, um diese Arbeit durchzubekommen.

**P0 fertig:** Jeder gemeldete Fehler ist mit frisch gebautem Toolchain-Paar reproduziert oder seine Abweichung konkret erklärt. Dauerhafte Reproquellen und Vorher-Logs existieren. Keine Aussage über einen historischen Crash allein aus dem heutigen Code ableiten.

### P1 — B1: verschachtelte Map-Werte mit einheitlichem Layout

**Dateien/Funktionen:** `context.h` Typauflösung, `ast_compiler_call.cpp` Map-Dispatch, `ast_compiler_stmt.cpp::emitMapHandleDecl`, `map_impl.{h,cpp}`, `ast_compiler.cpp::elemHandleSerNodes` und dessen Deskriptor-Builder, `string_runtime.cpp::aiml_map_free`, bei nachgewiesenem Bedarf `ser_runtime.cpp`.

**Diagnoseschritt:** B1 ist als Crash bestätigt, die erste falsche Speicherung noch nicht abschließend lokalisiert. Für den Minimalfall die erzeugte IR und die Laufzeitwerte abgleichen: tatsächlicher Handle, Map-Value-Slot, gespeicherte Bytes, Stride, Root-Offset und Root-Kind des Deskriptors. Sowohl Header-statt-Handle-Speicherung als auch falscher Slot/Deskriptor sind zu prüfen. Nicht allein die spätere Fault-Stelle mit Nullchecks behandeln.

**Implementierung:**

1. Eine zentrale Klassifikation für Container-Element-/Value-Slots verwenden bzw. gezielt ergänzen. Vorgeschlagenes internes Ergebnis: `storageType`, `stride`, Besitzklasse und Release-/Retain-Metadaten. Das ist ein Schnittstellenentwurf, kein behaupteter bestehender Helper.
2. `map<K:list<U>>`, `map<K:map<X:Y>>` und `map<K:buffer>` allokieren und adressieren Pointer-Slots. `put/get/remove`, Wachstum, Decl-/Scope-Abbau und Serializer verwenden dieselbe Klassifikation. `MapImpl::put` darf die Slotgröße nicht unkontrolliert aus dem zufälligen LLVM-Typ der RHS ableiten.
3. Receiver-Headerpointer, Handlewert und Adresse eines Handle-Slots explizit auseinanderhalten. Ein Inline-Struct im memV3-Container-Value-Store ist ein Codegen-Vertragsfehler mit Diagnose, kein Anlass für einen stillen Bitcast/Fallback.
4. Deskriptor für einen Containerwert ist auf dessen Handle-Slot gerootet (`*_PTR`, passender Offset); die Runtime erhält die Adresse genau dieses Slots. Für Inline-Modelle bleibt der Modell-Deskriptor auf den Inline-Bytes gerootet.
5. Derselbe Vertrag gilt für native 64 Bit und wasm32. Gemeinsam mit dem Layout alle betroffenen Größen-/Stride-Konstanten ersetzen, ohne unbegründet die gesamte ABI umzubauen.

**P1-Abnahme:** einfache verschachtelte Map ohne Crash und ohne Leak; Self-put zunächst ohne ungültige Speicherzugriffe. Seine vollständige Referenzbilanz wird in P4 abgenommen. Inhalt nach `get`, mehrere Keys, mindestens zwei Wachstumsschritte; Alias bleibt nach Drop der äußeren Map gültig. `list`, `map`, `buffer` als Value, numerische und String-Keys. `copy/clone` und serialisierter Roundtrip als Regression für die berührten Layoutpfade. Falls der Self-put danach einen Referenzleck zeigt, P4 muss ihn schließen; B1-Safety und finaler Leakstatus getrennt berichten.

### P2 — B2: `list.insert` erwirbt Elementbesitz

**Dateien/Funktionen:** `ast_compiler_call.cpp` Insert-/Push-Dispatch, `list_impl.{h,cpp}::insert`, bestehende String-/Modell-/Container-Retain-Helfer und Ownership-Transferregistrierung.

1. Push und Insert auf denselben typgesteuerten Vorbereitungsschritt für ein einzufügendes Element führen. Der Primitive zum Schieben/Speichern bekommt einen bereits gesicherten Wert und dessen korrekten Speichertyp.
2. Benannter String: incref; owned String-Temporary: Referenz übernehmen und Temporary-Cleanup konsumieren. Literal/immortales Leerstring über den vorhandenen gültigen Stringvertrag behandeln. Keine neue direkte Header-Probe auf beliebigen Pointern einführen.
3. Dasselbe Prinzip bei bereits unterstützten Elementklassen sichern: Container-/Shared-Handles retain/adopt, kopierbare Modelle member-wise retain, echte Unique-Moves mit geleerter Quelle. Keine rohe Bytekopie eines nichttrivialen Modells als Ownership-Ersatz.
4. Quelle **vor** Realloc oder Verschieben sichern. Bei `insert(..., at(sameList,...))` kann der Quellslot ungültig werden; gesicherten Handle bzw. unabhängigen temporären Value-Slot verwenden. Provisorisch erworbener Besitz muss auch bei einem vorhandenen Fehler-/Ablehnungspfad aufgeräumt werden.

**Abnahme:** ursprünglicher UAF-Repro grün; lokale Quelle lebt länger/kürzer als Liste; Quelle danach lesbar; frische Interpolation, Literal und Pop-Ergebnis; Einfügen am Anfang/in der Mitte/am Ende, mit und ohne Wachstum, Quelle aus derselben Liste. Bei gemeinsam berührten Typen mindestens je eine Container-/Shared-/Value-Model-Probe sowie bestehende Unique-Move-Negativtests.

### P3 — B3: Map-Scope-Release unabhängig vom Keytyp

**Datei/Funktion:** `src/sys/string_runtime.cpp::aiml_map_free`.

- Äußeres Schleifengate korrigieren: alle unterstützten Value-Arten mit Release-Pflicht müssen den Durchlauf erreichen. Konkret fehlt heute `valKind == 3`, wenn `keyKind != 1`. Besser einen klaren `valueNeedsRelease`-Prädikat/Switch verwenden als Key-Ownership als Stellvertreter.
- Innere Shared-Release-Operation beibehalten: pro lebendem Slot genau ein `aiml_model_decref`; keine zusätzliche Freigabe beim noch vorhandenen Mitbesitzer.
- Nullwerte und leere Maps bleiben gültig; skalare Values lösen keinen Objekt-Release aus. Dense Iteration `[0,size)` bleibt erhalten.

**Abnahme:** Integer-, Enum- und String-Key mit `shared<Item>`; 200 und 400 Wiederholungen jeweils 0/0 B; mehrere Keys, Map-Wachstum, leere Map und noch lebender externer Shared-Besitzer. Quelle bleibt nach Map-Drop lesbar, letzter Besitzer gibt die Felder und Shell frei.

### P4 — B4/B5: typgerechtes Remove und sicheres Ersetzen

**Dateien/Funktionen:** `ast_compiler_call.cpp` Map-Dispatch, `map_impl.{h,cpp}::put/remove`, gemeinsame Slot-Klassifikation aus P1, bestehende rekursive Modell-Release-/Retain-Helfer; bei geändertem Moveverhalten Ownership-Analyzer und Cleanup-Verwaltung im selben Paket.

**Remove:**

1. Value-Ownership-/Release-Metadaten übergeben; `bool valIsString` reicht nicht. Für jeden gefundenen Eintrag den Key und Value nach ihrer Klasse freigeben. Der aktuelle Key-String-Release bleibt genau einmal bestehen.
2. Value freigeben **vor** Shift und Size-Reduktion. Durch `memmove` verschobene Einträge werden bewegt, nicht kopiert: weder incref noch zusätzlicher Drop der alten Byteposition. Den nun unbenutzten Tail-Slot bei owning Typen in leeren Zustand setzen, ohne ein zweites Release.
3. Fehlender Key ist No-op. Erster/mittlerer/letzter Eintrag werden symmetrisch behandelt. Falls der Such-Key in den zu löschenden Daten lebt, darf er nach Release nicht mehr verwendet werden.

**Put / Replace:**

1. Vor dem Update die Quelle nach Spec sichern: benannte kopierbare Werte teilen; owned Temporaries übernehmen; Unique-Moves Quelle zur Laufzeit leeren. Pauschales `consumeLocalDrop` für alle Nicht-Strings durch den tatsächlichen Transfervertrag ersetzen. **Append und Update im selben Paket** auf diesen Vertrag umstellen: übernimmt der Caller künftig SHARE statt MOVE, müssen beide Store-Zweige exakt die gesicherte Referenz erhalten; bloßes Release-old im Update reicht nicht. Keine Cleanup-Liste auf einer nur bedingt ausgeführten Route global ausräumen.
2. Im gefundenen Value-Slot den alten Wert mit genau derselben Klassifikation wie Remove/Scope-Abbau freigeben und erst danach den gesicherten neuen Wert speichern. Inline-Modelle rekursiv droppen; Shared-Values decrefen; Containerhandles decrefen. Den existierenden Key nicht neu übernehmen. Ein vor der Key-Suche gesicherter neuer Key-Temporary muss im Update-/Fehlerpfad regulär freigegeben werden; nur Append übernimmt den neuen Keybesitz.
3. Aktuelles String-Update ebenfalls auf secure-before-release ausrichten: es darf bei einer aliasenden RHS nicht erst den alten String freigeben und danach dessen Inhalt lesen. Nach §4 durch Retain/Adopt teilen, nicht durch Deep-Copy reparieren.
4. Pointergleichheit allein überspringt keine bereits fällige Besitzbilanz. Bereits gesicherte neue Referenz plus alte Slotreferenz: alte freigeben, neue installieren, auch bei gleichem Referenten. Nur **identischer Quell-/Zielslot** erlaubt den echten frühen No-op. Unterschiedliche Slots mit gleichem Handle und Move müssen Quellslot plus überschüssige Referenz korrekt abbauen.
5. Quell- und Zielslots nicht über RHS-Aufrufe hinweg halten, die dieselbe Map vergrößern können. Einen Value aus einem bestehenden Map-Slot vor einem potentiellen Realloc materialisieren/sichern; Zielposition nach mutierenden Ausdrücken gemäß bestehender Evaluationsreihenfolge neu auflösen.

**Abnahme:** ursprüngliche Modell-/Container-Remove- und Modell-Replace-Repros 0/0 B; wiederholtes Replace, Update nach Wachstum, gleiche/verschiedene Keys, gleiche/verschiedene Handlewerte, `put(k,get(k))`, eine Quelle aus einem anderen Slot derselben Map, Quelle aus einem Feld des alten Modells. Inhalts- und Aliasassertions sowie lebender Mitbesitzer nach Remove/Replace. Mindestens string, verschachtelter Container, shared, kopierbares Modell mit String+Container+Nested-Model-Feldern. Unterstützte Unique-/Weak-Value-Routen positiv/negativ gegen §6.2 prüfen; nicht unterstützte Typen dürfen nicht still als triviale Pointer durchfallen.

### P5 — B6: Pop-Ergebnisse als owned Resultate behandeln

**Dateien/Funktionen:** `ast_compiler_call.cpp` Pop-Dispatch und Call-Argumentpfade, `list_impl.cpp::pop`, `ast_compiler_stmt.cpp` Decl/Set/Return/Statement-Cleanup; bestehende Owned-Temporary-Verwaltung in Compiler/Context. Mit den uncommitteten Arbeiten an container-return/producers (Probes 86/87) abstimmen, keine zweite konkurrierende Temporary-Liste einführen.

1. `pop` entfernt den Slot und liefert dessen **bestehenden Besitz**: kein zusätzliches incref bei der Extraktion, kein decref des ausgegebenen Elements. Es ist keine geliehene `.get`-Rückgabe und benötigt keine `producer`-Namensheuristik.
2. String-Pop beim Erzeugen des Ergebnisses als owned Temporary registrieren. Für andere unterstützte nichttriviale Pop-Typen den passenden typisierten Ergebnis-/Cleanup-Vertrag verwenden; Aggregate brauchen bei adressbasiertem Drop einen stabilen temporären Slot.
3. `decl`/`set` übernimmt den owned String genau einmal, ohne Borrow-Retain; `set` gibt das alte Ziel nach §5.5 frei. Als gewöhnliches Call-Argument bleibt das Temporary bis zum Ende seiner Nutzung gültig und wird danach freigegeben; zusätzliche Parameter-/Callee-Referenzen müssen separat ausgeglichen sein. Ein konsumierender Parameter übernimmt den Besitz.
4. Weitergabe an Return, Push, Insert oder Map-Put übernimmt bzw. teilt nach Zielvertrag. Genau ein Cleanup ist zu konsumieren; keine vorzeitige Freigabe innerhalb verschachtelter Ausdrücke. Ein Ausdruck, der zwei Pops enthält, erzeugt zwei getrennte Besitzpflichten.
5. Direkt verworfenes Pop entweder durch denselben Resultat-Drop oder den bestehenden `aiml_list_pop_free`-Spezialpfad behandeln, niemals beides. Bedingungen, Return, `break`/`continue` und vorzeitige Blockausgänge dürfen Temporary-Cleanup nicht überspringen.
6. Vorhandenes Verhalten bei leerer Liste gesondert absichern; dieser Leak-Fix erfindet keine neue Empty-Pop-Semantik. Ein ungültiger Pop darf nicht als grüne Leakprobe durchgehen.

**Abnahme:** gebunden, Rebind, Argument einer freien Funktion/einer Methode, direkt verworfen, Return-Weitergabe, erneutes Push/Insert, Interpolation und verschachtelter Call; 200/400 Iterationen mit korrektem Text und 0/0 B. Container-/Shared-/Value-Model-/Unique-Pop gezielt prüfen, soweit vom Compiler unterstützt, damit die neue Klassifikation keinen String-Sondervertrag für alle Typen erzeugt.

### P6 — Integration und endgültige Abnahme

**Verfügbare Runner (vom AIML-Projektroot):**

```bash
AIML_MEM_V3=1 AIML_LIFETIME_DROPS=1 bash tests/run_lifetime_leak_gates.sh
AIML_MEM_V3=1 AIML_LIFETIME_DROPS=1 bash tests/run_memory_sanitizers.sh
MODE=spec61 bash tests/lifetime/run_lifetime.sh
```

Gezielte neue Probes außerdem über `run_wasm_tests.sh` mit expliziter Dateiliste und frisch gebauter passender wasm32-Runtime ausführen. Der Runner existiert; eine native Valgrind-Messung ersetzt weder WASM-Ausführung noch dessen gesonderte Leak-Messung. Die native AOT-Abnahme und JIT-Lifetime-Ausführung sind beide nötig.

- Alle sechs Befunde müssen feste GREEN-Regressionen werden. Der gezielte strikte Runner `python3 tests/lifetime/container_leaks/run.py` muss Exit 0 liefern. Das bestehende Leak-Gate meldet RED-Fälle nur; dessen Exit 0 allein genügt daher nicht als Abnahme dieser Arbeit. Je Paket zuerst gezielte Probes; nach Integration die vorhandenen GREEN-Leakfälle und Safety-Suite vollständig. Den zum Umsetzungszeitpunkt vorhandenen Status fremder Fälle erhalten. Im Review stehen 86/87 bereits in GREEN; deren unveränderte Tests weiter absichern. Der neue Fall 88 ist fremde laufende Arbeit. Keine Rückstufung oder Überschreibung dieser Änderungen.
- Pro Leakprobe: nativer Exit 0 plus Inhaltsassertions, **0 definite / 0 indirect**, kein Invalid read/write/free und vollständiger Valgrind-Lauf. Build-/Linkfehler, Signalabbruch, Timeout oder fehlendes Werkzeug/Log sind kein PASS. Keine abweichenden Leak-Schwellen wegen der historischen Bytezahlen.
- Bei 200 und 400 Durchläufen weiterhin 0/0 B. Zusätzlich einen längeren wiederholten Ablauf mit begrenztem Bestand nach Warm-up prüfen, um dauerhaft erreichbare, aber unkontrolliert wachsende Referenzen zu entdecken. RSS allein ist kein eindeutiger Leakbeweis; Live-Allokationen nach abgeschlossenem Besitzerabbau verwenden.
- WASM: richtige Inhalte, Alias-Semantik, Scope-Enden und Wachstum für die neuen Fälle. Pointer-Strides/Deskriptoren explizit in wasm32 prüfen. Wenn die Umgebung fehlt, als ausstehende Plattformabnahme ausweisen.
- Wegen der gemeinsamen Compilerpfade abschließend betroffene 9rooms-Szenarien (Mana-Map, Event-Log-Liste, wiederholte Modell-/Containerupdates) mit den tatsächlichen Deployment-Flags prüfen; konkreten App-/Browser-Runner bei Umsetzung ermitteln. Keine Browserabnahme allein aus einem Node-Test ableiten.
- Änderungen an Runtime-Signaturen synchron in nativer und WASM-Registrierung/Exports durchführen. Keine neu gebauten Compiler gegen eine alte Runtime testen oder ausliefern.

## 4. Fertig-Kriterium und geplante Liefergegenstände

- [ ] P0: dauerhafte Repros, frische Baseline und unveränderte historische Belege.
- [ ] P1: erster falscher B1-Layout-/Deskriptorpfad belegt und korrigiert; native/WASM-Stride-Regeln konsistent.
- [ ] P2: Insert sichert eigenen Elementbesitz vor Growth/Shift.
- [ ] P3: Shared-Map-Abbau für alle erlaubten Keytypen korrekt.
- [ ] P4: Remove und Replace geben alle unterstützten Value-Arten richtig frei, einschließlich Self-Alias.
- [ ] P5: Pop-Ergebnisse werden übernommen oder genau einmal gedroppt, auf allen getesteten Ausgängen.
- [ ] P6: neue Fälle GREEN; bisherige GREEN-Fälle ohne Regression; Plattform-/Integrationsstatus belegt.

Lieferung: kleine zusammenhängende Compiler-/Runtime-Änderungen pro Paket, dauerhafte Repros mit Inhaltsprüfungen, registrierte Leak-/Safety-Gates und ein abschließender Vorher-/Nachher-Bericht. Paketstatus erst dann abhaken, wenn seine Tests ausgeführt wurden. Kein Anspruch, damit alle Speicherfehler der Sprache oder alle noch offenen Spec-61-Migrationspunkte zu lösen.


## 5. Review vom 2026-09-29 und vorhandene Beweistests

**Review-Korrekturen:**

1. **P1/P4-Abhängigkeit:** Die ursprüngliche P1-Abnahme verlangte bereits vollständige Self-put-Leakfreiheit, obwohl P4 erst die Besitzbilanz beim Ersetzen korrigiert. P1 beseitigt die Layout-/Safety-Ursache; der Self-put-Fall wird erst nach P4 endgültig GREEN.
2. **Gate-Aussage:** Der vorhandene Runner meldet RED, gibt dafür aber keinen Fehlerstatus zurück. Der ergänzte gezielte Runner prüft ausschließlich den Zielvertrag und endet bei jedem Leak, Safety-/Build-/Assertionfehler ungleich 0. Historische Fehler sind Metadaten, keine erlaubten XFAILs. Vollständige Logs bleiben erhalten.
3. **Store-Vertrag vollständig:** Änderungen am pauschalen Move-Unterdrücken betreffen Append UND Update sowie nicht übernommene Key-Temporaries. Beide Zweige müssen gemeinsam auf denselben Erwerb-/Transfervertrag umgestellt werden.
4. **Status fremder Arbeit:** 86/87 sind inzwischen GREEN, 88 wurde unabhängig neu angelegt. Diese Tests und Änderungen bleiben erhalten; der ältere RED-Verweis wurde korrigiert.
5. **Abdeckung ist nicht Gesamtmigration:** Die neuen Minimaltests beweisen die sechs ursprünglichen Befunde und sichern Kontrollen; zusätzlich entstand B7 (Modell-Get-Borrow-Crash). Die größere Alias-/WASM-/Integrationstestmatrix in P1–P6 bleibt Teil der späteren Implementierung. P0 insgesamt bleibt bis zur vollständigen bestehenden Suite-Baseline offen.

Dauerhafte Quellen: [Container-Leak-Tests](../tests/lifetime/container_leaks/README.md).
Gemessene Ergebnisse und Binary-Hashes: [Review-Befunde](analysis/container_leak_audit_2026-09-29/README.md).
Die neuen Quellen enthalten Inhalts-/Größenassertions. Leakfreiheit wird separat von Valgrind geprüft; ein normales Exit 0 beweist sie ausdrücklich nicht. Der Defaultlauf testet N=200 und N=400 mit konstanten String-Payloads, damit sich verlorene Bytes ohne Änderung der Stringlänge vergleichen lassen.


### Review-Nachtrag B7: Map-Modell-Get als Borrow

Die ergänzte Feld-Leseassertion `decl [view, ref<Item>, {call: [index.get, "key"]}]; view.text` crasht bereits nach einmaligem Put, ohne Remove/Replace. Valgrind meldet in `strcmp` eine Adresse aus den Textbytes (`payload=...`), kein Leak. Dauerhafter isolierter Test: `map_model_get_borrow.aiml`. Die ursprünglichen B5-/Kontrolltests behalten nur ihre Größenassertion, damit der frühere Get-Crash den Leak-Nachweis nicht verhindert.

**P1b, vor vollständiger P4-Inhaltsabnahme:** Nach §5.4 muss der Zugriff auf einen Value-Model-Slot eine Borrow-View liefern; die Bindung an `Model` materialisiert per §7, die Bindung an `ref<Model>` behält die Slotadresse. `ast_compiler_call.cpp` Map-get-Dispatch und `map_impl.cpp::get` darauf prüfen, ob ein by-value Struct-Load als Slotadresse verwendet wird. Die genaue Korrektur mit erzeugter IR belegen, nicht pauschal für skalare/String-/Container-Get-Ergebnisse denselben Rückgabetyp ändern. Regressionen: ref-Bindung + Feldlesen, Value-Bindung + materialisierter Besitzer, unveränderte skalare und owning Handle-Reads; Borrow nicht über invaliderende Map-Mutationen halten. B7 wird separat nach GREEN überführt. Es ist eine zusätzliche Safety-Voraussetzung für die im Plan geforderten Modell-Inhaltsassertions, kein Nachweis weiterer verlorener Bytes.
