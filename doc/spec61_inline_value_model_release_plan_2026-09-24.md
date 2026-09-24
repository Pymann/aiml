# Implementierungsplan: sichere Freigabe von inline list<Model>-Elementen

Stand: 24.09.2026. Status: Plan, keine Implementierung. Zielkonfiguration: `AIML_MEM_V3=1`, `AIML_LIFETIME_DROPS=1`. Quellstellen beziehen sich auf den beim Erstellen gelesenen Arbeitsstand, nicht ausschließlich auf HEAD.

## 1. Entscheidung nach aktueller Spec

Die Formulierung „braucht Escape-Analyse (Views deep-copyen)“ beschreibt den historischen Workaround, aber nicht mehr den korrekten Zielvertrag. Spec 61 §5.4, Zeilen 749–764, verlangt ausdrücklich:

- Bindung an `Model`: eigene inline Value-Kopie, deren string/container/shared-Felder retained werden. Keine rekursive Duplikation der Container. Die Kopie überlebt Entfernung und Reallokation des äußeren Listen-Slots.
- Bindung an `ref<Model>`: echte nicht besitzende View auf den Slot. Nach dessen Freigabe oder einer relevanten Verschiebung/Reallokation darf sie nicht mehr benutzt werden.
- Move-only Model (ptr/unique-resource-Feld, auch transitiv): keine implizite Value-Kopie. Borrow, expliziter Clone soweit klonbar oder besitzende Extraktion verwenden.
- `remove`/`clear`: Elementbesitz freigeben. `pop`/`take`: Besitz in das Ergebnis verschieben; ein verworfenes Ergebnis am Ausdrucksende freigeben.

**Empfehlung:** Ownership bei der Value-Materialisierung richtig herstellen und Borrows getrennt analysieren. Kein automatisches Deep-Clone von `ref<Model>` vor einer Mutation: Das würde die Bedeutung einer expliziten View ändern. Tiefe Unabhängigkeit bleibt `clone` vorbehalten.

Damit muss keine vollständige interprozedurale Escape-Analyse fertig sein, bevor eindeutig besitzende inline Elemente sicher freigegeben werden können. Lokal erkennbare ungültige Borrows brauchen Diagnosen; unbekannte externe Effekte sind eine ausdrücklich zu behandelnde Analysegrenze, kein Freibrief für automatische Kopien.

## 2. Aktuelle C++-Ansatzpunkte

| Datei / Stelle | Befund / geplante Änderung |
|---|---|
| `src/core/context.h:58–65` | `Symbol::is_view_alias` ist ein syntaktisches bool, ohne Owner/Invalidierungszustand. Nicht als alleinige Ownership-Quelle weiterverwenden. |
| `src/core/ownership_flow.h` | `OwnFact`, `Origin`, `Transfer`, `OwnershipPlan` vorhanden. Value-Materialisierung und Borrow-Provenienz hier ausdrücklich abbilden. |
| `src/core/ownership_analyzer.cpp` | Flow-Analyse erweitern; Alias-Ketten, Verzweigungen und Schleifen berücksichtigen. |
| `src/core/ast_compiler_stmt.cpp:1302–1332` | Widersprüchliche alte Kommentare und View-Markierung bei Value-Deklarationen. Durch typgesteuerte Materialisierung ersetzen. |
| `src/core/ast_compiler.cpp:6004ff` | `emitLValueAddress`: Adresse inline gespeicherter Elemente; Grundlage für echte `ref<Model>` und Copy-Quelle. |
| `src/core/ast_compiler_stmt.cpp:2237ff,2382ff,3554ff,4570ff` | Alte Deep-Copy-Sonderpfade für set/return/idx gegen aktuellen Share-Vertrag konsolidieren. |
| `src/core/ast_compiler_stmt.cpp:4790–4827` | `store` besitzt bereits bit-copy + `aiml_ser_retain`; Reihenfolge derzeit release vor copy/retain, daher auch auf Unterobjekt-/indirektes Aliasing prüfen. |
| `src/core/ast_compiler_call.cpp:1545ff` | Value-Model push: dieselbe Copy-/Move-Klassifikation einsetzen; keine pauschale Deep-Copy-Entscheidung. |
| `src/core/ast_compiler_call.cpp:1850–1861` | Expliziter Deferred-Kommentar bei remove; clear ohne Elementtyp-Freigabe. Erst nach gesicherten Value-Kopien aktivieren. |
| `src/core/list_impl.cpp:129,240,327` | pop, clear, remove: rohe Speicheroperationen von Ownership-Aktionen trennen. |
| `src/sys/ser_runtime.cpp:802–844`, `ser_runtime.h:140` | `aiml_ser_retain` existiert. Als Basis für member-wise Retain verwenden, Typabdeckung prüfen. |

Zeilennummern sind Navigationshilfen; Funktionsnamen bleiben die maßgeblichen Anker. Der vorhandene Deferred-Kommentar ist keine verbindliche Designentscheidung gegenüber der neueren Spec.

## 3. Gemeinsame C++-Primitive zuerst

In `ast_compiler.h` deklarieren, im passenden Compiler-Implementierungsmodul definieren. Namen unten sind Vorschläge, keine bestehenden APIs:

```cpp
enum class ValueTransfer { CopyRetain, Move, ExplicitClone };

// Erzeugt einen gesicherten besitzenden Wert in einer temporären Storage.
// Prüft Copy-Fähigkeit; wertet die Quelle genau einmal aus.
llvm::Value* prepareOwnedModelValue(Expr* source,
                                   const std::string& modelType,
                                   ValueTransfer transfer);

// Gibt Felder frei, niemals den inline Model-Slot selbst.
void emitValueModelRelease(llvm::Value* address,
                           const std::string& modelType);

// Veröffentlicht bereits gesicherten Besitz am Ziel; kein weiteres retain.
void commitOwnedModelValue(llvm::Value* destination,
                          llvm::Value* prepared,
                          const std::string& modelType);
```

CopyRetain-Lowering, schematisch:

```cpp
// src ist noch gültig; tmp besitzt keine vorherigen Ressourcen.
memcpy(tmp, src, sizeof(Model));
aiml_ser_retain(tmp, modelNodes, nodeCount);
// Erst nach dem Retain darf alter Zielbesitz verschwinden:
aiml_ser_free_decoded(dst, modelNodes, nodeCount);
memcpy(dst, tmp, sizeof(Model));
// tmp wurde nach dst verschoben: kein zweiter Drop.
```

Bei Initialisierung entfällt release(dst); bei exakter Copy-Selbstzuweisung darf die gesamte Operation entfallen. Bei überlappenden/indirekt aliasierenden Quellen bleibt die temporäre Sicherung notwendig. Keine Quelladresse nach möglichem Ziel-Drop oder Listen-Realloc erneut dereferenzieren. Move braucht denselben Schutz sowie rechtzeitiges Invalidieren der Quell-Ownership; Transfer aus einer nur geborgten View ist verboten. Auch Move-Selbstzuweisung und der Fall, dass die Quelle in einem freizugebenden Zielunterobjekt liegt, erhalten eigene Tests.

`aiml_ser_retain` ist vor Benutzung über alle zulässigen Felder zu auditieren: strings/path, Container-Handles, shared, Class-5-Ressourcen, eingebettete Value-Modelle und Arrays. `ref` bleibt schwach. Unique-Felder werden nicht durch Retain kopierbar. Fehlende Deskriptoren oder nicht unterstützte Eigentumsklassen dürfen nicht still auf flaches memcpy zurückfallen. Gegebenenfalls typspezifische Retain/Release-Emission ergänzen; Serialisierung allein ist kein vollständiger Lifecycle-Vertrag für OS/GPU-Ressourcen.

## 4. Value-Materialisierung vereinheitlichen

In dieser Reihenfolge auf die gemeinsamen Primitive umstellen:

1. `decl: [copy, Model, at/get(...)]` und Kopie aus einem anderen Value-Local: CopyRetain, eigenes OwnershipId, normaler Scope-Drop.
2. set, Member-Assignment, `store` in einen Out-Parameter und idx-Ersatz: Quelle sichern, alten Besitz releasen, neuen Besitz übernehmen.
3. Return-by-value: geborgene Quelle materialisieren, eigene Quelle gemäß Transferplan bewegen. Callee liefert genau einen Besitz; Caller adoptiert ihn. Keine zusätzlichen unbalancierten retains.
4. push/insert/extend/slice und weitere elementkopierende Operationen: CopyRetain oder Move nach Typ und Transferplan. Bei Copy vor einem möglichen Realloc der Quellliste sichern (`list.push(at(list,i))`).
5. Temporäre Ergebnisse: einheitliche Registrierung und Konsumierung. `pop`-Ergebnis darf auch bei Call-Verschachtelung oder Verwerfen weder verschwinden noch doppelt gedroppt werden.

Semantiktests müssen unterscheiden: `copy.cards.push(...)` verändert den gemeinsam gehaltenen Container; `copy.scalar = ...` oder das Ersetzen von `copy.cards` verändert nur die Kopie. Wer einen inline Slot selbst verändern will, bindet `ref<Model>`. Spielcode, der bisher von unbeabsichtigtem Scalar-Write-through einer Value-Kopie abhing, muss gezielt angepasst werden.

## 5. Ownership- und Borrow-Analyse

