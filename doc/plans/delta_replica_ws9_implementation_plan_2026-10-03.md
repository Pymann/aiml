# DELTA, Replika-Ownership und WS9: konkreter Implementierungsplan

Stand: 2026-10-03. Status: **Entwurf zur Implementierung, kein implementierter Fix**.
Ergänzt Abschnitt 11 von `rpc_implementation_fix_plan_2026-10-02.md`.

Die Snippets sind konkrete Änderungsvorschläge. Neu eingeführte Funktionen sind
als solche bezeichnet; sie sind nicht als bereits verfügbare APIs zu verstehen.
Zeilennummern sind Orientierungen, Funktionsnamen die stabilen Suchanker.

## 1. Festgelegtes Verhalten

1. DELTA wird vollständig, begrenzt und transaktional decodiert. Keine teilweise
   Übernahme bei Fehlern; Schemaabweichung ist kein erfolgreicher Snapshot.
2. Mirror **borgt** das vom AIML-Aufrufer initialisierte Objekt. Registrierung
   und Session-Ende zerstören dessen Felder nicht. Das Objekt muss bis
   Unregister/Close leben. Der normale AIML-Lifetime-Pfad bleibt Eigentümer.
3. Bereitschaft wird durch einen erfolgreich übernommenen Snapshot angezeigt,
   nicht durch REGISTER, eine Pointerbelegung oder die Zahl der `update`-Aufrufe.
4. Der WS9-Test wartet auf Snapshot + erwartete Werte und beendet den Host erst
   nach einer expliziten Empfangsbestätigung des Clients.
5. Modellversionsmigration bleibt außerhalb dieses Fixes. Gleiche Wire-Schemata
   aus verschiedenen Compiler-Kontexten sind unterstützt; verschiedene Schemata
   werden abgewiesen.

## 2. Änderungsmatrix

| Datei / Anker im aktuellen Stand | Änderung |
| --- | --- |
| `src/sys/ser_runtime.cpp`: `aiml_ser_encode`, `aiml_ser_rpc_fp`, `aiml_ser_decode_sized_tx` (~1539) | explizite Wire-Encode-API und zwingende Envelope-/Schema-Prüfung für DELTA |
| `src/sys/ser_runtime.h:92` | neue Deklarationen und eindeutiger Ownership-Vertrag |
| `src/sys/mirror_runtime.cpp`: `AimlMirrorRegistered` (~163) | Snapshot-Zähler und Fehlerstatus statt `has_decoded` als Ownership-Schalter |
| `mirror_packet_length` (~440) | markiertes DELTA-Format erkennen, unbekannte Flags ablehnen |
| `send_delta_to_peer` (~520), `send_delta_pkt` (~545), `session_update` (~1126), `register_id_typed` (~1312) | einheitliches Encoding inklusive Dirty-Tracking |
| `process_packet`, DELTA (~590), Pending-Blob-Apply (~1287) | gemeinsamer transaktionaler Apply-Helper |
| `PKT_UNREGISTER` (~674), `session_close` (~1057), `session_unregister` (~1234) | nur Registrierungsressourcen freigeben, alle Lookup-Einträge entfernen |
| `src/core/builtin_registry.cpp`: `MIRROR_FIND_OBJ_PTR`, `register`, `register_id` (~9920–10065) | echtes langlebiges Objekt binden; alle serialisierbaren Modelle typisiert registrieren; Status-Builtins |
| `src/core/ast_compiler.cpp`: `initClassContainerMembers` (~131), globale Initialisierung (~1595/~1783) | Anfangszustand untersuchen und gezielt an der belegten Ursache korrigieren |
| `src/core/ast_compiler_stmt.cpp`: Modellinitialisierung (~1711), Defaults (~2078), ScopeDrop | genau einmal gültig initialisieren und später korrekt freigeben |
| `tests/test_mirror_ws_9rooms_imports.aiml` | Zustandsmaschine, ACK, keine ungeschützten Host-Global-Reads |
| `tests/ws_context_*`, `test_mirror_ws_shared_module_context_mismatch` | positive Kontext- und negative Schema-Regression trennen |

## 3. Zuerst den Anfangszustand isolieren (Commit 1)

Die ursprüngliche Initialisierungsursache ist noch nicht bewiesen. Kein pauschales
`memset` und keine erneute Initialisierung in `register_id` einbauen: beides kann
lebende Strings/Container überschreiben und Leaks verdecken.

### 3.1 Minimaltest vor jeder Mirror-Operation

