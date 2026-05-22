# AionDB Kv mmap Refactor — Plan und API-Vertrag

**Datum:** 2026-04-29 (Plan), 2026-04-30 (Done)
**Status:** ✅ **DONE** — alle 7 Implementations-Schritte deployed, 161/0/19 Tests gruen
**Betrifft:** alle `Kv<...>` Stores (typed, string, blob, fixed)
**Kompatibilitaet:** alte `.kvwal`-Dateien werden invalidiert (Format-Version 1 → 2)
**Final-Spec:** [`spec/31_db_integration.md`](../31_db_integration.md) §6.2-§6.7

## Implementations-Zusammenfassung

| Schritt | Was | Status |
|---|---|---|
| 1 | Datei-Header (`AIMLKVWL` magic, version, flags) + format check | ✅ |
| 2 | mmap-Setup mit sparse-extended Reserve (16 MB initial, mremap-grow) | ✅ |
| 3 | Index-basierte Reads (kv->index statt kv->map fuer persistent) | ✅ |
| 4 | Strings mit `\0` im WAL → zero-copy mmap-pointer (Version-Bump auf 2) | ✅ |
| 5 | `kv.evict` via `madvise(MADV_DONTNEED)`, Index intakt | ✅ |
| 6 | `kv.checkpoint` mit Temp-File + atomic rename (crash-safe) | ✅ |
| 7 | Decoded-Cache als bounded LRU (MAX 1024) | ✅ |

Tests: 161 passed (158 bestehend + `test_db_kv_evict.aiml` + neu hinzugefuegt
`test_db_kv_checkpoint.aiml` + `test_db_kv_lru.aiml`), 0 failed, 19 skipped.

## Motivation

Aktuell wird beim `db.open_kv()` der gesamte WAL gescannt und alle Werte
in eine `std::unordered_map<string, vector<uint8_t>>` kopiert. Bei
hundert MB / GB grossen Stores (z.B. Tensor-Trajectories im Per-File
Training) fuehrt das zu RSS-Druck und in Verbindung mit mehreren
gleichzeitig offenen Stores zu OOM.

Ziel: Werte direkt aus einer `mmap`'d Datei lesen, RAM-Footprint = nur
ein kleiner Index `key → (offset, length)`.

## Geltungsbereich

Alle vier KV-Speichermodi werden gleichzeitig migriert:

| Modus | Trigger | Heute | Nach Refactor |
|---|---|---|---|
| Typed | `ser_nodes != nullptr` | Blob in `map`, decoded in `decoded_cache` | Blob in mmap, decoded LRU-bounded |
| String | `val_size==0`, `!blob_only` | `strdup`'d `char*` in `map` | mmap-Pointer (mit Null-Terminator in WAL) |
| Blob | `val_size==0`, `blob_only` | rohe Bytes in `map` | mmap-Pointer (zero-copy) |
| Fixed | `val_size>0` | rohe Bytes in `map` | mmap-Pointer |

## Datei-Format (NEU)

### Header (32 Bytes, Datei-Anfang)

```c
struct AimlKvWalHeader {
    char     magic[8];       // "AIMLKVWL"
    uint32_t version;        // 1
    uint32_t flags;          // bit0: typed, bit1: string, bit2: blob, bit3: fixed
    int64_t  val_size;       // fixed-mode element size (0 fuer string/typed/blob)
    uint64_t wal_used;       // letzter geschriebener Byte-Offset (best-effort)
};
static_assert(sizeof(AimlKvWalHeader) == 32);
```

`wal_used` wird:
- bei `kv_close` / `kv_checkpoint` aktualisiert (msync auf erste Page)
- bei normalen `set` NICHT mitgeschrieben (Performance)
- beim Open via Crash-Scan ab `header.wal_used` rekonstruiert

### Records (unmittelbar nach Header)

```
SET:        [op=1:u8][key_len:u32][key:bytes][val_bytes:u32][val:bytes]
DELETE:     [op=2:u8][key_len:u32][key:bytes]
SET_BLOB:   [op=3:u8][key_len:u32][key:bytes][val_bytes:u32][val:bytes]
```