`Transfer` um eine explizite Retain-Copy-Aktion erweitern; DeepCopy für explizite Clone-Semantik getrennt halten. `Origin::SharedView` darf nicht als Ersatz für einen materialisierten besitzenden Value dienen. Ressourcenidentität geteilter Felder von Besitz des äußeren inline Aggregats unterscheiden: zwei Aggregate können dieselben Container-Felder jeweils mit eigenem rc halten.

Für echte Borrows Provenienz vorsehen, etwa:

```cpp
struct BorrowOrigin {
    std::set<ResourceId> ownerResources;
    // Zusätzlich Element-/Memberpfad und InlineSlot vs Pointee.
};
// An Ownership-Fakten/Expressions anbinden, nicht bloß an Variablennamen.
```

Datenfluss muss unterstützen:

- Alias-Ketten und Zuweisungen propagieren; Materialisierung trennt Slot-Borrow von besitzender Kopie.
- Beim CFG-Merge Owner-Mengen vereinigen; nach Mutation auf nur einem Pfad nicht unconditionally valid annehmen. Schleifen bis Fixpunkt analysieren.
- Bei inline Elementen remove/insert (Shift), clear, Ersetzen des betroffenen Slots, Owner-Drop und mögliche Reallokation modellieren. Shared Container-Aliase referenzieren denselben Owner; Mutation über Alias invalidiert ebenfalls.
- Nur spätere Benutzung eines invalidierten Borrows beanstanden; ein bereits unbenutzter Borrow darf remove nicht pauschal verbieten.
- Call-Effekte typisiert zusammenfassen: may-reallocate/may-remove/may-retain, Return-Borrow-Anker. Bei unbekannten Effekten keine falsche Sicherheitsgarantie; bestehende Spec-Diagnostik/Migrationsregeln beachten und verbleibende Grenzen dokumentieren.
- Borrow-Return nur bei ausreichend langlebigem Anker; Borrow auf callee-local oder lokales temporäres Copy-Ergebnis ablehnen.

Kein verzögertes automatisches Deep-Copy bei „Escape“ eines expliziten ref: entweder vom Zielvertrag erlaubte Value-Materialisierung oder Diagnose. Dadurch bleibt die Bedeutung von Mutation durch eine View erhalten.

## 6. Elementfreigabe aktivieren

Erst nachdem Materialisierungs-/Transferpfade unter Tests bestehen:

- **remove(i):** Argumente genau einmal auswerten, bestehenden Bounds-Vertrag vor Speicherzugriff durchsetzen, entferntes Model member-wise releasen, Rest mit Struct-Stride memmove verschieben, len vermindern. Letzte nun inaktive Slotkopie nicht nochmals releasen; optional ohne Drop nullen. Kein free(slot).
- **clear():** alle aktiven Slots genau einmal releasen, danach len=0. Speicherreserve darf bestehen bleiben. Zweites clear ist wirkungslos.
- **pop()/take():** Inline-Wert in besitzendes Ergebnis verschieben, Slot aus dem aktiven Bereich entfernen, keine Elementfreigabe im Transferpfad. Verworfene Ergebnisse anschließend durch temporären Drop releasen.
- **idx/replace:** vorbereitete Quelle vor Release des alten Slots; dieselben gemeinsamen Primitive wie set/store. Keine erneute Deep-Copy als Sonderfall.
- **finaler List-Drop:** aktive Elemente genau einmal releasen; bereits entfernte Slots bleiben unsichtbar. Vorhandene Runtime-Deskriptoren mit denselben Klassenregeln abgleichen.

Entscheidung nach Review (§9.7): Der Runtime-Dispatcher `free_list_elem_at` wird für remove/clear/pop-discard/final-drop vereinheitlicht; ListImpl/Codegen übergibt Typklassifikation und steuert Copy-/Move-Transfers. Keinen zweiten unabhängigen Release-Walker für remove, clear und finalen Drop etablieren. Beide möglichen Schichten nicht gleichzeitig droppen lassen.

## 7. Tests vor Produktivumschaltung

Neue Tests unter `aiml/tests/lifetime/`, freie Nummern bei Umsetzung bestimmen. Reale Client-Reproduktionen weiterhin unter `9rooms_v2/leak_probes/`.

| Testfall | Erwartung |
|---|---|
| Value aus at/get binden, Ursprung remove/clear/drop, Kopie lesen | gültige Inhalte; beide Drops balanciert |
| Kopie enthält string, list, map, nested Model, array | rekursives Retain/Release korrekt |
| Mutation gemeinsamer Container-Felder, Scalar-/Feldersatz getrennt prüfen | Share-Semantik und unabhängiger äußerer Value |
| Mehrere Kopien, Alias-Kette, Branch-Merge, Loop | kein Leak/UAF, Drop auf jedem tatsächlich besitzenden Pfad |
| ref aus at, inline remove/insert/realloc, danach ref benutzen | negative Diagnose bei lokal sichtbarer Invalidierung |
| remove über zweiten Listen-Handle-Alias | gleiche Invalidierung wie über ursprünglichen Handle |
| Value überlebt Wachstum, echter ref wird nicht benutzt | positive Kontrolle gegen zu strenge Borrow-Diagnose |
| remove erstes/mittleres/letztes Element, clear zweimal | survivors korrekt, keine doppelte Freigabe |
| pop gebunden, verworfen, an Funktion weitergereicht | genau ein übertragener Besitz / ein temporärer Drop |
| idx/store Selbstzuweisung und überlappende Quelle | secure-before-release, keine zerstörte Quelle |
| push(at(sameList,i)) mit erzwungenem Wachstum | Sicherung vor Realloc, valider Inhalt |
| Move-only Model kopieren, auch verschachtelt | Compilefehler; ref/pop erlaubt |
| Return und Out-Param über Modulgrenze | Caller/Callee zählen Besitz konsistent |

Vorhandene Tests zwingend einbeziehen: `tests/lifetime/45_store_view_outparam.aiml` (prüft explizit Aliasing), `37_nested_replace_remove.aiml`, `neg/n08_invalidated_borrow.aiml`, `neg/n13_move_only_value_copy_from_at.aiml`.

9rooms-Regressionsfälle: `test/unit/test_effect_processor.aiml`, `test_effect_death_on_destroy.aiml`, `test_mana_pick.aiml` und `test/integration/test_enchantment_picker.aiml`. Diese sichern gerade die zuvor durch Deep-Copy veränderte Spiellogik ab.

Native Assertion-Läufe plus Valgrind unter beiden aktivierten Flags. Kleinprobe und 1000 Wiederholungen vergleichen: 0 definite/indirect Verluste im isolierten Zielpfad, keine invalid reads/writes/free. Erreichbare Runtime-Registries separat behandeln. Das bestehende `tests/run_memory_sanitizers.sh` nutzt `--errors-for-leak-kinds=none`; dessen Safety-PASS genügt ausdrücklich nicht als Leak-Abnahme. Zusätzlich Leak-Zahlen als harte Gate-Bedingung prüfen. Anschließend WASM-Compile/Client-Smoke für Struct-Layout/Stride und die volle relevante AIML-/9rooms-Suite.

## 8. Umsetzungspakete und Abnahme

1. **Vertrag + rote Tests:** aktuelle Spec-Materialisierung als Autorität dokumentieren; Tests für Share-Semantik, remove/clear und temporäre pop-Drops erstellen. Bestehende Deferred-Kommentare kennzeichnen.
2. **Gemeinsame Retain/Release/Move-Primitive:** Runtime-Typabdeckung und Alias-Reihenfolge testen.
3. **Value-Ownership konsolidieren:** decl/set/store/return/idx und Container-Copy-Sinks umstellen; alte syntaktische View-Heuristiken für Value-Kopien entfernen. OwnershipPlan und Codegen müssen denselben Vertrag verwenden.
4. **Borrow-Analyse:** echte ref-Provenienz, Mutationseffekte und negative Tests. Keine unbekannten Fälle durch versteckte Deep-Copies überdecken.
5. **remove/clear/pop-Lifecycle einschalten:** erst jetzt Deferred-Lücke schließen; Leak-Gate plus bisherige Double-Free-Regressionsfälle müssen bestehen.
6. **Gesamtabnahme:** relevante AIML-/9rooms-Tests, Valgrind, WASM; Kommentare/Spec-Implementierungsnotizen aktualisieren. Keine breitflächige Drop-Unterdrückung als „Fix“ akzeptieren.

Der sicherheitskritische Abhängigkeitspfad ist 1 → 2 → 3 → 5. Borrow-Analyse läuft als eigenes Paket mit, und lokal bekannte ungültige Borrows müssen spätestens zur Gesamtabnahme diagnostiziert werden. Der Umfang ist mittel bis groß, weil dieselbe Value-Ownership über alle Erzeuger und Senken gelten muss; ein isolierter free-Aufruf im remove-Zweig ist nicht ausreichend.

---

## 9. Offene Umsetzungsfragen (Claude, 2026-09-24 — zum Review/Nachtragen)

Ich habe den Plan gegen den aktuellen Code gelesen. Unten die Stellen, an denen ich **unsicher bin, WIE oder WO** zu implementieren ist — bitte reviewen und die Design-Entscheidung eintragen. Legende: 🔴 = Kernrisiko / Entscheidung zwingend vor Umsetzung, 🟡 = konkrete Codestelle/Verfahren unklar. Verifiziertes Positivum vorab: **`aiml_ser_retain`/`ret()` [`src/sys/ser_runtime.cpp:805–847`] deckt STRING/STRUCT(rekursiv)/LIST_PTR/MAP_PTR/BUFFER_PTR/MODEL/PTR_REF(shared) bereits korrekt ab; unique ptr/tensor werden korrekt NICHT increft.** Paket 2 ist damit größtenteils vorhanden.

