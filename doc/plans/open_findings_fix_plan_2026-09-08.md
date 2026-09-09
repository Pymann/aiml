# Offene Findings: konkreter Restarbeitsplan

Stand: 2026-09-08. Zeilenangaben beziehen sich auf den während der Prüfung gelesenen, laufend bearbeiteten Arbeitsbaum; Funktionsnamen dienen als stabile Anker. Dies ist ein Implementierungsplan, kein Nachweis ausgeführter Tests. Keine Runtime-/Compiler-Änderungen im Rahmen dieser Prüfung.

## Maßgeblicher Lifetime-Ansatz (Korrektur 2026-09-08)

WP-C ist laut Nutzer verworfen. Maßgeblich sind `spec/61_lifetime_ownership.md` (noch als PROPOSAL markiert) und `doc/plans/lifetime_qualifier_2026-09-08.md`: lokal bestimmte Single-Owner-Semantik mit `scope`/`persistent`/`consumed`, ohne Ganzprogramm-Escape-Analyse und ohne Runtime-Drop-Flags. Dieser Restarbeitsplan reaktiviert WP-C nicht. Die Decoder- und KV-Befunde sind davon unabhängig; ihr Implementierungsstatus muss wegen laufender Änderungen jeweils neu geprüft werden.

## 1. Bereinigter Status

- F1: Die ursprünglich beanstandeten Skalare und Stringlängen sind korrigiert (`src/sys/ser_runtime.cpp:164–174,319–354`). Der Decoder insgesamt ist noch nicht freigegeben: Restlücken siehe P0.1 und P0.2.
- F2: Die Aussage „keine Allokationen“ in der Spec-Tabelle ist korrigiert. Eine widersprüchliche Runtime-Zusammenfassung bleibt; siehe P2.
- F3: Fehlerpfad-Abdeckung und Rückgabewertprüfung in der Messschleife fehlen weiterhin; siehe P0.4.
- F4/F5: Die konkret gemeldeten Widersprüche und die pauschale UAF-Sicherheitsbehauptung sind im Ownership-Dokument korrigiert. Das ist keine Abnahme der noch zu implementierenden Ownership-Semantik.
- F6: Die beanstandete `exodus-asan.json` existiert nicht mehr. Offen bleibt der Nachweis, dass auch Speicherzugriffe im erzeugten AIML-Code instrumentiert werden; siehe P1.2.

## 2. P0.1 — Tatsächliche Blob-Grenze an den Decoder übergeben

**Stellen:** `src/sys/ser_runtime.h:84` (`aiml_ser_decode`), `src/sys/ser_runtime.cpp:719–760`; `src/sys/db_runtime.cpp:117–120` (`AimlKvValueLoc`), `:1678–1710` (`aiml_db_kv_get_copy_at`).

**Befund:** Der Decoder erhält einen Pointer, aber keine tatsächliche Speicherlänge. Er liest bereits das vier Byte lange Präfix ungeprüft und verwendet dessen Wert als Grenze. Die neuen Feldprüfungen schützen damit nicht vor einem Präfix, das mehr Bytes behauptet, als der gespeicherte Wert enthält. Ein konkreter Crash wurde in dieser Prüfung nicht ausgeführt.

**Änderungsvorschlag:**