**Aenderung gegenueber heute (string-mode):**
- `kv_wal_write_set` schreibt im String-Modus jetzt `strlen(str)+1` Bytes
  inklusive `\0`. Damit ist `mmap_base + offset` direkt ein gueltiger
  null-terminierter `char*`.
- Alle anderen Records bleiben formatgleich, nur durch den neuen Header
  praefigiert.

### Magic + Versions-Check

Beim Open:
1. Datei existiert nicht / leer → schreibe Header, `ftruncate` auf Reserve-Groesse
2. Datei existiert, magic stimmt → aktuelle Version: nutzen
3. Datei existiert, magic stimmt nicht → **fatal log + abort**
   (alte Format-Version 0 ohne Header → User informieren, manuell loeschen)
4. Datei existiert, magic stimmt, version unbekannt → fatal log

→ alte `.kvwal` Dateien sind nicht migrierbar; User loescht `aiondb/`.

## Speicher-Modell

### mmap-Reserve

Beim Open wird die Datei via `ftruncate` auf eine Reserve-Groesse gestreckt
(default `KV_MMAP_RESERVE = 16 GB`, sparse — kein Disk-Verbrauch). Das
gesamte Reserve-Fenster wird mit `MAP_SHARED|MAP_NORESERVE` gemappt:

```c
ftruncate(fd, KV_MMAP_RESERVE);
void* base = mmap(nullptr, KV_MMAP_RESERVE,
                  PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_NORESERVE,
                  fd, 0);
```

- Pages werden lazy alloziert beim ersten Touch
- RSS waechst nur mit tatsaechlich genutztem Daten-Volumen
- Keine `mremap`-Fallunterscheidung im Hot Path (Reserve gross genug)
- 16 GB virt pro Store ist vertretbar (auch 1000 Stores = 16 TB virt, kein Problem)

### Append = memcpy

Schreiben nutzt **keine** `write()` Syscalls mehr:

```c
char* dst = (char*)kv->mmap_base + kv->wal_used;
memcpy(dst, &record, record_size);
kv->wal_used += record_size;
```

Eintraege landen direkt in der Page-Cache-Region. Kernel synct lazy.

Wenn `wal_used + record_size > KV_MMAP_RESERVE`, wird entweder:
- die Reserve verdoppelt (`ftruncate` + `mremap`) — Phase 2
- oder `log.fatal` (Phase 1)

### Index

```c
struct ValueLoc {
    uint64_t offset;     // Offset in mmap_base
    uint32_t length;     // Bytes des Value-Records
};
std::unordered_map<std::string, ValueLoc> index;
```

`ValueLoc::offset` zeigt auf den **Value-Block** (nicht den Record-Anfang).
Damit ist `mmap_base + offset` direkt ein gueltiger Pointer auf die Daten.

### Open-Pfad

1. `open(wal_path, O_RDWR|O_CREAT)`
2. Falls leer: Header schreiben, `ftruncate(fd, RESERVE)`
3. Falls vorhanden: Header lesen, magic/version pruefen
4. `mmap(RESERVE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_NORESERVE)`
5. Index aufbauen: linearer Scan von `sizeof(header)` bis zum ersten
   ungueltigen Op-Byte (Crash-Recovery) oder bis `header.wal_used`
6. Fertig — keine Daten kopiert, nur Index gefuellt

## API-Vertrag

### Was bleibt unveraendert (alle AIML-Aufrufe)

```yaml
- decl: [kv,  Kv<string:string>, { call: [db.open_kv, "name", {persist: true}] }]
- call: [kv.set, "k", "v"]
- decl: [v,   string,            { call: [kv.get, "k"] }]
- decl: [n,   i32,                { call: [kv.len] }]
- decl: [has, i32,                { call: [kv.has, "k"] }]
- call: [kv.delete, "k"]
- call: [kv.clear]
- call: [kv.set_blob, "k", buf]
- decl: [bl,  buffer<u8>,         { call: [kv.get_blob, "k"] }]
- call: [kv.iter]
- call: [kv.checkpoint]
- call: [kv.evict]
- call: [kv.reload]
```

Alle obigen Aufrufe haben **identische Signatur und Semantik** wie zuvor.