### 9.1 🔴 View-in-place-Mutation ist die eigentliche Hürde — nicht das Retain
Der AKTUELLE Vertrag [`src/core/ast_compiler_stmt.cpp:1355–1377`]: `decl x = Model {at/get/first/last}` setzt `sym.is_view_alias = true` = **geteilte VIEW**, und `x = at(..); apply_buff(x)` **mutiert die QUELLE**. Der Kommentar bei 1355–1358 sagt ausdrücklich, dass Deep-Copy-at-decl bereits **zurückgenommen** wurde, weil 9rooms (enchantment_picker, effect_processor) genau auf diese In-place-View-Mutation baut. CopyRetain (§4) macht `x` zu einer eigenständigen co-owned Kopie → `apply_buff(x)` mutiert dann die KOPIE, nicht die Quelle.
> ⚠️ ENTSCHEIDUNG NÖTIG: Werden alle „`decl m = Model at(..)` + danach `mutate(m)`"-Stellen verbindlich auf `ref<Model>` migriert (Plan §4 sagt das, aber es ist eine beobachtbare Verhaltensänderung im ganzen Spiel)? Wenn ja, brauche ich (a) deine Bestätigung des neuen Vertrags und (b) am besten eine kuratierte Liste der betroffenen Stellen (mein grep-Startpunkt: `decl:` mit Value-Model-Typ + AtExpr/`.get`/`.first`/`.last`-Init, danach ein Aufruf/`set` auf dieselbe Variable). **Das ist die risikoreichste Änderung — exakt die Klasse „breiter ABI-Eingriff", die beim String-Auto-Detect zu einer In-Game-Regression führte, die Leak/Sanitizer NICHT gefangen haben.**


**Antwort / Entscheidung (Codex, 24.09.2026):** Der Zielvertrag aus der aktuellen Spec bleibt verbindlich: `Model` materialisiert einen eigenen Value, `ref<Model>` bindet den Slot. Eine pauschale Migration aller mutierenden Aufrufe auf ref ist jedoch **nicht** nötig. Die Annahme „CopyRetain mutiert nur die Kopie“ gilt für Scalar-/Feldersatz, nicht für Mutation des über ein Feld gemeinsam gehaltenen Containers. Genau dieser Unterschied erklärt, warum ein früherer Deep-Copy-Versuch regressieren konnte, ohne dass CopyRetain dieselbe Regression verursachen muss.

Drei Fälle getrennt behandeln:

1. Containerinhalt ändern (`m.buffs.push`, `m.pool.put`): CopyRetain erhält die gemeinsame Sichtbarkeit; Value-Bindung kann bleiben.
2. Scalar-/Stringfeld setzen oder Containerfeld neu binden, anschließend `idx`-Writeback: Value-Bindung kann ebenfalls bleiben. Writeback schreibt die geänderten Felder zurück; Quelle davor sichern. Voraussetzung: Zielindex bezeichnet noch den beabsichtigten Eintrag.
3. Scalar-/Feldersatz soll ohne Writeback sofort den ursprünglichen Slot ändern: auf `ref<Model>` migrieren und die gesamte Benutzungsdauer auf Shift/Realloc/remove prüfen. Bei struktureller Mutation stattdessen Value behalten und über stabile Identität erneut lokalisieren; keinen Borrow darüber hinweg halten.

**Kuratierte, am Quelltext geprüfte Startliste** (Pfade relativ zu `9rooms_v2`, keine behauptete vollständige Migrationsliste):

| Stelle | Beobachtung / Entscheidung |
|---|---|
| `sources/cards/effect_processor_base.aiml:547–585`, `buff_zone` | `ent = at(zone,idx)`, `add_buff(ent,buff)`, danach `idx(zone,idx,ent)`. Value + CopyRetain beibehalten; vorhandener Writeback deckt Scalar-Änderungen ab. Regressionstest enchantment_picker zwingend. |
| `sources/match/buff_ops.aiml:276–295`, `gc_zone_inplay` | `gent` wird über `remove_buff(&gent,...)` geändert und mit `idx` zurückgeschrieben. Value beibehalten; innere buff-Liste darf sich ändern, äußerer Zone-Slot bleibt bis Writeback stabil. |
| `sources/match/buff_ops.aiml:164–173`, `remove_buff` | `buf = at(zen.buffs,idx)` wird gelesen, anschließend Ursprung entfernt. Gerade hier ist besitzendes CopyRetain sinnvoll; **nicht** automatisch zu ref machen. |
| `sources/match/card_pick_helpers.aiml:28–30`, `revive_place_one` | `rp_d2`, `place_card_at(&rp_d2,...)`, danach `idx(mat.players,...)`: Value + Writeback behalten. |
| `sources/cards/effect_handlers.aiml:519–525`, `do_apply_buff` | `ctrl`/`opp` dienen hier zum Lesen der IDs. Keine ref-Migration erforderlich. |
| `client/card_editor_base.aiml:492–495` | `src_ab` wird in eine neue Ability übertragen. Copy-/Clone-Absicht des Editors separat testen; keine Write-through-View erforderlich. |

Für die vollständige Inventur bei Implementierung einen AST-basierten Bericht über Value-at/get/first/last-Bindungen und die aufgerufenen mutierenden Parameter erzeugen. Grep allein kann Callee-Schreibeffekte und Writebacks nicht beweisen. Bislang ist aus den geprüften Kernstellen **keine pauschale Pflichtmigration** belegt. Behavior-Tests zusätzlich zu Leak-Tests sind die Freigabebedingung; der Plan autorisiert keinen ungetesteten Massenersatz.

### 9.2 🟡 Wo genau CopyRetain bei `decl` emittieren
Bei [`ast_compiler_stmt.cpp:1359–1377`] wird HEUTE nur das Flag gesetzt, keine Kopie; die Materialisierung passiert erst an Escape-Punkten (unten). Unklar: soll `prepareOwnedModelValue`/CopyRetain HIER bei decl emittiert werden (memcpy in eigenen Slot + `aiml_ser_retain`, is_view_alias für Value entfällt), oder bleibt die „escape-time"-Struktur und nur die Kopie-Art wird getauscht (deep-copy → retain)? Ich weiß nicht, welchen der beiden Wege du willst.


**Antwort / Entscheidung:** CopyRetain erfolgt **bei der Value-Bindung**, nicht erst am Escape. Das Metadatenstück bei 1359–1377 klassifiziert nur; die IR-Emission gehört in den tatsächlichen Initialisierungs-/Store-Pfad von `visitDeclStmt`, nach einmaliger Auswertung des Initializers und vor Veröffentlichung des fertigen Werts. Die existierenden Pointer-vs-Struct-Initialisierungspfade zusammenführen: Slot-Adresse auflösen oder einmalig spillen → temporäre Value-Kopie → retain → in lokalen Zielslot übernehmen. Anschließend `Owned/Live` und genau einen Scope-Drop registrieren.

Für einen uninitialisierten Zielslot kein release; wurden durch einen bestehenden Initialisierungspfad bereits leere Container erzeugt, müssen diese vor Überschreiben freigegeben oder ihre unnötige Erzeugung vermieden werden. Nicht zugleich im Flag-Abschnitt und im späteren Store kopieren. Eigene producer/pop/clone-Ergebnisse werden adoptiert, nicht zusätzlich retained. `ref<Model>` bleibt der getrennte LValue-/Borrow-Pfad. Dieser frühe Besitzaufbau ist nötig, damit remove **vor** einem späteren Escape bereits sicher ist.

### 9.3 🟡 Welche der Deep-Copy-Sinks werden CopyRetain, welche bleiben `clone`?
`aiml_ser_deep_copy` wird an vielen Stellen emittiert: [`ast_compiler_stmt.cpp:1192, 1960, 2254, 2274, 2313, 2471, 2603, 3111, 3135, 3576`] (set/return/idx/member) und der Value-Model-**push** [`ast_compiler_call.cpp:1545ff`] (heute MOVE-wenn-movable, sonst Deep-Copy). Plan §4: Value-Materialisierung → CopyRetain, aber explizites `{clone}` → Deep-Copy behalten.
> ⚠️ Ich brauche pro Sink die Zuordnung **Value-Materialisierung (→ retain)** vs. **explizites clone (→ deep-copy bleibt)** — sonst rate ich. Bitte die Liste durchgehen und markieren.


**Antwort / Entscheidung:** Keine globale Ersetzung nach Funktionsnamen. Zuordnung anhand der aktuell gelesenen Zweigbedingungen (Zeilen können sich verschieben):