1. Einen neuen internen, längengeprüften Einstieg ergänzen, z.B. `int aiml_ser_decode_checked(const char* blob, size_t blob_size, void* out, const AimlTypeNode* nodes, int32_t n_nodes)`. Das bestehende ABI zunächst erhalten; die unsized API darf für untrusted gespeicherte Daten nicht der Prüfpfad bleiben.
2. Vor Präfixzugriff `blob_size >= 4` prüfen. Danach `data_len >= 0` und `data_len <= blob_size - 4` prüfen. Bestehenden Vertrag für leere Payloads zunächst erhalten; nicht beiläufig Formatsemantik ändern.
3. Fingerprint/Guard nur innerhalb des so geprüften Payloadbereichs lesen. Bestehende Legacy-Kompatibilität erhalten; kein neues Pflicht-Guard-Format in diesem Fix.
4. KV-Aufrufer geben `loc.length` bzw. `it->second.size()` mit. Für WAL zusätzlich vor Pointerbildung `offset <= wal_mmap_size` und `length <= wal_mmap_size - offset` sicherstellen. Keine überlaufgefährdete Addition als Prüfung verwenden.
5. Interne Positions-/Längenprüfungen auf geprüfte Restlängen umstellen: `needed <= end - pos` nach `pos <= end`; insbesondere `r_i32`, Skalare und Strings. `pos + len` kann derzeit als signed Addition überlaufen.
6. Weitere Decoder-Einstiege erfassen: Slot-Decode (`db_runtime.cpp:290`), Deep-Copy (`ser_runtime.cpp:715`), Mirror (`mirror_runtime.cpp:424,1110,1382`). Dort verfügbare echte Frame-/Bloblängen bis zum checked Einstieg führen. Das sind zusätzliche Auditstellen; ihre vollständigen Fehlerverträge wurden hier nicht geprüft.

**Abnahme:** Präfixlängen 0–3 Byte, negative/überhöhte Payloadlänge und überhöhte Stringlänge führen kontrolliert zum Fehler; keine Out-of-bounds-Zugriffe unter ASan/UBSan. Gültige Guard- und unterstützte Legacy-Blobs bleiben lesbar.

## 3. P0.2 — Restliche Decode-Zweige schließen

**Stellen:** `src/sys/ser_runtime.cpp`, Funktion `dec`.

| Stelle | Aktueller Fehler | Konkrete Änderung |
|---|---|---|
| `:419–423`, MAP | `if (pos >= end) break` meldet trotz angekündigter weiterer Einträge keinen Fehler. | Bei fehlendem erforderlichem Eintrag Fehler setzen und abbrechen. Falls Null-Byte-Typen erlaubt sind, deren gültige Darstellung berücksichtigen, statt pauschal Payloadbytes zu verlangen. |
| `:428–445`, BUFFER | Negative Größe wird übernommen; Allocation erfolgt vor Payloadprüfung; fehlende Daten setzen keinen Fehler. Cast von `bytes` nach `int` kann die Prüfung verfälschen. | Größe und Stride prüfen, Multiplikation auf Überlauf prüfen, vollständige Payload vor Allocation verlangen. Bei Fehler Buffer leer lassen und Fehler setzen. Kein Cast auf `int` zur Bereichsprüfung. |
| `:449–454`, RAWBYTES | Unvollständiger Bereich wird still übersprungen. | Ungültige Länge bzw. fehlende Bytes als Fehler melden; nur nach Restlängenprüfung kopieren. |
| `:456–460`, PTR_STRUCT | Fehlendes Markerbyte wird wie gültiges Null behandelt. | Fehlendes Markerbyte als Fehler melden. Markerwerte entsprechend Encodervertrag validieren; gültiges Null separat behandeln. |

Zusätzlich LIST/MAP-Rekursion (`:377–424`) bei bereits gesetztem Fehler beenden, damit beschädigte Counts nicht unnötig weitere Decode-Allokationen verursachen. Dabei Freigabeinvarianten erhalten: Pointer nullinitialisiert, MAP-occupied nur für freigebbare Slots gesetzt, Teilobjekte vollständig mit `aiml_ser_free_decoded` abräumbar. Nicht nur Schleifen abbrechen und Cleanup ungeprüft lassen.

**Abnahme:** Für jeden genannten Typ mindestens ein gültiger Grenzfall und ein abgeschnittener/ungültiger Fall; Fehler auch dann erkennen, wenn das betroffene Feld das letzte Feld des Objekts ist. Bereits dekodierte verschachtelte Felder nach Fehler ohne Leak/Double-Free freigeben können.

## 4. P0.3 — Fehlerhafte Decode-Ergebnisse nicht veröffentlichen

**Stellen:** `src/sys/db_runtime.cpp:1576,1597` (`aiml_db_kv_get`), `:1637,1651` (`aiml_db_kv_get_copy`), `:2520` (Indexaufbau). Referenz für vorhandene Fehlerbehandlung: `:1698–1708` (`aiml_db_kv_get_copy_at`).