### Was sich aendert: Pointer-Lebensdauer

| Operation | Gueltig bis (heute) | Gueltig bis (neu) |
|---|---|---|
| `kv.get(k)` String-Modus | naechste `kv.set(k,...)` oder `kv.delete(k)` | `kv.checkpoint()` oder `kv.clear()` |
| `kv.get_blob(k)` Blob-Modus | naechste `kv.set_blob(k,...)` | `kv.checkpoint()` oder `kv.clear()` |
| `kv.get(k)` Typed-Modus | naechste `kv.set(k,...)` oder `kv.delete(k)` | unveraendert (Decoded-Cache) |
| `kv.iter()` Keys | bis `kv.set/delete/clear` | bis `kv.set/delete/clear` (unveraendert) |

**Wichtige Lockerung:** Pointer aus `kv.get(k)` werden NICHT mehr durch
`kv.set(k, anderer_wert)` invalidiert. Hintergrund: alte Records
bleiben im WAL, der neue Wert wird angefuegt, der Index zeigt auf den
neuen — der alte Pointer zeigt aber weiterhin in eine gueltige
mmap-Region.

→ **Alle Code, der heute korrekt ist, bleibt korrekt.** Die neue
Garantie ist staerker, nicht schwaecher.

### Was sich aendert: Performance-Profil

| Operation | Heute | Neu |
|---|---|---|
| `db.open_kv()` (gross) | viel RAM, langsam | wenig RAM, ungefaehr gleich schnell |
| `kv.get()` | hash + memcpy/strdup | hash + Pointer-Return (schneller) |
| `kv.set_blob(grosser Wert)` | malloc + memcpy + write | memcpy in mmap (schneller) |
| `kv.evict()` | clear() der Map (free) | `madvise(MADV_DONTNEED)` (instant, RSS faellt) |
| `kv.reload()` | full WAL replay | Index-Rebuild (gleich schnell) |
| `kv.checkpoint()` | rewrite + fsync | unmap + truncate + rewrite + remap (langsamer, aber selten) |

### Was sich aendert: Crash-Verhalten

- Heute: Crash → letzter unvollstaendiger Record bleibt im WAL, beim
  naechsten Open wird er beim Replay erkannt (read schlaegt fehl) und
  abgebrochen. Datenverlust = der eine unvollstaendige Eintrag.
- Neu: gleich. Open scannt bis zum ersten ungueltigen Op-Byte. Sparse
  Pages nach dem letzten gueltigen Record sind 0 (durch ftruncate),
  was natuerlicher Sentinel ist.

## API fuer 9rooms-Projekt — Migration-Hinweise

### Was 9rooms bemerken wird

1. **Alte `.kvwal` Dateien sind unbrauchbar** — beim Open wird per
   Magic-Check abgebrochen. User muss `aiondb/`-Verzeichnis loeschen
   bzw. die spezifischen `.kvwal`-Dateien.

2. **Datenbank-Reinit haengt sich nicht mehr** — `kv.evict()` ist jetzt
   instant statt teuer.

3. **Hot-Reload aus anderem Prozess** — `kv.reload()` Verhalten
   bleibt: Index wird neu aufgebaut. Der MAP_SHARED-Mode sieht aber
   neue Bytes anderer Schreiber automatisch. `reload()` wird nur noch
   benoetigt, wenn der Schreiber **andere Stores** angelegt hat oder
   bestehende **gekuerzt** wurden (Compaction).

4. **`get()` Pointer-Lebensdauer ist gelockert** — bestehender Code
   bleibt korrekt. Neuer Code darf laenger Pointer halten.

### Was 9rooms nicht aendern muss

- Keine AIML-Source-Aenderung
- Keine Recompile noetig (libaiml ABI bleibt)
- Keine Build-Konfig-Aenderung

### Empfohlener Migrations-Schritt fuer 9rooms

```bash
# Vor dem ersten Run mit neuer libaiml:
rm -rf aiondb/*.kvwal
# Optional: alle Stores neu populieren
```

Bei Stores mit nicht-rekonstruierbaren Daten (User-State,
Spielfortschritt) — vor dem Update **manuell exportieren** und nach
dem Update neu schreiben.