| `ast_compiler_stmt.cpp` / Sink | Entscheidung |
|---|---|
| 1192: `ctx.kvGetCalled`, Decode-Cache → neue MODL-Shell | **Vorläufig behalten, eigener KV/Refcount-Adapter**, kein inline-list-Sink. Nicht als expliziten clone fehlklassifizieren. Umstellung erfordert eigenen Cache-Eviction-/Mutate-set-Test und Prüfung des KV-Vertrags. |
| 1960/1962: decl mit `ctx.kvGetCopy` | **Explizite unabhängige KV-Kopie behalten**. Nicht automatisch CopyRetain. |
| 2254/2274: lokale Model-Zuweisung | **CopyRetain**, inklusive release-alt nach Sicherung; 2254 ist Kommentar, kein eigener Aufruf. |
| 2313: set aus `at` | **CopyRetain**. |
| 2471: set aus View/Member/Accessor | **CopyRetain**; Herkunft künftig aus Ownership-Fakten statt Namensheuristik. |
| 2603: set mit `ctx.kvGetCopy` | **Unabhängige KV-Kopie behalten**, alten Zielbesitz trotzdem korrekt ersetzen. |
| 3111: `memberDeepCopy` aus Borrow/Value | **CopyRetain**; Plan-Aktion umbenennen/umklassifizieren. |
| 3135: gemischtes `ctx.kvGetCopy || memberDeepCopy` | **Zweig aufteilen**: explizites KV-copy bleibt unabhängig, gewöhnliche Value-Zuweisung wird CopyRetain; ein besitzendes Ergebnis wird Move/Adopt. |
| 3576: return `kv.get_copy` | **Unabhängige KV-Kopie behalten**. Dies ist im aktuellen Stand nicht der normale View-Return! |
| 3655: normaler Value-Return aus View | **CopyRetain**; bereits besitzendes Local/Temp gemäß Transferplan Move/Adopt. |
| 4662: inline Model idx-Ersatz | **CopyRetain oder Move**, immer Quelle sichern vor release-alt. |
| 4790ff: Value-`store` | Bereits Share; über gemeinsamen Helper Reihenfolge korrigieren, damit auch indirektes Aliasing sicher ist. |
| 4850: `ctx.isRefcountModel`-Pointee-Store | **Separater Refcount-Pointee-Zweig**, in diesem inline-Paket nicht blind ändern. Kein Beleg für expliziten clone; eigener Folgetest/Vertragsabgleich erforderlich. |
| `ast_compiler_call.cpp:1545ff`, `pushDeepCopyModel` | **CopyRetain für kopierbare geborgte Quelle; Move für nachweislich besitzende Quelle**. Quelle vor Realloc sichern. Bereits verschobene Quelle ist Fehler, kein Copy-Fallback. |

Echte `{clone}`-/`sys.clone`-Pfade, unter anderem `emitContainerCloneDeep` und `visitCloneExpr`, behalten tiefe Graph-Kopie. Die expliziten KV-Copy-Pfade bleiben ebenfalls unabhängig, ohne sie semantisch mit dem vollständigen Clone-Walker gleichzusetzen. `1192` und `4850` sind bewusst abgegrenzte Adapter, keine Freigabe zum ungeprüften Erhalten beliebiger Deep-Copies in normalen Value-Sinks. Tests müssen beide Adapter getrennt von der inline-Abnahme abdecken, falls ein Zieltest sie benutzt.

### 9.4 🟡 `is_view_alias` — entfernen oder durch Provenienz ersetzen?
Konsumiert an [`ast_compiler_stmt.cpp:2475` (reset nach unabhängiger Kopie), `3229`, `3633`] u.a. Es trennt heute NICHT sauber Borrow (`ref`) von Value-View. Plan §3 will „alte syntaktische View-Heuristiken für Value-Kopien entfernen" + §5 echte `BorrowOrigin`-Provenienz. Unklar: `is_view_alias` komplett durch den OwnershipPlan (§5) ersetzen, oder für den reinen `ref<Model>`-Borrow behalten und nur den Value-Zweig herauslösen?


**Antwort / Entscheidung:** Ziel ist **Entfernung des bool als semantischer Ownership-Quelle**, kein Umwidmen zu „ref-Flag“. Bei materialisierten Values entfällt es. Echte ref-Borrows benötigen Owner-/Pfad-Provenienz und Gültigkeit im OwnershipPlan; der deklarierte ref-Typ kennzeichnet bereits die Referenzform. Ein bool kann weder Alias-Ketten noch CFG-Merges korrekt abbilden.

In einem Zwischencommit darf das Feld für noch nicht umgestellte Legacy-Zweige existieren, aber nicht die Freigabe neuer CopyRetain-Values unterdrücken. Sämtliche Leser (einschließlich consume/drop/return, nicht nur die drei genannten Zeilen) auf Fakten umstellen; danach Feld und veraltete Kommentare löschen. Für unbekannte Fakten Diagnose/ausdrücklich begrenzten Legacy-Pfad nutzen, nicht Value versehentlich als nicht besitzend behandeln.

### 9.5 🟡 Move-only-Modell-Prädikat fehlt — wo ansiedeln?
Kein Prädikat erkennt „Model hat (auch transitiv) ein unique `ptr<T>`-Box- oder `tensor`-Feld" → nicht implizit value-kopierbar. `ret()` überspringt unique korrekt [`ser_runtime.cpp:826–833`], aber der **Compiler muss die Value-Kopie solcher Modelle ablehnen** (sonst teilt die Kopie eine nicht-besessene Ressource → double-free/UAF). Vorschlag: `ctx.isMoveOnlyModel(typeName)` via serNodes-Walk (PTR_STRUCT=10 / TENSOR + PTR_REF param2==0). Unklar: an welchen Sinks prüfen (decl/push/set/return) und welche exakte Diagnose-Formulierung/„static_off"-Escape du willst. Vorhandener Negativtest-Anker: `tests/lifetime/neg/n13_move_only_value_copy_from_at.aiml`.


**Antwort / Korrektur:** Das Prädikat fehlt nicht vollständig: `modelIsMoveOnly` existiert als statischer Helper in `ast_compiler_stmt.cpp:923–943`; geprüft werden `ptr<...>`, `tensor` und transitiv verschachtelte User-Modelle. Es wird bereits bei decl und set benutzt (u.a. 959 und 2208). Es fehlt seine **vollständige Typabdeckung und sinkübergreifende Nutzung**.

Entscheidung: aus dem lokalen Helper einen gemeinsamen semantischen Type-Trait, z.B. `CompilerContext::isCopyableValue(type)`, ableiten. Über deklarierte Membertypen und zentrale Ressourcenklassifikation laufen, **nicht** über Serialisierungs-TD als alleinige Wahrheit: RAWBYTES kann Lifecycle-Information verlieren. Arrays rekursiv prüfen, dyn_array und sämtliche Class-4-Ressourcen als unique klassifizieren. ref/shared sowie rc-Container bleiben kopierbare Handles; `list<ptr<T>>` als Model-Feld macht das Model nicht selbst move-only, weil nur das Listen-Handle kopiert wird. Value-Nesting rekursiv und mit Zyklusguard, Traits erst nach abgeschlossener Typregistrierung cachen.

Prüfung zentral vor jedem CopyRetain und zusätzlich in den AST-Checks für decl, set, Member/idx/store, push/insert/extend/slice, get_copy und Value-Argument/Return aus geborgter Quelle. Ein nachgewiesener Besitz-Move braucht keine Copy-Fähigkeit. Diagnosevorschlag: `Cannot copy move-only model 'M': field 'M.child.resource' owns a unique resource. Bind ref<M>, transfer an owned value, or use clone if supported.` **Kein static_off-Escape** für diese Ownership-Verletzung; statische Style-Abschaltung darf keine doppelte Unique-Ownership erzeugen. clone selbst prüft Klonbarkeit gesondert.

### 9.6 🟡 Arrays-of-Heap als Model-Feld — existiert das?
Es gibt **kein `AIML_TD_ARRAY`** (nur RAWBYTES=9 für value-only). `array<string:N>`/`array<Model:N>` als Model-Feld wäre von `ret()`/Release nicht erfasst. Unklar: existiert das überhaupt als Feldtyp, und wenn ja, in welcher TD-Kodierung? Falls unterstützt → retain/release-Zweig ergänzen; falls nicht → als „nicht unterstützt" dokumentieren.


**Antwort / Befund:** Arrays existieren in der Typauflösung: `CompilerContext::getLLVMType`, `context.h:955ff`, erzeugt den LLVM-Arraytyp. `serFieldSubtree` in `ast_compiler.cpp` hat dagegen aktuell keinen Array-Zweig und fällt für diesen Typ auf RAWBYTES zurück (3469–3474). `ret()` hat folglich keine Elementinformation. Der vorhandene Test `tests/lifetime/13_array_dynarray_cascade_map_buffer.aiml` verwendet ein **lokales** `array<string:3>`; er beweist nicht den Lifecycle eines Model-Felds. Es ist daher eine Descriptor-Lücke, keine belastbare Aussage „Arrays sind als Feld nicht unterstützt“. Ein Model-Feld-Compile-/Lifecycle-Test ist als erstes zu ergänzen; diese Untersuchung hat ihn noch nicht ausgeführt.

Entscheidung für dieses Paket: für feste Array-Felder die vorhandene STRUCT-Komposition im Lifecycle-Deskriptor verwenden: flach N (bei mehreren Dimensionen Produkt) Element-Unterbäume mit tatsächlichem LLVM-Stride/Offsets erzeugen; Unterbaum-Indizes korrekt verschieben. Retain/Release können so über vorhandene STRUCT-Rekursion laufen, ohne einen neuen TD-Enum-Wert und sämtliche Walker gleichzeitig zu verändern. Diese Darstellung ausschließlich für Lifecycle einsetzen, falls die bestehende Wire-/Clone-Repräsentation davon abweicht; bestehende Serialization-Layouts nicht still verändern. Bei fehlender sauberer Trennung eigenen Lifecycle-Descriptor einführen. Große Arrays können später einen kompakten ARRAY-Knoten bekommen.