**Befund:** Die fünf genannten Aufrufe ignorieren den Decode-Status. Teilweise/ungültig dekodierte Werte können in den Cache gelangen und als gültiger Pointer oder Indexquelle weitergereicht werden.

**Änderungsvorschlag:**

- Decode jeweils in einen nullinitialisierten temporären Slot durchführen. Nur bei Erfolg in den Cache übernehmen und LRU aktualisieren.
- Bei Fehler partiellen Heap deep-freigeben; keinen scheinbar gültigen Cacheeintrag hinterlassen. `get`/`get_copy` geben `nullptr` zurück. AIML-Aufrufer/Codegen auf diese Fehlerdarstellung prüfen; nicht durch einen Pointer auf ein leeres Objekt kaschieren.
- Indexaufbau: fehlerhaften Datensatz nicht an `extract_and_add` übergeben; Cleanup und nachvollziehbare Fehlerdiagnose. Ob der gesamte Aufbau abbrechen soll oder einen dokumentierten Teilindex liefern darf, am bestehenden Indexvertrag festmachen.
- `get_copy_at` behält `1 / 0 / -1` und die vorhandene Cleanup-/Nullungslogik, erhält aber den längengeprüften Einstieg aus P0.1.
- Separates Anschluss-Audit: `aiml_ser_deep_copy` (`ser_runtime.cpp:702–717`) ignoriert ebenfalls den Status und liefert `void`. Für fehlerfestes Deep-Copy/Replace nach Kapitel 61 ist ein checked Erfolgskontrakt nötig; keine breite Ownership-Umstellung in diesen Decoderfix mischen.

**Abnahme:** Fehlerhafte Daten werden weder gecacht noch indexiert. Nach Fehler funktioniert eine anschließende gültige Abfrage. Wiederholte Fehler erzeugen kein proportional wachsendes Live-Heap-Aufkommen.

## 5. P0.4 — Gezielt nachweisen, dass Fehlerpfade funktionieren

**Bestehende Stelle:** `tests/test_kv_get_copy_at.aiml:65–68,81–104`.

**Vorgeschlagene neue Dateien:** `tests/test_ser_decode_checked.cpp`, `tests/test_kv_get_copy_at_errors.cpp` (native Proben für gezielte Blob-Manipulation); ein gemeinsamer Runner kann in P1.2 entstehen. Kein Produktions-API zum Einspielen kaputter Blobs nur für Tests hinzufügen.

1. In `probe_reuse` den bisher ungenutzten `okr` prüfen: jeder andere Wert als `1` muss die Probe eindeutig fehlschlagen lassen, z.B. über einen zusätzlichen Fehlerzähler, den `main` auswertet. Ein negativer Messwert allein darf nicht als Fehlercode dienen, da RSS-Differenzen auch regulär negativ sein können.
2. Beim Refill auch `ok2` prüfen; aktuell werden nur die Folgedaten geprüft.
3. Native Decoderfälle für P0.1/P0.2 einschließlich Schema-Mismatch und Fehler nach bereits alloziertem String/Listenteil.
4. KV-Integration für persistent und in-memory: Erfolg = `1`; missing = `0` und unverändertes Ziel; korrupt/schemafremd = `-1` und vollständig bereinigtes, genulltes Ziel; danach wieder gültiges Refill = `1`.
5. Fehlerpfade über `get`/`get_copy`/Index aus P0.3 ebenfalls prüfen. Für Persistenz ausschließlich eine temporäre Test-WAL verwenden.
6. Erfolgs- und Fehlerzyklen N/2N unter Leakprüfung vergleichen. Vorhandene RSS-Probe ergänzend behalten; flaches RSS allein ist kein Speichersicherheitsnachweis.

**Gate:** P0.1–P0.4 müssen vor dem Server-Live-Test bestanden sein. Tests sind hier geplant, nicht bereits geschrieben oder ausgeführt.

## 6. P1.1 — Server-Nachmessung vor großer Ownership-Arbeit