## Implementierung — Teilschritte

### Schritt 1: Datei-Header und Format-Migration
- `AimlKvWalHeader` Struct + Konstanten
- `aiml_db_kv_open_persist`: Header schreiben/lesen, Magic-Check
- Alte Dateien ohne Header abweisen

### Schritt 2: mmap-Setup
- Reserve via `ftruncate` + `mmap(MAP_SHARED|MAP_NORESERVE)`
- `wal_used` Cursor in `AimlKv` Struct
- Records via memcpy in mmap, kein `write()` mehr

### Schritt 3: Index-basierte Reads
- `ValueLoc` Struct + `index` Map ersetzt `kv->map`
- `kv_wal_replay()` baut nur Index, kopiert keine Daten
- `aiml_db_kv_get`: Lookup → `mmap_base + offset` zurueckgeben

### Schritt 4: String-Modus mit Null-Terminator
- `kv_wal_write_set` (string-mode): `val_bytes = strlen(str) + 1`
- WAL enthaelt `\0`-terminierte Strings → direkter `char*` Return

### Schritt 5: Blob-Modus zero-copy
- `kv_set_blob`: memcpy direkt in mmap-Region
- `kv_get_blob`: Pointer + length aus Index

### Schritt 6: Typed-Modus mit Decoded-Cache
- Serialized-Blob via mmap
- `decoded_cache` als bounded LRU (z.B. 100 Eintraege)
- Auf Cache-Miss: decode aus mmap

### Schritt 7: kv_evict mit madvise
- Decoded-Cache leeren
- `madvise(mmap_base, mmap_size, MADV_DONTNEED)`
- Index NICHT leeren (klein, faellt bei naechstem Touch eh wieder ein)
- `evicted`-Flag und `lazy_reload` entfallen

### Schritt 8: Compaction (kv_checkpoint)
- Lock acquire
- alte mmap unmap
- alle aktiven Records in temp-Datei schreiben
- temp → real (rename)
- neue mmap aufsetzen
- Index neu aufbauen (Offsets haben sich geaendert)

### Schritt 9: Tests + Dokumentation
- Bestehende Tests laufen lassen (sollten alle gruen sein)
- Neue Tests: grosser Store + evict → RSS-Drop verifizieren
- spec/31_db_integration.md updaten

## Risiken und Mitigation

| Risiko | Wahrscheinlichkeit | Mitigation |
|---|---|---|
| 16 GB Reserve auf 32-bit-Systemen unmoeglich | irrelevant (nur 64-bit) | – |
| Reserve-Ueberlauf (Datei waechst >16 GB) | niedrig | log.fatal in Phase 1, mremap in Phase 2 |
| Crash mitten im Header-Update | sehr niedrig | Crash-Scan ab `sizeof(header)` ignoriert Header-Felder |
| `kv.checkpoint()` waehrend andere Threads lesen | mittel | Per-KV mutex haelt; Compaction blockiert Reads |
| Page-Cache-Druck durch viele Stores | niedrig | madvise(DONTNEED) bei evict freigibt Pages |
| msync-Latenz bei sehr grossen Schreibvolumen | niedrig | Linux flush-Daemon syncs alle 30 s automatisch |

## Erfolgs-Kriterien

1. **Per-File-Training laeuft durch ohne OOM** (10 Files × 4 Worker)
2. **RSS faellt nach Worker-Join** (kv.evict + tensor.arena_free greifen)
3. **Bestehende AIML-Tests laufen alle gruen** (159+ Tests)
4. **Test-Run mit grossem Store**: Open eines 1 GB Stores ohne RSS-Spike
5. **9rooms-Smoke-Test**: nach Verzeichnis-Reset funktioniert das Spiel

## Offene Fragen

- Soll Reserve-Groesse konfigurierbar sein? (env var `AIMLDB_KV_RESERVE_MB`?)
- Brauchen wir bounded-LRU explizit, oder Heap-Druck als implizite Eviction?
- Compaction automatisch triggern (z.B. bei DELETE-Anteil > 30 %)?

Diese koennen in Phase 2 entschieden werden.