Neues `tests/test_mirror_replica_defaults.aiml` mit demselben Modell wie der
Problemfall: String ohne Default, String mit Default, Liste, Map, verschachteltes
Modell. Vor dem ersten Netzwerkaufruf `.len`/Defaults prüfen, dann serialisieren
und Scope verlassen. Varianten: lokale Variable, importiertes Global,
`ptr<Model>`, aktivierte Refcount-Repräsentation. Bestehende Refcount-Schalter
verwenden; keine neue Modellrepräsentation für Mirror einführen.

Erzeugtes LLVM-IR auf diese Reihenfolge prüfen:

```cpp
// Soll-Reihenfolge im bestehenden Value-Modell-Konstruktionspfad.
// Nur bei ERSTKONSTRUKTION, nicht bei register_id oder bei jedem DELTA.
ctx.builder.CreateStore(llvm::Constant::getNullValue(modelTy), objectAddr);
initClassContainerMembers(objectAddr, modelName);
// Explizite Defaults nur einmal anwenden; vorhandene String-Decrefs beachten.
// Anschließend ScopeDrop für dieselbe Adresse und denselben Deskriptor.
```

`initClassContainerMembers` initialisiert Strings und Container bereits. Deshalb
zuerst überprüfen, ob der Aufruf fehlt, durch einen späteren Store überschrieben
wird oder auf die falsche Adresse zeigt. Bei Globals prüfen, dass der Initializer
vor Start eines Host-Threads ausgeführt wird. Bei `ptr<>`/Refcount-Modellen muss
Mirror die Objektadresse erhalten, nicht die Adresse des Pointer-Slots.

### 3.2 Temporäre Receiver ausschließen

`MIRROR_FIND_OBJ_PTR` erzeugt derzeit für manche Ausdrücke ein temporäres Alloca.
Eine langlebige Registrierung darf dieses temporäre Objekt nicht behalten.
Makro durch einen gemeinsamen Helper für beide Builtins ersetzen:

```cpp
// Neuer Compiler-Helper, schematische Signatur (Symbol-Lookup wiederverwenden).
struct MirrorObjectBinding {
    llvm::Value* address;
    std::string modelType;
    uint64_t size;
};
// resolveMirrorObjectBinding(ctx, expr):
// - nur nachweislich adressierbares, bis Unregister/Close lebendes Objekt;
// - Symbol-Typ normalisieren (ptr/ref/Refcount);
// - bei Pointer-Slot genau einmal laden;
// - Größe aus DataLayout und konkretem Modelltyp;
// - temporäre Modellwerte ablehnen, niemals still reg_tmp registrieren.
```

Bei initialisierten Value-Modellen gilt: Registrierung nimmt keine zusätzliche
Eigentümerschaft an Heapfeldern an. Bei expliziten Pointer-/Borrow-Varianten
Lifetime-Analyse so erweitern, dass ein Objekt nicht vor der Registrierung endet.
Komplexe nicht belegbare Lebensdauern mindestens mit klarer Diagnose begrenzen.

**Gate:** Default-/Teardown-Test unter ASan bzw. bestehendem Memory-Sanitizer
muss bestehen, bevor in Commit 3 `release_old = 1` verbindlich wird.

## 4. Ein markiertes, schemageprüftes DELTA-Format (Commit 2)

### 4.1 Wireformat und Kompatibilität

Für `PKT_DELTA` wird `reserved = 2` definiert:

```text
[net_id:u32 LE][type=0x01:u8][format=2:u8]
[payload_len:u32 LE][wire_magic:u32 LE][schema_fp:u32 LE][encoded fields...]
```

Wiederverwendung des vorhandenen strukturellen RPC-Envelopes
(`0xA1F5C0DF`, Fingerprint, Payload), ausdrücklich auch für DELTA dokumentiert.
Keine Änderung persistierter KV-Blobs. Der Marker unterscheidet neue DELTAs von
alten ungetaggten `reserved=1`-Paketen.

Dies ist ein **koordiniertes Client-/Server-Upgrade**. Alte Binaries sind nicht
kompatibel und können Marker 2 als Raw-Daten fehlinterpretieren. Deployment daher
nur mit gemeinsam aktualisierten Peers. Ein Mischbetrieb ist nicht Teil dieses
Fixes; falls benötigt, muss vorher eine separate DELTA-Capability ausgehandelt
werden. RPC-Version 3 allein beweist keine Unterstützung von DELTA-Format 2.
Kein automatisches Best-effort-Fallback.