POD-Arrays brauchen keinen Retain. Arrays mit Unique-Elementen machen das umschließende Value-Model move-only; Release muss dennoch jedes Element abarbeiten. Bis die Feld-Lifecycle-Unterstützung nachgewiesen ist, solche Heap-Array-Felder in den neuen Copy/Release-Pfaden mit gezielter Fehlermeldung ablehnen statt RAWBYTES als „sicher“ zu behandeln. Vollständige Abnahme umfasst die Array-Feldtests; bloßes Dokumentieren der Lücke erfüllt sie nicht.

### 9.7 🟡 Element-Release-Lifecycle nicht verdoppeln (Abgleich mit meinem bereits gebauten Teil-Fix)
Ich habe für die SICHEREN Kinds bereits gebaut: `free_list_elem_at` + `aiml_list_clear_elems` + `aiml_list_pop_free` [`src/sys/string_runtime.cpp`], `Compiler::classifyListElem` [`ast_compiler_stmt.cpp`], sowie `clear`/`pop`-Verdrahtung [`ast_compiler_call.cpp`] — **elemKind 2 (Value-Model-inline) ist dort bewusst ÜBERSPRUNGEN** (`skipInlineValueModel`). `remove` gibt elemKind 4/5 bereits frei [`ast_compiler_call.cpp:1802–1848`]; der finale List-Drop macht elemKind 2 via `aiml_list_free` [`string_runtime.cpp:192–195`].
> ⚠️ Plan §6 will EINEN einheitlichen Walker. Unklar: (a) elemKind 2 in `free_list_elem_at` einfach aktivieren, sobald Paket 3 steht? (b) wie remove/clear/pop/final-drop denselben Element-Lifecycle teilen, OHNE dass final-drop (`aiml_list_free`) und ein neuer Walker dasselbe Element doppelt releasen? Ich bin unsicher, ob ich `aiml_list_free`s elemKind-2-Zweig auf `free_list_elem_at` umstelle oder beide getrennt lasse.


**Antwort / Entscheidung:** Den vorhandenen `free_list_elem_at` als **einzigen Element-Release-Dispatcher** weiterverwenden und auch `aiml_list_free` darauf umstellen. Vorwärtsdeklaration/Position anpassen. Das ist ein Runtime-Refactoring, keine zweite Freigabeschicht.

- Final-drop: über die zu diesem Zeitpunkt aktiven Slots laufen, Dispatcher einmal pro Slot aufrufen, danach ausschließlich Datenbuffer freigeben und Header nullen. Wrapper `aiml_list_handle_decref` gibt bei rc 0 anschließend die Handle-Shell frei.
- clear: dieselbe aktive Range releasen, len=0, Buffer behalten. Danach sieht final-drop keine aktiven Elemente mehr.
- remove: einen Runtime-Einstieg mit Bounds-Prüfung, Dispatcher für genau den entfernten Slot und danach Shift/len-- verwenden. Bisherige C++-Codegen-Sonderfreigaben für denselben Kind entfernen, sobald dieser Einstieg benutzt wird; kein zusätzliches inline-decref davor. Inaktiven Tail nicht erneut droppen.
- pop-discard: vorhandenen `aiml_list_pop_free` als optimierten Drop des extrahierten Ergebnisses behalten. Gebundener/weitergereichter pop überträgt Besitz und ruft den Dispatcher **nicht** am Listen-Slot auf.

Während Migration ist `skipInlineValueModel` nur beim vorzeitigen Leeren aktiv; final-drop behält seine bisherige Kind-2-Freigabe. Nach bestandener Materialisierung/Transfer-Abnahme Kind 2 überall freigeben und den Skip-Schalter entfernen. Logging für Kind 3 sowie Stride für Kind 6 erhalten. Kind 6 ist weiterhin nur shallow für die konkrete Interface-Implementierung; die Vereinheitlichung behebt diese separate Destruktorlücke nicht.

**Kommentarfehler im Teil-Fix korrigieren:** „inline field heap is left to eventual list scope-drop“ trifft nach clear/pop nicht zu: Die verkleinerte len verhindert gerade, dass final-drop diese Slots noch besucht. Das aktuelle Skip ist ein bewusstes Leak, keine spätere Freigabe. Double-free wird durch den aktiven Bereich und eindeutige Zuständigkeit vermieden, nicht durch zwei unabhängig laufende Walker.

### 9.8 🟡 pop-discard in verschachteltem/Call-Kontext
Mein `ctx.popResultDiscarded`-Flag [`context.h` + `visitExprStmt` in `ast_compiler_stmt.cpp`] fängt nur den DIREKTEN `call: [l.pop]`-Statement-Fall. Plan §5.5 will einheitliche Temp-Registrierung/Konsumierung (auch `foo(l.pop())`, tief verworfen). Die `stmtTempStack`-Maschinerie ist heute **string-only** [`ast_compiler_stmt.cpp:181–213`]. Unklar: typisierten owned-Temp-Stack (Model/Handle) einführen, oder reicht der Direkt-Statement-Fall? (Für elemKind 2 ist pop ohnehin erst nach Paket 3 sicher.)


**Antwort / Entscheidung:** Für die vollständige Abnahme genügt der Direkt-Statement-Fall nicht. Einen **typisierten Owned-Temp-Stack** einführen; vorhandene string-Temps darin integrieren oder zunächst über eine gemeinsame Schnittstelle anbinden. Ein Temp-Eintrag braucht eigene Identität, Storage/SSA-Wert, Typ/Lifecycle-Aktion, Live/Consumed-Zustand und die Auswertungsregion. Nicht nur ein globales bool.

`foo(l.pop())` ist nicht automatisch ein konsumierender Call: leiht foo den Parameter, bleibt der Temp bis nach dem Call besitzend und wird danach gedroppt. Übernimmt foo Ownership, wird der Temp als consumed markiert und nicht beim Caller gedroppt. Ein zurückgelieferter Borrow darf den nötigen Anker nicht verlieren; ein Return-by-value wird separat adoptiert. Gleiches gilt für verschachtelte Calls und weggeworfene äußere Resultate.

Temps in umgekehrter Erzeugungsreihenfolge am Ende der **tatsächlich ausgeführten** Full-Expression freigeben; Bedingungen/Short-circuit/Return/early-exit brauchen eigene Cleanup-Kanten. Übergabe an decl/return/push markiert genau den betreffenden Temp als übernommen. Quellen genau einmal auswerten. `ctx.popResultDiscarded` darf vorläufig als optimierter Direktfall bleiben, aber darf dann keinen zusätzlichen Owned-Temp-Drop erzeugen; nach Vereinheitlichung als unnötigen Zustand entfernen oder aus dem Temp-/Expression-Plan ableiten. Tests für borrowed/consumed Calls und Branches sind Pflicht.

### 9.9 🟡 Leak-Gate — Sanitizer-Suite genügt nicht (ich stimme dem Plan zu)
Plan §7 sagt korrekt: `tests/run_memory_sanitizers.sh` läuft mit `--errors-for-leak-kinds=none` → dessen PASS ist nur Memory-Safety, KEINE Leak-Abnahme. Ich habe bisher auf diesem PASS + separaten `definitely lost`-Probes aufgesetzt. Unklar: willst du ein hartes Leak-Zahlen-Gate direkt in `run_memory_sanitizers.sh` (pro Probe `definitely+indirectly == 0`), oder ein separates Skript? Das würde auch meine bisherigen Fixes rückwirkend absichern.

**Antwort / Entscheidung:** **Separates hartes Gate** `tests/run_lifetime_leak_gates.sh` anlegen; vorhandenen Safety-Runner nicht global auf Null-Leaks umstellen. Er enthält absichtliche Fehlerkontrollen und Tests mit anderer Messmethodik. CI/Abnahme ruft beide auf.

Das Leak-Gate führt eine explizite Liste isolierter Lifetime-/9rooms-Probes aus: memV3 und Drops aktiv, nativer Assertion-Lauf erfolgreich, danach Valgrind mit `--leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect --error-exitcode=99`. Vollständigen Bericht und Prozessstatus prüfen; fehlender Bericht, Signal, Buildfehler oder nicht installierter Valgrind im verpflichtenden CI-Job sind Fehler, kein PASS. Für maschinenlesbare Auswertung XML verwenden. Definite/indirect müssen 0 sein; beide Kategorien nicht doppelt aus den Detailrecords aufsummieren (indirekte Verluste können im Definite-Record bereits enthalten sein). Für das Gate genügt schon das Vorhandensein einer dieser Leak-Arten als Fehler.

Historische rote Probes getrennt als reproduzierbare Befunde halten. Erst nach bestandenem Fix in die Liste der erwarteten Null-Leak-Probes aufnehmen; sichere bereits behobene Kinds jetzt aufnehmen. Kontrollen beweisen, dass das Gate einen absichtlichen Leak tatsächlich ablehnt. 10/1000-Läufe für Wachstumsnachweis weiter dokumentieren, still-reachable nicht pauschal als Verlust zählen. Compiler-/Runtime-Stand und vollständige Logs als Artefakte speichern. Die bestehenden Dateien unter `9rooms_v2/leak_probes` bleiben die projektbezogenen Reproduktionen.

