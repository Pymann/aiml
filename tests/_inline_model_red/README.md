# Rote Vertrags-Tests: inline `list<Model>`-Element-Release (Paket 1)

Gehört zu `doc/spec61_inline_value_model_release_plan_2026-09-24.md`. Diese Tests kodieren den
Ziel-Vertrag (Spec 61 §5.4) für inline Value-Model-Elemente. Sie liegen bewusst unter `_inline_model_red/`
(Präfix `_` → vom Korpus `run_all_tests.sh` ausgeschlossen), weil sie **RED bis Paket 5** sind und den
grünen Korpus nicht brechen sollen. Nach Abschluss von Paket 3+5 wandern die grünen Fälle nach
`tests/lifetime/[0-9]*` (Content) bzw. werden mit hartem Leak-Gate in die mem-sanitizer-Abnahme
aufgenommen.

## Ausführen
```sh
# Content (JIT):
AIML_MEM_V3=1 AIML_LIFETIME_DROPS=1 out/aiml/aiml -r tests/_inline_model_red/49_value_survives_remove.aiml
# Leak (standalone valgrind):
AIML_MEM_V3=1 AIML_LIFETIME_DROPS=1 out/aiml/aiml tests/_inline_model_red/49_value_survives_remove.aiml -o /tmp/r/main.o
g++ /tmp/r/*.o -Lout/libaiml -laiml -lyaml-cpp -lpthread -lssl -lcrypto -ldl -Wl,-rpath,$PWD/out/libaiml -o /tmp/r/bin
AIML_MEM_V3=1 AIML_LIFETIME_DROPS=1 valgrind --leak-check=full /tmp/r/bin
```

## Befund bei Erstellung (2026-09-24, aiml @ pop/clear-Fix-Stand)

| Test | Content (JIT) | Leak (valgrind) | Bedeutung |
|---|---|---|---|
| `49_value_survives_remove` | **PASS** | **RED: 47 def + 16 indirect lost** | Die Kopie überlebt `remove` inhaltlich — ABER das entfernte elemKind-2-Element wird nicht freigegeben (remove defert kind 2). |
| `50_share_semantics` | **PASS** | (n/a) | Container-shared + Scalar-independent ist BEREITS korrekt (is_view_alias + copy-on-write-at-set). |

## Zentrale Erkenntnis (empirisch, für die Umsetzung wichtig)

Der Inhalt in Test 49 ist heute **nur deshalb** grün, weil `remove`/`clear` das Value-Model-Element
NICHT freigeben (der Leak IST der Grund, warum die geteilte View gültig bleibt). Ein **naives**
Aktivieren des Element-Release (Paket 5 ohne Paket 3) würde die View danglen lassen → Content bräche
(double-free/UAF). Das bestätigt die Plan-Sequenz: **erst CopyRetain-Materialisierung (Paket 3), dann
Release (Paket 5).** Test 49 verlangt am Ende BEIDES zusammen: PASS + 0 lost.

Re-Scoping-Hinweis: Share-Semantik (§4) und Content-Korrektheit sind bereits erfüllt; die eigentliche
offene Arbeit ist der elemKind-2-Release-Leak plus die dafür nötige eigenständige (retain-)Kopie.

## Paket 2 — Retain/Release-Audit (2026-09-24)

| Test | Ergebnis | Bedeutung |
|---|---|---|
| `p2_retain_coverage` | **GRÜN** (PASS + 0 lost + 0 errors, 200 Iter) | `aiml_ser_retain` co-owned string + list<i32> + nested Value-Model + map korrekt über den bestehenden `store: [ref<Model>, at(list,i)]`-Pfad. Linchpin validiert für die gängigen Feldarten. |
| `p2_array_field_leak_RED` | **RED** (6780 B / 400 Blöcke lost, kein double-free) | `array<string:N>` als Model-Feld → serFieldSubtree RAWBYTES → retain/release sehen die Elemente nicht → reiner Leak. §9.6-Lücke bestätigt + quantifiziert. |

**Offene Paket-2/3-CODE-Arbeit (mit Risiko):** (a) Array-Feld-Lifecycle-Descriptor (STRUCT-Komposition) — ⚠️ ändert serFieldSubtree → **Schema-fp/Wire-Risiko** (wie die matches-KV-FATALs); separaten Lifecycle-Descriptor erwägen. (b) `isCopyableValue`-Trait aus `modelIsMoveOnly` ableiten (§9.5). (c) CopyRetain-Materialisierung bei decl/set/return/push verdrahten (§9.2/§9.3, Paket 3) — der breite ABI-Eingriff, braucht das volle Verhaltens-Gate.

## Paket 3 — CopyRetain-Mechanik isoliert BEWIESEN (2026-09-24)

| Test | Ergebnis | Bedeutung |
|---|---|---|
| `p3_copyretain_survives_source_drop` | **GRÜN** (PASS + 0 lost + 0 errors, 200 Iter) | Eine retain-Kopie (memcpy + `aiml_ser_retain` via store-retain-Pfad) überlebt die kind-2-FREIGABE des Quell-Listen-Elements (aiml_list_free scope-drop = dieselbe `aiml_ser_free_decoded`, die remove/clear nutzen) mit intaktem Inhalt (string/list/map/nested-model) und balancierten rc — **kein double-free, kein Leak.** |

**Fazit:** Die Runtime-Mechanik (retain-Kopie ⊕ Element-Release) ist risikofrei validiert. Der frühere remove-Deep-Free-Regress lag NICHT an der Freigabe, sondern an der fehlenden retain-Kopie (View). Verbleibende (riskante) Arbeit: CopyRetain in den IMPLIZITEN `decl copy = at(..)`-Pfad verdrahten (statt is_view_alias) + remove/clear/pop-Release aktivieren — betrifft die Kern-Strukturen (ZoneEntry/MockCard/ActiveAbility), braucht das volle Game+Client-Verhaltens-Gate.

## Paket 3 — Stage-A-Versuch (decl-only CopyRetain) VERWORFEN (2026-09-24)

Versuch: CopyRetain nur am `decl copy = at(..)`-Init emittieren (memcpy → `aiml_ser_retain` +
owned-Drop + `is_view_alias=false`), Escape-Sinks (set/idx/return/push) UNVERÄNDERT.
**Ergebnis: mem-sanitizer FAIL (1 Check).** → **Empirischer Beweis: decl allein reicht NICHT.**
Die Escape-Pfade (ast_compiler_stmt.cpp 2313/2471/3655, 4662, ast_compiler_call.cpp:1545 push) haben
eigene is_view_alias-/deep-copy-Logik; wenn decl die Kopie zu owned+retain macht, widersprechen die
Escapes (doppeltes retain/copy oder falscher Move → double-free/Imbalance). Bestätigt Plan §3/§4:
Value-Materialisierung muss über ALLE Sinks GLEICHZEITIG konsolidiert werden (decl + set + idx +
return + push + store), mit einheitlichem Ownership-Fakt statt der is_view_alias-Heuristik. Stage A
wurde reverted (grüner Stand wiederhergestellt). Die eigentliche Paket-3-Umsetzung ist damit ein
koordinierter Multi-Sink-Umbau — kein isolierter decl-Patch.