Für Modelle auf dem neuen typisierten Pfad werden `reserved=0/1` abgewiesen.
Raw-DELTA bleibt allenfalls für die explizite Legacy-C-API; es wird nicht als
schemaüberprüft oder plattformunabhängig dokumentiert.

### 4.2 Serializer-APIs

Neue Deklarationen hinter `AimlTypeNode` in `ser_runtime.h`:

```cpp
char* aiml_ser_encode_wire(const void* obj,
                          const AimlTypeNode* nodes, int32_t n_nodes);
int aiml_ser_decode_delta_tx(const char* blob, size_t blob_size,
                            void* out, size_t out_size,
                            const AimlTypeNode* nodes, int32_t n_nodes);
```

Implementierung in `ser_runtime.cpp`; nutzt den vorhandenen Envelope-Modus und
stellt ihn auch bei C++-Exceptions wieder her:

```cpp
extern "C" char* aiml_ser_encode_wire(const void* obj,
                                     const AimlTypeNode* nodes, int32_t n) {
    struct RestoreMode {
        int previous = aiml_ser_set_rpc(1);
        ~RestoreMode() { aiml_ser_set_rpc(previous); }
    } restore;
    return aiml_ser_encode(obj, nodes, n);
}

// Private Hilfsfunktion in ser_runtime.cpp.
static uint32_t wire_u32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8)
         | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

extern "C" int aiml_ser_decode_delta_tx(
    const char* blob, size_t len, void* out, size_t out_size,
    const AimlTypeNode* nodes, int32_t n) {
    if (!blob || !out || !nodes || n <= 0 || out_size == 0 || len < 12)
        return -1;
    const auto* b = reinterpret_cast<const uint8_t*>(blob);
    if (size_t(wire_u32(b)) != len - 4) return -1;
    if (wire_u32(b + 4) != AIML_RPC_MAGIC) return -2;
    if (wire_u32(b + 8) != aiml_ser_rpc_fp(nodes, n)) return -2;
    // Vorbedingung: out ist ein vollständig initialisiertes, besessenes Modell.
    return aiml_ser_decode_sized_tx(blob, len, out, out_size, nodes, n, 1);
}
```

`aiml_ser_decode_sized_tx` liefert bereits vollständigen Konsum, Budget-/Depth-
Grenzen und einen temporären Decode. Die hier zusätzliche zwingende Magic-Prüfung
ist nötig, weil der bestehende allgemeine Decoder Legacy-Blobs akzeptiert.

**Schema-Grenzen ausdrücklich prüfen:** Der aktuelle Fingerprint beschreibt
Serializer-Kinds, nicht Feldnamen oder alle semantischen Typen (`i32`/`f32`
teilen einen Kind). Er ist kein Authentifizierungsmechanismus. Für diesen Fix
wird Wire-Schema-Kompatibilität garantiert, keine beliebige semantische
Modellgleichheit. Rekursive Deskriptoren (`PTR_REF`) dürfen nicht einfach nur nach
lokaler Type-ID verglichen werden: kanonisch auflösen oder für Sync zunächst
mit Diagnose ablehnen. Gleiches gilt für nicht serialisierbare Ressourcen.

### 4.3 Alle Sender-/Vergleichspfade gemeinsam umstellen

In `send_delta_to_peer`, `send_delta_pkt`, Dirty-Tracking von `session_update`
und Initialisierung von `shadow_blob` in `register_id_typed`:

```cpp
char* blob = aiml_ser_encode_wire(reg->obj_ptr, reg->ser_nodes, reg->ser_n);
if (!blob) return; // Im update-Loop: continue.
const size_t len = size_t(read_u32_le(
    reinterpret_cast<const uint8_t*>(blob))) + 4;
// len gegen dieselbe Frame-Obergrenze wie beim Empfang prüfen.
std::vector<uint8_t> pkt(6 + len);
write_u32_le(pkt.data(), uint32_t(reg->net_id));
pkt[4] = PKT_DELTA;
pkt[5] = 2;
memcpy(pkt.data() + 6, blob, len);
// Senden + vorhandene Shadow-Ownership übernehmen oder blob freigeben.
```

Nach Möglichkeit ein einziges `encode_delta(reg)`/`make_delta_packet` für alle
vier Stellen einführen. Dirty-Tracking muss exakt denselben Blob vergleichen,
der später gesendet wird; sonst erzeugt allein der neue Header Dauersynchronisation.