---

## 10. Umsetzungsstand + geplante nächste Schritte (Claude, 2026-09-24 — zum Review)

Dieser Abschnitt hält präzise fest, was tatsächlich umgesetzt/committet wurde, welche empirischen Befunde vorliegen, und was ich als nächstes vorhabe. Alle Commits: aiml branch `mem-v3-compiler`, 9rooms branch `memv3`.

### 10.1 Erledigt + committet (sicher, verifiziert)

**A. Zwei separate Leak-Klassen behoben (nicht der inline-Value-Model-Fall — das sind die *anderen*, sicheren Kinds):**
- **owned-String-Return** (§6.2 T1): `retStrFieldOfOwnedModel` in visitRetStmt (incref Feld + Owner voll droppen, gekoppelt → nie double-free) + neues `currentFunctionOwnsReturn`-Flag. 9rooms-Quellen als `producer` markiert (pid_for/nick_for/nick_for_pid/resolve_token/resolve_token_pid; client avatar_initial/join_pipe/card_sig/zone_sig/board_content_sig/take_edit_card_id). — aiml `196b2c1`, 9rooms `585341c`.
- **pop(verworfen)/clear Element-Release** für die SICHEREN Kinds (1 string / 3 refcount-model / 4 ptr<T>-box / 5 nested-container-handle / 6 iface): Runtime `free_list_elem_at` + `aiml_list_clear_elems` + `aiml_list_pop_free`; Compiler `classifyListElem` + clear/pop-Verdrahtung + `popResultDiscarded`-Flag. **elemKind 2 (Value-Model-inline) bewusst ÜBERSPRUNGEN.** — aiml `196b2c1`.
- Beide valgrind-verifiziert (Standalone + 9rooms leak_probes 0-lost), mem-sanitizer memory-safe, Korpus 422/461 (kein neuer Regress). Regressionsprobes `erf` + `pcr` in run_memory_sanitizers.sh.

**B. Verworfener Auto-Detect (String-Return-Klassifikator):** breite Auto-Klassifikation verursachte eine 9rooms-Client-Navigations-Regression (Deck→Card-Editor), die Leak/Sanitizer NICHT fingen → DISABLED belassen (`classifyOwnedStringReturn`, `[[maybe_unused]]`). Lehre: breite Ownership-ABI-Änderungen brauchen ein Game+Client-VERHALTENS-Gate.

**C. Array-Feld-Diagnose (§9.6):** deduplizierte Warnung im serFieldSubtree-RAWBYTES-Fallback für Heap-Element-Array-Model-Felder (kein fp-Risiko; POD/lokale Arrays unbetroffen). — aiml `96bc6fa`.