**Stellen:** `../9rooms_v2/sources/match/match_scheduler.aiml:37–63`, `../9rooms_v2/sources/match/match_service_base.aiml:855`; bestehender Hauptplan `memory_lifetime_fix_plan_2026-09-07.md:193–196`.

Der Scheduler prüft bereits `tok == 1` und markiert bei `-1` den Schlüssel zum Überspringen. Diese vorhandene Mitigation nicht erneut implementieren.

Nach bestandenem Decode-Gate mindestens ca. 50 Minuten mit dokumentiertem, vergleichbarem Match-/Verbindungsprofil messen. RSS-Verlauf, Live-Heap wenn verfügbar, Match-/Spieler-/Verbindungszahlen, Decodefehler und tatsächliche erfolgreiche Ticks erfassen. Undekodierbare Datensätze dürfen weder getickt noch zurückgeschrieben werden. Flaches RSS bei ausgefallener Spiellogik zählt nicht als Erfolg.

Vor dem Lauf Warmup und Bewertungskriterium für Restwachstum festhalten. Bestätigte Stabilität bewertet die Wirksamkeit der Scheduler-Mitigation; sie ersetzt nicht die Sicherheitsabnahme einer späteren Umsetzung von Kapitel 61. WP-C bleibt verworfen. Ergebnis mit Binary-/Quellstand und Messdaten als neuen Bericht unter 9rooms `doc/analysis/memory_checks/` ablegen.

## 7. P1.2 — Sanitizer-Abdeckung des erzeugten Codes nachweisen

**Stellen:** `compiler.cpp:1655–1699` (`compileToObject`, bislang reine Codeausgabe ohne erkennbaren ASan-Instrumentierungspass); `main.cpp:165–193` (vorhandener `.ll`-Export einschließlich Imports und optionalem Entry-Point). Build-Flags allein instrumentieren nicht automatisch AIML-generierte LLVM-Loads/Stores.

**Minimaler Vorschlag:** Neuer Runner `tests/run_memory_sanitizers.sh` für native AOT-Proben:

1. Vorhandenen LLVM-IR-Export einschließlich importierter Module und benötigtem Entry-Point verwenden.
2. Alle erzeugten Module mit zur Projekt-LLVM-Version passender Toolchain explizit ASan-instrumentieren. Pass-/Funktionsattribute anhand der tatsächlich installierten LLVM-Version festlegen; `-fsanitize` beim bloßen Linken genügt nicht.
3. C++-Runtime ebenfalls mit ASan, Debugsymbolen und Frame-Pointern bauen und passende Sanitizer-Runtime linken; LSan aktivieren. Decoderarithmetik zusätzlich unter UBSan prüfen.
4. Instrumentierung durch IR-/Objektprüfung UND eine isolierte, absichtlich fehlerhafte Kontrollprobe belegen: ein direkter Speicherzugriff aus erzeugtem AIML-Code muss einen ASan-Fehler auslösen. Die Kontrollprobe läuft separat mit erwartetem Fehlerstatus.
5. Danach die echten Proben ohne Sanitizerbefund und mit nachvollziehbaren N/2N-Live-Heap-Ergebnissen bestehen lassen.

Zunächst kein zwingender Umbau der produktiven JIT-Pipeline. Der AOT-Prüflauf belegt nur den instrumentierten nativen Pfad; für abweichende JIT-/WASM-Pfade keine identische Abdeckung behaupten. Falls ein Compiler-Schalter später benötigt wird, Instrumentierung vor `addPassesToEmitFile` vorsehen und Sanitizer-Modus im Import-Cache-Schlüssel berücksichtigen.

## 8. P2 — Kleine Dokumentationskorrekturen

- `spec/31_db_integration.md:826–829`: nach Decode Erfolg `1`, bei Fehler Teil-Heap freigeben + Ziel nullen + `-1`; missing unverändert `0`.
- `src/sys/ser_runtime.h:81–83`: Rückgabevertrag korrekt angeben: `0` Erfolg, `-1` struktureller Fehler, `-2` Schema-Mismatch. Fehlerziel ist kein gültiger Wert; Cleanup-Verantwortung benennen.
- Optional angrenzende Kommentare in 9rooms (`match_scheduler.aiml:46–47`, `match_service_base.aiml:852`): „keine per-Tick-Allokation“ durch „wiederverwendete Hülle, verschachtelte Felder werden freigegeben und neu alloziert“ ersetzen.