Compiler-Builtins `register` und `register_id`: Bedingung
`hasHeapFields && serNodesVar` durch `serialisierbares Modell && serNodesVar`
ersetzen. Auch reine Zahlenmodelle erhalten Format 2 und Schema-Prüfung.
Nicht serialisierbare Modelle diagnostizieren, nicht auf Raw-Memcpy ausweichen.

### 4.4 Parser

In `mirror_packet_length`, `case PKT_DELTA`, explizite Flags verwenden:

```cpp
if (d[5] == 2) {
    if (avail < 10) return more;
    const uint32_t payload = read_u32_le(d + 6);
    if (payload < 8 || payload > kMaxRpcFrame) return PKT_INVALID;
    t = 10 + size_t(payload);
    // Bestehender gemeinsamer Tail prüft vollständiges Paket und WS-Restbytes.
} else if (d[5] == 0) {
    // Expliziter bisheriger Raw-Legacy-Zweig, nicht als Fallback für typed.
} else {
    return PKT_INVALID; // einschließlich ungetaggtem Format 1 im neuen Protokoll
}
```

Raw→typed auch in `process_packet` abweisen, damit ein manipulierter Marker
nicht Heap-Pointer durch `memcpy` überschreiben kann. Typed→untyped ebenfalls
ablehnen. Pending-Daten speichern zusätzlich das Format; Marker 0 darf für eine
später typisiert gebundene Replika nicht versehentlich zum Anfangszustand werden.

## 5. Ein gemeinsamer Apply-Pfad und eindeutige Ownership (Commit 3)

### 5.1 Registrierungszustand

`AimlMirrorRegistered` erweitern, `has_decoded` als Ownership-Indikator entfernen:

```cpp
uint64_t snapshot_revision = 0; // lokale Anzahl erfolgreich übernommener DELTAs
int32_t last_decode_error = 0;  // 0=kein Fehler, -1=Decode, -2=Schema/Format
uint8_t pending_format = 0;
```

Ein Decode-Fehler nach einem gültigen Snapshot lässt Revision und Modell
unverändert. `last_decode_error` wird gesetzt; beim nächsten Erfolg gelöscht.
Revision ist lokal und kein Wire-Versionszähler; Überlauf kontrolliert behandeln.

### 5.2 Apply-Helper in mirror_runtime.cpp, vor process_packet

```cpp
static int apply_typed_delta(AimlMirrorRegistered& reg,
                             const uint8_t* blob, size_t len) {
    if (!reg.obj_ptr || reg.obj_size <= 0 || !reg.ser_nodes || reg.ser_n <= 0)
        return reg.last_decode_error = -1;
    if (reg.snapshot_revision == UINT64_MAX)
        return reg.last_decode_error = -1;

    const int rc = aiml_ser_decode_delta_tx(
        reinterpret_cast<const char*>(blob), len,
        reg.obj_ptr, size_t(reg.obj_size), reg.ser_nodes, reg.ser_n);
    reg.last_decode_error = rc;
    if (rc != 0) return rc;

    ++reg.snapshot_revision;
    if (reg.shadow)
        memcpy(reg.shadow, reg.obj_ptr, size_t(reg.obj_size));
    return 0;
}
```

Im Live-DELTA-Zweig bisheriges Free/Memset/Decode vollständig ersetzen:

```cpp
if (reg->ser_nodes) {
    if (reserved != 2) {
        reg->last_decode_error = -2;
        return; // kein Raw-Fallback und keine Weiterleitung dieses Fehlers
    }
    const int rc = apply_typed_delta(*reg, data + 6, size_t(data_len - 6));
    if (rc != 0) {
        fprintf(stderr, "[mirror] DELTA rejected net_id=%d rc=%d\n", net_id, rc);
        return;
    }
} else {
    // Nur tatsächlich untypisierte Objekte dürfen den Raw-Zweig erreichen.
}
```

Host-Relay darf einen lokal verworfenen Snapshot nicht trotzdem weiterleiten.
Bei Host-Platzhaltern ohne eigenes Schema lediglich begrenzte Weiterleitung
vorsehen; die endgültige Schema-Prüfung erfolgt beim typisiert gebundenen
Empfänger. Nicht behaupten, der Host habe solche Payloads bereits validiert.

### 5.3 Pending-Snapshot beim Binden

Nach `obj_ptr`, `obj_size`, `ser_nodes`, `ser_n` und neuem Shadow:

```cpp
// s->by_ptr vor Apply/Status-Abfrage setzen.
s->by_ptr[obj] = existing;
if (existing->pending_blob) {
    const int rc = existing->pending_format == 2
        ? apply_typed_delta(*existing, existing->pending_blob,
                            size_t(existing->pending_blob_len))
        : (existing->last_decode_error = -2);
    if (rc != 0)
        fprintf(stderr, "[mirror] initial DELTA rejected net_id=%d rc=%d\n", id, rc);
    free(existing->pending_blob);
    existing->pending_blob = nullptr;
    existing->pending_blob_len = 0;
    existing->pending_format = 0;
}
```

Kein Flag auf Erfolg setzen, falls Apply fehlschlägt. Vorliegende Defaults bleiben
lesbar. Bei Allokationsfehler für Pending-Buffer alten Pointer/Länge konsistent
halten; nicht nach einem fehlgeschlagenen `malloc` eine alte Länge stehen lassen.

### 5.4 Gemeinsames Freigeben einer Registrierung

Neuer Helper, aufgerufen von lokalem Unregister, Remote-UNREGISTER und Close:

```cpp
static void erase_registration(AimlMirrorSession& s, AimlMirrorRegistered* reg) {
    if (!reg) return;
    if (reg->obj_ptr) {
        rpc_obj_forget(reg->obj_ptr, &s);
        auto it = s.by_ptr.find(reg->obj_ptr);
        if (it != s.by_ptr.end() && it->second == reg) s.by_ptr.erase(it);
    }
    s.by_net_id.erase(reg->net_id);
    free(reg->shadow);
    free(reg->shadow_blob);
    free(reg->pending_blob);
    // obj_ptr und dessen Felder gehören dem AIML-Aufrufer.
    delete reg;
}

// session_close: keine Iterator-Invalidierung in der bestehenden range-for.
while (!s->by_net_id.empty())
    erase_registration(*s, s->by_net_id.begin()->second);
```

`send_unregister_pkt` vor lokalem Entfernen senden; Remote-UNREGISTER nicht
noch einmal als neuer lokaler Unregister broadcasten. Der bisherige Remote-Pfad
muss insbesondere den veralteten `by_ptr`-Eintrag und RPC-Objektlookup beseitigen.

Tests müssen beweisen, dass Modellfelder nach Close/Unregister noch lesbar sind
und beim Scope-Ende genau einmal freigegeben werden. Das unterscheidet gültige
Borrow-Semantik von bloßem Unterdrücken eines Double-Free.

## 6. Bereitschaft und Fehler aus AIML beobachten (Commit 4)

Neue Runtime-APIs in `mirror_runtime.h/.cpp`:

```cpp
uint64_t aiml_mirror_session_snapshot_revision(void* session, int32_t net_id);
int32_t aiml_mirror_session_snapshot_error(void* session, int32_t net_id);

uint64_t aiml_mirror_session_snapshot_revision(void* session, int32_t id) {
    auto* s = static_cast<AimlMirrorSession*>(session);
    if (!s) return 0;
    const auto it = s->by_net_id.find(id);
    return it == s->by_net_id.end() ? 0 : it->second->snapshot_revision;
}

int32_t aiml_mirror_session_snapshot_error(void* session, int32_t id) {
    auto* s = static_cast<AimlMirrorSession*>(session);
    if (!s) return -3; // ungültige Session/Registrierung
    const auto it = s->by_net_id.find(id);
    return it == s->by_net_id.end() ? -3 : it->second->last_decode_error;
}
```

0 Revision bedeutet noch kein erfolgreich angewandter Remote-Snapshot, auch für
lokale Owner; es ist kein Fehler. Kombination mit `snapshot_error` unterscheidet
Warten und Ablehnung. Zugriff nur aus dem Session-Thread, keine implizite
Thread-Sicherheit versprechen.

In `builtin_registry.cpp`, bei `mirror_session` neben `has_event`:

```cpp
ci.builtinMethods["snapshot_revision"] = [i8p, i32, i64](CompilerContext& ctx,
    llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
    // Arity-/i32-Prüfung über bestehende Compiler-Diagnostik vornehmen.
    if (call->args.size() != 1) {
        LOG_ERROR(ctx.fmtLoc() + "snapshot_revision expects net_id: i32");
        return ctx.ir_gen->wrap(ctx.builder.getInt64(0));
    }
    auto* s = ctx.builder.CreateLoad(i8p, base);
    auto* id = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
    return ctx.ir_gen->wrap(ctx.builder.CreateCall(
        ctx.getFunction("aiml_mirror_session_snapshot_revision", i64, {i8p, i32}),
        {s, id}));
};
// snapshot_error analog, Rückgabetyp i32; Argumenttyp vor CreateCall validieren.
```