**D. Deferred-Kommentare markiert** (ast_compiler_call.cpp remove ~1873, ast_compiler_stmt.cpp decl ~1355, string_runtime.cpp free_list_elem_at) + Kommentarfehler korrigiert (§9.7: „left to scope-drop" war falsch → bewusster Leak).

### 10.2 Paket 1 + 2 + Prototyp — Befunde

- **Paket 1 (rote Vertrags-Tests, `tests/_inline_model_red/`, vom Korpus ausgeschlossen):** `49_value_survives_remove` (Content GRÜN, Leak RED 47+16 B), `50_share_semantics` (**bereits GRÜN** — container-shared + scalar-independent stimmt schon). **Re-Scoping-Befund:** Share-Semantik + Content sind bereits korrekt; die einzige Lücke ist der elemKind-2-Release-LEAK. Content ist nur grün, WEIL remove/clear kind-2 nicht freigibt (der Leak hält die View gültig) → naives Release ohne CopyRetain bricht den Content → beweist Sequenz 3→5.
- **Paket 2 (Retain/Release-Audit):** `p2_retain_coverage` GRÜN (0 lost, 0 errors, 200 Iter) → `aiml_ser_retain` co-owned string/list/map/nested-model korrekt über den store-retain-Pfad. `p2_array_field_leak_RED` RED (6780 B) → §9.6-Lücke bestätigt. `modelIsMoveOnly` existiert (ast_compiler_stmt.cpp:923). → **Runtime-Primitive sind vorhanden + korrekt.**
- **Paket 3 Prototyp (`p3_copyretain_survives_source_drop`, GRÜN):** eine retain-Kopie überlebt die kind-2-Freigabe des Quell-Elements (aiml_list_free scope-drop = dieselbe `aiml_ser_free_decoded` wie remove/clear) balanciert, intakter Inhalt über alle Feldarten. — aiml `9195ddb`. **→ Mechanik risikofrei bewiesen.**

### 10.3 Paket-3-Umsetzungsversuch „Stage A" (decl-only) — VERWORFEN

Versuch: CopyRetain nur am `decl copy = at(..)`-Init (memcpy → `aiml_ser_retain` + owned-Drop + `is_view_alias=false`), Escape-Sinks unverändert. **Ergebnis: mem-sanitizer FAIL (1 Check).** → **Empirischer Beweis: decl allein reicht NICHT.** Die Escape-Pfade (set/idx/return/push) tragen eigene is_view_alias-/deep-copy-Logik; eine owned+retain-Kopie am decl widerspricht ihnen (doppeltes retain/copy oder falscher Move → double-free/Imbalance). Reverted, grüner Stand + mem-sanitizer PASS bestätigt. — aiml `26caacc` (Doku).

### 10.4 Exposure (ROI + Blast-Radius)

elemKind-2 (`list<Value-Model>` mit Heap) betrifft die **Kern-Strukturen**: `list<ZoneEntry>` 93×, `list<MockCard>` 55×, `list<ActiveAbility>` 30×, `list<PlayerState>`/`Deck`/`ForumThread`/… → **hoher Nutzen ABER hoher Blast-Radius.** `list<ptr<Model>>` (elemKind 4, schon gefixt) ist selten (ActiveAbility 5×, CardBehavior 3×).

### 10.5 WAS ICH VORHABE — koordinierter Multi-Sink-Paket-3-Umbau (Reihenfolge + Gate)

Kein isolierter Patch mehr. Die Value-Materialisierung muss über ALLE Sinks mit EINEM Ownership-Fakt konsolidiert werden. Geplante Reihenfolge (jeder Schritt einzeln gegatet, Revert bei jeder Regression):

1. **Gemeinsames Primitiv** `emitValueModelCopyRetain(dstAddr, srcAddr, type)` (memcpy + `aiml_ser_retain`, move-only via `modelIsMoveOnly` ablehnen) + `emitValueModelRelease` (bereits `aiml_ser_free_decoded`). EIN Helfer, den alle Sinks nutzen.
2. **`is_view_alias` durch Ownership-Fakt ersetzen** (§9.4): die Sinks decl(1991)/set(2313,2471)/idx(4662)/return(3655)/push(ast_compiler_call.cpp:1545)/store(4790) ALLE gleichzeitig auf CopyRetain umstellen; kein Sink darf zusätzlich deep-copyen/retainen (sonst Imbalance wie Stage A). Reihenfolge secure-before-release (src retainen BEVOR dst-Release), auch indirektes Aliasing (§3).
3. **elemKind-2-Release aktivieren** (`skipInlineValueModel=false`; remove/clear/pop/final-drop über EINEN Dispatcher `free_list_elem_at`, §9.7).
4. **Gate pro Schritt:** mem-sanitizer (memory-safe) + Korpus (422/461) + `tests/lifetime` spec61 + die roten Tests (49 muss GRÜN werden: Content + 0 lost) + **hartes Leak-Zahlen-Gate** (§7, `run_lifetime_leak_gates.sh`) + 9rooms-Regressions (effect_processor/effect_death/mana_pick/enchantment_picker) + **Game+Client-Build + In-Game-Verhaltenstest** (Combat/Board/Deck-Editor). Erst wenn ALLE grün → nächster Sink.
5. **Move-only + Array-Felder:** `isCopyableValue`-Trait (§9.5) an jedem CopyRetain-Sink prüfen (Diagnose statt stillem Leak); Array-Feld-Lifecycle-Descriptor (§9.6) separat, wegen fp-Risiko eigener Pass.

**Offene Rest-Leaks (separat, nicht Teil dieses Plans):** Server ~540 B/Zyklus in `handle_validate` (Model-Geschwister-Teardown bei Whole-Model-`return out`, Repro `_mem_sanitize/handle_validate_sibling.aiml`) — andere Klasse, Fix offen.

**Empfehlung:** Der Multi-Sink-Umbau (10.5 Schritte 1-3) ist ein fokussierter Einzelauftrag mit dem vollen Gate aus Schritt 4 — nicht am Ende einer langen Session, sondern als dedizierte Sitzung, weil jede Zwischenstufe die Kern-Strukturen berührt.


---

## 11. Review der Fehleranalyse und Neuplanung (Codex, 24.09.2026)

**Urteil: Richtung sinnvoll, §10.5 in der jetzigen Reihenfolge noch nicht umsetzungsreif.** Der gemeinsame Ownership-Vertrag ist richtig. Zu korrigieren sind die Beweisstärke der Stage-A-Erklärung, die Position der Typ-Sicherheitsprüfungen, der Umgang mit mehreren legitimen Besitzreferenzen und die widersprüchlichen Zwischen-Gates. Dieses Review präzisiert §10; historische Berichte oben bleiben erhalten.

### 11.1 Verifikation und reproduzierte Ergebnisse

Geprüfter Quellstand: HEAD `362349f`; die genannten AIML-Commits `196b2c1`, `96bc6fa`, `9195ddb`, `26caacc` sind vorhanden. Code bestätigt: String-Autoklassifikation an den Aufrufstellen auskommentiert, Kind-2-Skip bei clear/pop aktiv, Array-Feld-Warnung vorhanden. `26caacc` enthält ausschließlich die Dokumentation des verworfenen Versuchs, nicht dessen Patch oder Fehlertrace.

Alle fünf vorhandenen Probes unverändert neu nach native Object kompiliert, mit g++ gegen die vorhandene libaiml gelinkt, nativ und unter Valgrind 3.22.0 ausgeführt. Beide Flags waren jeweils 1. **Kein kompletter Compiler-Neubuild, keine erneute vollständige Korpus-/Client-Abnahme.** Die Binäridentität ist deshalb separat festgehalten:

- Compiler SHA256: `ec1b66547c4aebc50e74c0cc2c448bd44be3ae74803a519b6d133c61ed774c7b`
- libaiml SHA256: `233596daaad53729ba867d10ea5abead2fcd6036295333809f66b23349451d9d`
- Rohlogs dauerhaft: [analysis/inline_value_model_review_2026-09-24](analysis/inline_value_model_review_2026-09-24/). Temporäre Kompilate: `/tmp/inline-plan-review-wp17hded/`.

| Probe | Native Exit | Definitely / indirectly lost | Valgrind Exit |
|---|---:|---:|---:|
| 49_value_survives_remove | 0 | 47 / 16 B | 99 |
| 50_share_semantics | 0 | **16 / 0 B** | 99 |
| p2_retain_coverage, 200 Iterationen | 0 | 0 / 0 B | 0 |
| p3_copyretain_survives_source_drop, 200 Iterationen | 0 | 0 / 0 B | 0 |
| p2_array_field_leak_RED | 0 | 6780 / 0 B | 99 |

Die gemeldeten Valgrind-Fehler in diesen roten Läufen sind Leak-Records; die Logs zeigen keine Invalid-read/write/free-Meldungen. Verwendet wurde `--leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect --error-exitcode=99`. Build- und Native-Logs liegen jeweils beim Valgrind-Log.

### 11.2 Priorisierte Befunde

**P1 — Stage-A-Ursache nicht nachgewiesen (§10.3).** Ein fehlgeschlagener Check belegt die Regression dieses Versuchs, aber nicht „alle Sinks müssen gleichzeitig geändert werden“. Im dokumentierten Stand fehlen Testname, Fehlerart, Trace, reproduzierbarer Patch und minimaler Gegenfall. „Doppeltes retain/copy oder falscher Move“ sind alternative Hypothesen. Ein zusätzlicher Retain für einen zusätzlichen Besitzer ist korrekt; eine separat besitzende Deep-Copy kann semantisch falsch oder teuer sein, verursacht aber nicht allein dadurch Double-free.

Vor einem breiteren Umbau Stage-A-Patch in isoliertem Checkout rekonstruieren/sichern, exakten roten Check mit Baseline vergleichen und auf Minimalprobe reduzieren. Besonders `ownership_analyzer.cpp::freshOwned`, `StatementPlan` und `mayDrop` gegen `visitDeclStmt::modelOwnedInit`/Cleanup-Registrierung abgleichen: ein IR-Retain plus registrierter Drop genügt nicht, wenn die Flow-Fakten den Drop verweigern oder einen Move falsch klassifizieren. Das ist eine konkrete Prüfrichtung, **keine hier bewiesene Fehlerursache**. Falls der ursprüngliche Versuch nicht rekonstruierbar ist, als nicht kausal geklärte Regression dokumentieren; die Multi-Sink-Architekturentscheidung davon unabhängig begründen.

**P1 — Typ-/Lifecycle-Sicherheit kommt in §10.5 zu spät.** Schritt 5 (`isCopyableValue`, Array-Unterstützung) darf nicht erst nach CopyRetain und Kind-2-Freigabe kommen. Der vorhandene `modelIsMoveOnly` ist weiterhin auf ptr/tensor/Value-Nesting begrenzt. RAWBYTES-Array-Felder besitzen keine sichere Retain/Release-Abdeckung. Vor jedem neuen Materialisierungspfad getrennt prüfen: (a) semantisch kopierbar und (b) Lifecycle vollständig unterstützt. Ein kopierbares Array mit fehlendem Descriptor erfüllt nur (a). Für noch nicht unterstützte Typen gezielter Compilefehler oder klar isolierter alter Pfad ohne neue Freigabegarantie; Warnung allein ist keine Sicherheitsvoraussetzung. Array-Lifecycle darf separat implementiert werden, aber nicht nachträglich als Voraussetzung für bereits aktivierte allgemeine Freigaben behandelt werden.

**P1 — „ALLE Sinks auf CopyRetain“ und „kein zusätzlicher Retain“ sind zu pauschal (§10.5.2).** Jeder Sink braucht dieselbe Transferentscheidung, nicht zwingend dieselbe Aktion:

| Quelle / Verwendung | Aktion |
|---|---|
| kopierbare geborgte Quelle → neuer Value-Besitzer | CopyRetain |
| bestehender besitzender Value bleibt beim Caller und wird zusätzlich gespeichert | CopyRetain für den zusätzlichen Besitzer |
| tatsächlicher Besitztransfer eines Locals/Temps | Move/Adopt, Quelle invalidieren, genau einen Cleanup konsumieren |
| echter ref-Borrow | kein Besitz-Retain und kein Owner-Drop |
| explizites clone / unabhängige KV-Copy | vorhandenen unabhängigen Copy-Vertrag erhalten |

Die Invariante lautet: **jede neu entstehende Besitzreferenz hat genau einen Retain oder einen übernommenen Besitz; jeder auslaufende Besitz genau einen Release.** Nicht „höchstens ein Retain pro Ausdruckskette“. Insbesondere `decl copy=at(...); push(copy)` benötigt je nach Copy-/Move-Vertrag unterschiedliche Aktionen. Die sinkweise Ausnahmetabelle §9.3 bleibt maßgeblich.

**P1 — Temp-/Call-Ownership und echte Borrows fehlen in der verkürzten Neuplanung.** §10.5 nennt die wesentlichen Stores, aber übernimmt §9.8 sowie §5 nicht ausreichend in seine Freigabevoraussetzungen. `foo(rows.pop())`, ein verworfenes äußeres Ergebnis, Return/early-exit und Bedingungen müssen typisierte Temp-Cleanups besitzen. Call-Parameter unterscheiden Borrow, Value-Copy und Konsum. Bindungen `ref<Model>` dürfen durch remove/clear/Shift nicht unbemerkt zu UAF werden. Vor allgemeiner Kind-2-Aktivierung mindestens lokal beweisbare Invalidierungen diagnostizieren und entsprechende Negativtests ausführen. Vollständige interprozedurale Analyse kann begrenzt bleiben; die Grenze muss ausdrücklich dokumentiert sein.

**P2 — „einzige Lücke ist Release-Leak“ und „risikofrei bewiesen“ überzeichnen die Tests (§10.2).** Test 50 verliert **16 Byte**, obwohl er weder remove noch clear benutzt. Der Trace zeigt `aiml_str_dup` im main; im Quelltext wird `copy.label = "changed"` gesetzt. Damit besteht bereits ein weiterer Verlustpfad innerhalb der unbalancierten Value-Kopie. Der konkrete Drop-Pfad ist beim Fix zu prüfen; ein reiner remove-Patch reicht für diesen Test nicht.

Die P2/P3-Prototypen bestätigen den getesteten store-Retain-Pfad für bestimmte Felder. P3 prüft nach Quell-Drop label, inner.tag und tags[0], aber nicht mana-Inhalt und inner.nums; shared/Class-5/Arrays/Move-only/Call-Temps sind damit ebenfalls nicht vollständig abgedeckt. Korrekte Formulierung: „Mechanik für die getesteten Feldarten und den store-Pfad bestätigt; Integration und weitere Klassen offen“. Map-Inhalt, inner.nums, Wechsel der Drop-Reihenfolge und Selbst-/Unterobjekt-Aliasing als Assertions ergänzen.

**P2 — Modell der bisherigen View ist ungenau.** Die Headerkommentare von Test 49/50 behaupten, ein Shift ändere den gelesenen äußeren Slot bzw. String-Assignment schreibe durch. Der aktuelle decl-Pfad kopiert jedoch bereits Struct-Bytes in lokale Storage (`ast_compiler_stmt.cpp`, gewöhnlicher memcpy-Zweig um 1990ff), ohne dabei vollständigen eigenen Member-Besitz zu materialisieren. Zu unterscheiden sind also (1) eigenständige Struct-Bytes mit ungesicherten Feldreferenzen und (2) echte ref auf einen Slot. Testheader an beobachteten Istzustand anpassen; kein pauschales „copy-on-write-at-set“ als Erklärung verwenden, solange nicht im jeweiligen Pfad belegt.

**P2 — Gate-Reihenfolge widerspricht sich (§10.5.2–4).** Einerseits sollen alle Sinks gleichzeitig wechseln, andererseits „erst alle grün → nächster Sink“. Zudem kann Test 49 vor Aktivierung des Element-Release weiterhin erwartbar leaken; „alle Zieltests müssen nach jedem Vorbereitungsschritt grün sein“ blockiert den geplanten Ablauf. Tests nach Entwicklungsphase kennzeichnen: Baseline-Erhaltung, neue Helper-/Transfer-Tests, erwartete rote Zieltests, abschließendes Null-Leak-Gate. Koordinierter Endzustand kann aus kleinen überprüfbaren Commits bestehen. Kein teilkonvertierter Zustand wird als neue produktive Semantik ausgeliefert.

**P2 — 422/461 ist kein hinreichendes Regression-Gate.** Dieselbe Anzahl grüner Tests kann neue Fehler bei gleichzeitig behobenen alten Fehlern verbergen. Identische Testauswahl und Flags, Vergleich der konkreten Status pro Test, Exitcodes und keine neuen Skips verlangen. Existierende Fehlschläge namentlich baselinen. Das angekündigte `tests/run_lifetime_leak_gates.sh` existiert beim Review noch nicht; daher vor dem nächsten Umsetzungsversuch als Harness bereitstellen. Client-Verhaltensprüfung konkret als Szenarien mit Soll-Ergebnis und Evidenz definieren (Deck→Card-Editor→zurück, Änderung speichern/neu öffnen, Buff anwenden/ablaufen lassen, Board-Zustand). Ein Client-Build allein erfüllt sie nicht.

**P2 — Array-Warnung ist nützlich, aber keine vollständige Diagnose (§10.1.C).** `serFieldSubtree` trennt den Array-Elementtyp aktuell am ersten `:`; bei `array<map<string:i32>:N>` liegt dieser innerhalb des Elementtyps. ptr/shared/tensor/path/Class-5 sind nicht vollständig in `elemHeap` enthalten; spätere Typregistrierung kann den hasHeapFields-Befund beeinflussen. Gemeinsamen verschachtelungsfesten Typ-Parser und Lifecycle-Trait nutzen. Die deduplizierende Menge ist funktionsstatisch/prozessweit; für verlässliche Diagnosen besser pro CompilerContext/Compile-Run. Die Warnung verändert hier zwar keine Deskriptorbytes, deckt aber nicht alle gefährlichen Typen ab. Kein Sicherheitsnachweis aus ihrem Ausbleiben ableiten.

### 11.3 Verbesserte Neuplanung (ersetzt die Reihenfolge aus §10.5)

1. **Belege und Baseline sichern.** Stage-A-Failure isolieren oder die ungeklärte Ursache ausdrücklich festhalten. Test 50 in die Leak-Ziele aufnehmen. Historische README-/Testkommentare korrigieren. Compiler, Runtime, Flags und Teststatus pro Fall speichern.
2. **Harness und Typvoraussetzungen vorziehen.** Hartes Leak-Gate erstellen; `isCopyableValue` plus gesonderte Lifecycle-Abdeckung zentralisieren. Unsupported-Array-/Unique-Fälle vor neuen Copy-Pfaden abfangen. Wire-/Lifecycle-Descriptor-Trennung vor Array-Arbeit festlegen.
3. **Gemeinsame Transferprimitive ohne breite Aktivierung.** CopyRetain / Move / Borrow / Clone explizit unterscheiden. API mit Quell- und Zielzustand, einmaliger Evaluation und eindeutigem Cleanup-Token statt nacktem memcpy-Helper. Source sichern → retain bzw. move-invalidieren → release-old → commit; Self-/Unterobjekt-Alias und möglichen Realloc testen. Bestehenden store-Pfad damit absichern.
4. **OwnershipPlan und Codegen gemeinsam migrieren.** Pro vollständiger Produzent-Verbraucher-Kette decl→set/store/idx/return/push und Call-Übergaben umstellen, einschließlich `freshOwned`, mayDrop, temp cleanup und Branch-Merges. Alte und neue Konvention dürfen sich nicht unmarkiert mischen. Nicht jedes Retain eliminieren; Referenzbilanz pro Besitzer prüfen. Neue Pfade vor Auslieferung vollständig über die relevante Matrix abdecken.
5. **Borrows und Temp-Lifetime absichern.** Lokal erkennbare Invalidierung über Containeraliase, verworfene/weitergereichte pop-Ergebnisse, Return/early-exit und Short-circuit testen. Kein temporärer Owner darf vor dem letzten gültigen Borrow-Verbrauch droppen.
6. **Kind-2-Release aktivieren und Dispatcher vereinheitlichen.** Vorhandenen `free_list_elem_at` auch für final-drop und remove nutzen, bisherige doppelte Spezialpfade entfernen. pop-transfer und pop-discard trennen; len/aktive Slots verhindern erneuten Drop. Erst jetzt Null-Leak-Ziele 49 und 50 sowie clear/pop/remove-Matrix verbindlich grün fordern.
7. **Gesamtabnahme gegen Baseline und Spielverhalten.** Kein neuer Korpusfehler/Skip, komplette relevante Lifetime-Safety- und Leak-Gates, genannte 9rooms-Regressionen, WASM-Build und dokumentierter Client-/Combat-Smoke. Nicht ausführbare Gates bleiben ausdrücklich offen; keine Freigabe allein aufgrund nativer Prototypen.

**Abgrenzung:** Die früher berichteten 422/461, die Deck-Editor-Regression, der konkrete Stage-A-Fehler und der Serververlust von ~540 B/Zyklus wurden in diesem Review nicht erneut reproduziert. Sie bleiben Berichte aus §10 und dürfen nicht als hier unabhängig bestätigte Kausalnachweise gelesen werden. Die fünf oben aufgeführten Probe-Ergebnisse wurden dagegen neu gemessen. Es wurden keine Compiler-/Runtime-Fixes vorgenommen.

---

## 12. CopyRetain-Umsetzungsversuch — Befunde (Claude, Nacht 2026-09-24/25)

Ernsthafter Umsetzungsversuch der decl-CopyRetain (Codex §11.3). Ergebnis: **kein committable-grüner Stand erreicht; reverted.** Die Befunde (verifiziert, wertvoll für die Fertigstellung):

1. **Stage A (CopyRetain an JEDEM struct-memcpy-from-pointer-Init, ast_compiler_stmt.cpp ~1991) → mem-sanitizer FAIL.** Isolierte Ursache: die Probe `producer_set_view_return` leakt 22,9 MB, weil der Init dort auch **Producer-Call-Ergebnisse** (`decl brd = make_board(idx)`, owns_return) trifft — die sind BEREITS owned/adoptiert; ein zusätzliches `aiml_ser_retain` über-increft sie. Bestätigt Codex P1: „producer/pop/clone-Ergebnisse werden adoptiert, nicht retained".

2. **Stage B (CopyRetain nur wenn `sym.is_view_alias`, d.h. echter at/get/member-View) → mem-sanitizer PASS.** Das Gate schließt Producer/clone/owns-return korrekt aus; `producer_set_view_return` passt wieder; KEINE Escape-double-frees (alle Escape-Probes grün). Das view-gated retain ist also memory-safe.

3. **ABER: `aiml_ser_retain` wird für `decl copy = at(rows,0)` GAR NICHT emittiert** (objdump: 0 Vorkommen im test-50-.o). D.h. der at-View-Value-Model-decl **erreicht den 1991-struct-memcpy-from-pointer-Zweig NICHT** (dort ist `initRaw->isPointerTy()` offenbar false für at-Value-Model-Elemente, oder ein anderer Store greift). → **Meine CopyRetain-Stelle ist die falsche.** Stage B war für die eigentlichen Views ein No-op (traf nur Producer-decls, die es korrekt skippt). Deshalb blieben 49/50 unverändert rot.

4. **freshOwned-View-Fall + `CompilerContext::isMoveOnlyModel` (geteilt Codegen↔Analyzer) sind korrekt formuliert + bereit**, aber INERT ohne das retain im richtigen Pfad (freshOwned gated nur einen REGISTRIERTEN Drop; ohne CopyRetain-Block wird kein Drop registriert → harmlos, aber wirkungslos).

**Konkreter nächster Schritt (für die Fertigstellung):** den echten Materialisierungs-Pfad für `decl <value-model> = {at/get/first/last}` finden — prüfen, was `visitExpression(AtExpr)` für ein Value-Model-Element zurückgibt (Element-ADRESSE als Pointer vs. geladener Wert) und wo der Store in den decl-Slot passiert. CopyRetain (memcpy + `aiml_ser_retain` + owned-Drop-Registrierung, gated auf is_view_alias && !isMoveOnlyModel) DORT platzieren; freshOwned-View-Fall dann wieder aktivieren (muss exakt matchen). Danach das Gate aus §11.3 Schritt 4-7 (inkl. Game+Client-Verhaltenstest — headless NICHT möglich). Bis dahin bleibt elemKind-2-Release deferred; die sicheren Kinds (1/3/4/5/6) + owned-String-Return sind bereits committet + grün.

Verifikations-Artefakte dieser Nacht: `tests/run_lifetime_leak_gates.sh` (hartes Leak-Gate, committet), rote Ziele 49 (47/16 B) / 50 (16 B, Feld-Reassignment auf View) / p2_array (6780 B) reproduzierbar; GREEN-Prototypen p2/p3 0/0.