## 9. Reihenfolge und Grenzen

P0.1 → P0.2 → P0.3 → P0.4 mit nativer Sanitizer-Prüfung → Server-Nachmessung. P2 kann im jeweiligen Änderungspaket mitlaufen. Vollständige AIML-Instrumentierung aus P1.2 ist spätestens vor Abnahme der Ownership-Änderungen verbindlich; sie soll den kleinen Runtimefix nicht zu einem Compiler-Großumbau machen.

Die neuen Decoder-/Aufruferbefunde sind aus dem aktuellen Quelltext abgeleitet. Ihre konkrete Auswirkung auf den beobachteten Server-OOM ist damit nicht bewiesen. Keine zusätzliche Sprachsemantikänderung ist Teil dieses Restarbeitsplans.

## 10. Offene Spezifikationsfragen in Kapitel 61

Quellstand der Kapitelprüfung: 2026-09-08, Kapitel mit 144 Zeilen. Dies sind Designlücken, keine nachgewiesenen Implementierungsfehler.

1. **`consumed` ohne Übergabe** (`:33,73–88,121–123`): Tabelle verspricht keinen Drop, Nicht-Konsum/uneinheitlicher Konsum sind nur Warnungen. Für einen weiter kompilierenden Pfad ist damit kein sicherer Cleanup festgelegt. Vorschlag: nie konsumierte Werte trotz Warnung wie `scope` droppen; divergierenden Besitz am Merge auch für `consumed` als Fehler behandeln, solange keine explizite statische Normalisierung definiert ist. Das behält die Vorgabe ohne Runtime-Flags bei.
2. **`persistent` braucht einen konkreten Besitzer** (`:25,32,61–62`): Ein lokales persistent-Objekt unterdrückt Drop, ohne anzugeben, wie es in den längerlebigen Besitzer gelangt. Copy an einen anderen Besitzer entsorgt das Original nicht. Vorschlag: persistent nur mit nachweislich gebundener Owner-Speicherung zulassen oder einen expliziten Release-Vertrag definieren; freie lokale Deklaration ohne Owner/Release ablehnen.
3. **Copy/Move ist widersprüchlich** (`:65–66,104–115`): Assignment ist einmal Deep-Copy und einmal optional Move; die Definition von „consumable“ fehlt. Außerdem Copy-Ausnahme für persistent (`:61`) gegen „owned box storing always move“ (`:112`). Vorschlag: eine verbindliche Tabelle für Zielart, Quellbesitz und Operation mit eindeutiger Rangfolge; Beispiele für lokale Zuweisung, Member-Set und persistente Box ergänzen. Kein implizites Last-Use-Verhalten voraussetzen.
4. **Borrow-Sicherheit innerhalb desselben Scopes** (`:43–46,100,124`): Escape-Verbot allein verhindert keinen ungültigen View nach `clear`, `remove`, Reallocation, Ersetzen oder Freigabe des Owners. Vorschlag: lokale Invalidierungs-/Mutationsregeln sowie Rückgabe und Weitergabe an Funktionen explizit festlegen. Dafür ist keine Ganzprogramm-Escape-Analyse nötig.
5. **Funktionsgrenzen fehlen** (`:43,55–56,133–135`): consuming Parameter werden erwähnt, aber ihre Deklaration und ihr Verhältnis zu by-ref Listen/Maps und by-value Modellen nicht definiert. Vorschlag: Signaturvertrag für borrow/consume/copy und Rückgaben festlegen; insbesondere dürfen by-ref Parameter nicht durch die allgemeine Regel „Wertetyp = owned“ am Callee-Ende gedroppt werden.

Sanitizer-Abdeckung bleibt auch für diesen neuen Ansatz relevant; der neue Lifetime-Plan fordert sie selbst in §8. Aus diesen Fragen folgt keine Rückkehr zum verworfenen WP-C.