Den AIML-Rückgabetyp von `snapshot_revision` als `u64` in Typinferenz/Metadaten
führen; LLVM verwendet dafür `i64`. Falls das bestehende Builtin-System zusätzliche
Return-Metadaten verlangt, dort ebenfalls eintragen. Keine neue Event-ABI nötig.

## 7. WS9-Test in einen Handshake umbauen (Commit 5)

### 7.1 Host: REGISTER bindet nur, DELTA macht bereit

In `test_mirror_ws_9rooms_imports.aiml` `client_info_nid` und `bound_ci` separat
führen. Im bisherigen `object_registered`-Zweig nur ID speichern und binden:

```yaml
- set: [client_info_nid, cev.net_id]
- call: [sess.register_id, g_ws9_server_client_info, client_info_nid]
- set: [bound_ci, 1]
# Kein sleep, kein got_ci=1 an dieser Stelle.
```

Nach jedem `sess.update` in der Wait-Schleife:

```yaml
- if:
    condition: { eq: [bound_ci, 1] }
    then:
      - decl: [snapshot_rc, i32, { call: [sess.snapshot_error, client_info_nid] }]
      - if:
          condition: { neq: [snapshot_rc, 0] }
          then:
            - print: ["FAIL ws9-host: ClientInfo decode rc=${snapshot_rc}"]
            - call: [sys.exit, 1]
      - if:
          condition: { gt: [{ call: [sess.snapshot_revision, client_info_nid] }, 0] }
          then:
            # Alle drei ClientInfo-Felder prüfen; bei Abweichung Fehler.
            - set: [got_ci, 1]
```

Erst nach `got_ci` View mit `round=7`, Namen, HP und Handgröße füllen. Timeout
endet mit Fehler, statt mit einem leeren Namen weiterzulaufen.

### 7.2 Client: vollständigen View prüfen und bestätigen

Client wartet auf positive View-Revision UND alle erwarteten View-Felder.
Erst danach `client.send_rpc(0, "ws9_view_received", 7)` senden. Der Host pumpt
bis zu diesem ACK und kontrolliert Methode, `arg` und den aus der Verbindung
stammenden erwarteten `peer_id`. Das ACK dient nur diesem One-Way-Testhandshake,
nicht als Ersatz für die vorhandene Request-ID-Korrelation allgemeiner RPCs.

Danach Host `sess.close`; Client wartet auf `host_th.join`, prüft seine eigenen
Modelle weiterhin und schließt seine Session. Host-Felder ausschließlich im
Host-Thread prüfen. Die bisherigen Client-Reads von
`g_ws9_server_client_info` und `ws9server.client_info_seen` entfernen. Hostfehler
beenden den Test oder werden über einen synchronisierten Kanal gemeldet; keine
ungeschützte globale Erfolgsvariable einführen.

Ein vorhandener monotoner Zeitgeber oder ein kleiner gemeinsamer Testhelfer
liefert Deadlines für Connect, REGISTER, Snapshot und ACK. Falls ein AIML-Builtin
hierfür fehlt, vor Testumbau ergänzen (C++ `steady_clock` auf Millisekunden).
Sleep bleibt höchstens die Poll-Taktung. Jede Timeoutmeldung benennt Phase,
Netz-ID, Revision und Decode-Fehler.

## 8. Gezielte Regressionen statt alleiniger Wiederholung (Commit 6)

Neue C++-Suite in bestehende Runtime-Testinfrastruktur integrieren; falls kein
geeigneter Target existiert, ein eigenes kleines Target mit Produktionsobjekten
anlegen. Keine dauerhafte `#include "mirror_runtime.cpp"`-Testlösung. Private
Apply-/Parserlogik ggf. in ein internes Modul auslagern.

### 8.1 Transaktionalität (C++-Testskizze)

```cpp
// Testfixture initialisiert OldState mit verwalteten Strings/Containern.
// encodeWire(model) ruft die neue aiml_ser_encode_wire-API auf.
const auto before = encodeWire(target);
const auto revision = registration.snapshot_revision;
auto bad = encodeWire(source);
bad.pop_back(); // innerer Längenpräfix passt nicht mehr
EXPECT_NE(applyDelta(registration, bad), 0);
EXPECT_EQ(encodeWire(target), before);
EXPECT_EQ(registration.snapshot_revision, revision);
EXPECT_NE(registration.last_decode_error, 0);

EXPECT_EQ(applyDelta(registration, encodeWire(source)), 0);
EXPECT_EQ(registration.snapshot_revision, revision + 1);
EXPECT_EQ(registration.last_decode_error, 0);
```

`applyDelta` ist hier ein Fixture-Adapter auf den Produktions-Apply-Pfad; die
Testnamen/-Makros sind an das tatsächlich vorhandene Framework anzupassen.
Zusätzlich beschädigte Payload mit formal korrekter äußerer Länge und Fehler
nach der ersten String-Allokation testen: das deckt Partial-Heap-Cleanup ab.

### 8.2 Pflichtfälle

| Fall | Erwartung |
| --- | --- |
| erstes DELTA defekt | Defaults unverändert, Revision 0, Fehler sichtbar |
| gültig → defekt → gültig | Zustand/Revision ändern sich nur bei Erfolg |
| DELTA vor REGISTER-Bindung | Pending und Live-Apply haben identische Semantik |
| gleiches Wire-Schema, andere Importsets | erfolgreicher Sync |
| `SharedCard` mit/ohne `rarity`, mindestens 2 Elemente | Ablehnung in beiden Richtungen, keine Teilübernahme |
| Raw- oder ungetaggtes DELTA an typed Modell | Ablehnung ohne Heap-Pointer-Mutation |
| Close/Unregister vor/nach erstem Snapshot | Objekt bleibt lesbar; ScopeDrop ohne Leak/Double-Free |
| Remote-UNREGISTER | keine hängenden `by_ptr`-/RPC-Lookup-Einträge |
| unverändertes Owner-Modell nach Initialsync | kein wiederholter Send allein wegen Headerunterschied |
| WS9: DELTA 100–250 ms nach REGISTER | Test wartet korrekt und besteht |
| WS9: Snapshot fehlt/ist defekt oder ACK fehlt | begrenztes, phasengenaues Scheitern |
| native↔wasm32, identisches Schema | gleiche Fingerprints trotz Pointer-/Offsetunterschieden |

Für deterministische Verzögerung einen Test-Proxy oder ausschließlich im
Test-Build aktivierten Send-Hook verwenden. Dieser hält DELTA-Frames zurück,
lässt REGISTER durch und gibt DELTA erst nach einem Testsignal frei. Keine
Produktions-Sleeps ergänzen. Auch Pending-Fall erzwingen: DELTA vor lokaler
Bindung vollständig zustellen.

Danach WS9 50–100-mal seriell ausführen, mit sauberem Host-Shutdown pro Lauf;
optional mit CPU-Last wiederholen. Der feste Testport verbietet parallele Läufe
gegen denselben Port. Ausgangsbefund: zuvor 16/16 grün, gemeldeter sporadischer
Fehler noch nicht dynamisch reproduziert.

## 9. Abschluss und Reihenfolge

1. Default-/Lifetime-Reproduktion und belegte Initialisierungsursache beheben.
2. Wire-Envelope, Parser und alle Sender-/Dirty-Tracking-Stellen zusammen ändern.
3. Transaktionalen Apply und Borrow-Ownership einschließlich Close/Unregister.
4. Snapshot-Revision/-Fehler-Builtins und deren Compiler-Typisierung.
5. Mismatch-Test auf Ablehnung umstellen, positiven Kontext-Test ergänzen,
   WS9-Handshake implementieren.
6. Sanitizer-, Native-, deterministische Delay- und WASM-Tests durchführen;
   anschließend Spec und Implementierungsstatus aktualisieren.

Jeder Zwischenschritt muss kompilieren; bis Sender und Empfänger gemeinsam
umgestellt sind, den neuen Formatpfad nicht als auslieferbar markieren.
Fertig erst, wenn Initialisierung ohne Netzwerk korrekt ist, Fehlpakete den
Zustand unverändert lassen, Session-Ende Borrow-Objekte nicht zerstört und der
verzögerte WS9-Test auf tatsächliche Zustandsübernahme wartet.

## 10. Umsetzungsstand

### Commit 1 – Befund Anfangszustand (2026-10-03)

Test: `tests/test_mirror_replica_defaults.aiml` (+ `mirror_replica_defaults_lib.aiml`). Modell mit
String ohne/mit Default, `list<string>`, `map<string:i32>`, verschachteltem Modell, `list<Model>`,
Skalar-Default; Varianten lokal, importiertes Global, Hauptdatei-Global, `ptr<>`, `shared<>`.

- Defaults vor jedem Netzwerkaufruf korrekt, JSON identisch über die Varianten.
- `decode_rpc` (transaktional, `release_old = 1`) in jede Default-Variante: korrekt.
- Replika-Bindung aller Varianten, Snapshot übernommen.
- Experiment: DELTA-Live- und Pending-Pfad auf `decode_sized_tx(…, release_old = 1)` umgestellt →
  komplette Mirror-Testreihe ohne Crash (Fehler nur: Close-Lesbarkeit im neuen Test, Mismatch-Test
  lehnt korrekt ab); AOT+Valgrind für den neuen Test, WS9-Imports, WS9-GameView, list_mutation:
  0 invalid, 0 definitely/indirectly lost.

**Ergebnis: nicht reproduziert** (nicht „behoben“). Der früher beobachtete Crash trat vor dem
`MIRROR_FIND_OBJ_PTR`-Fix (ptr-Slot statt Objektadresse, AP6) auf; ein Zusammenhang ist plausibel,
aber nicht belegt. Offen bleibt die Ownership beim Close (Felder danach leer) → Commit 3.

### Commits 2–6 – umgesetzt (2026-10-03)

- **Spec** 44_5 §4 (Objekte, Borrow-Ownership, Readiness, `snapshot_revision`/`snapshot_error`),
  §10 (DELTA-Format 2, Regeln, kein Mischbetrieb, optionaler Produktions-Test-Hook
  `AIML_MIRROR_TEST_HOLD_DELTA`), §11.1 (Dirty-Tracking vergleicht den gesendeten Blob);
  spec/23 `time.mono_ms`.
- **Serializer**: `aiml_ser_encode_wire`, `aiml_ser_decode_delta_tx` (Magic + Fingerprint
  zwingend, dann `decode_sized_tx(…, release_old = 1)`), `aiml_ser_wire_syncable`;
  `aiml_ser_decode_bounded` (Best-effort) entfernt.
- **Mirror**: `has_decoded` → `snapshot_revision`/`last_decode_error`/`pending_format`;
  `build_delta`/`make_delta_packet` für alle Sendepfade, Dirty-Tracking auf demselben Blob;
  Parser: Format 2 mit `payload_len`, Format 0 nur raw, alles andere (inkl. altem Format 1)
  ungültiger Frame; `apply_typed_delta` für Live- und Pending-Pfad; Raw↔typed abgewiesen;
  Host leitet nur Akzeptiertes bzw. Platzhalter weiter; `erase_registration` für Unregister,
  Remote-UNREGISTER und Close (keine Feldfreigabe mehr); Deskriptoren mit PTR_REF/Ressourcen
  werden mit Diagnose nicht registriert.
- **Compiler**: `register`/`register_id` typisiert für jedes Modell mit Deskriptor (auch reine
  Zahlenmodelle), sonst Compile-Fehler; temporärer Wert → Compile-Fehler (neg n27);
  Builtins `snapshot_revision` (i64/u64) und `snapshot_error` (i32).
- **Tests**: `test_mirror_replica_defaults`, `test_mirror_delta_reject`,
  `test_mirror_number_model`, WS9 als Handshake (+ `_delayed` mit Halte-Hook),
  `shared_module_context_mismatch` (Ablehnung in beide Richtungen, 2 Karten) +
  `shared_module_context_same`, Format-1-Fall in `test_mirror_rpc_malformed`,
  C++-Unit `tests/unit/test_mirror_delta.cpp` (Binary `out-test/aiml-tests/aiml-tests`).
- **Ergebnisse**: Mirror-Reihe 34/34; AOT+Valgrind (8 Tests + WS9 im Halte-Modus) 0/0;
  Unit-Test inkl. Valgrind 0/0; WS9 100× seriell (abwechselnd normal/Halte-Modus) 100/100;
  native↔WASM (`tools/run_wasm_mirror_test.sh`, `…_multifile_test.sh`) PASS.
- **Nebenbefund (Regression aus cdcba60)**: TCP-Empfangspuffer `uint8_t chunk[65536]` auf dem
  Stack → Stack-Overflow im WASM-`session.update` (64 KiB Stack). Behoben: direkt in `peer->rx`.
  WASM-Skripte starten den Server jetzt erst nach dem Client-Build (vorher Timing-Fehler);
  Multifile-Client spec-konform `ref<Tagged>` statt `ptr<Tagged>`.
- **Nicht abgedeckt**: negativer WS9-Fall „Snapshot/ACK fehlt“ nur über die Deadlines im Test,
  kein eigener Fehlertest; Modellversions-Migration bewusst außerhalb.
