# Store Typed Persist Fix Report

## Kurzfassung

Der persistente `Store<T>`-Pfad wurde fuer Heap-Felder auf denselben sicheren Serialize/Deserialize-Ansatz umgestellt, den typed `Kv<K:V>` bereits nutzt.

Vorher:

- persistente `Store<T>`-Eintraege wurden bei `insert`/`update` als rohe Struct-Bytes geschrieben
- das war fuer Typen mit `string`, `list<...>`, `map<...>` oder anderen heapbasierten Feldern gefaehrlich
- bei Reopen konnten dadurch stale pointer wiederverwendet werden

Jetzt:

- der Compiler waehlt fuer persistente `Store<T>` mit Heap-Feldern automatisch einen typed-Store-Pfad
- der Runtime-Pfad schreibt WAL-Records als serialisierte Blobs
- beim Laden werden diese Blobs wieder in owned decoded slots materialisiert

## Geaenderte Stellen

### Compiler

In [builtin_registry.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/builtin_registry.cpp) wurde `db.open("...", {persist: true})` fuer `Store<T>` erweitert:

- wenn `T.hasHeapFields` und `T.serNodesVar` gesetzt sind
- dann wird nicht mehr `aiml_db_store_open_persist(...)`
- sondern `aiml_db_store_open_typed_persist(...)` aufgerufen

### Runtime API

In [db_runtime.h](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/sys/db_runtime.h) wurde die neue Runtime-API ergaenzt:

- `aiml_db_store_open_typed_persist(...)`

### Runtime-Implementierung

In [db_runtime.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/sys/db_runtime.cpp) wurden folgende Punkte umgesetzt:

- `AimlStore` bekam `ser_nodes`, `ser_n` und `typed_persistent`
- fuer typed persistente Stores gibt es jetzt decoded slot storage statt roher mmap-Slotdaten
- `store_wal_write(...)` serialisiert typed-Store-Eintraege als Blob
- `store_wal_replay(...)` decodiert typed-Store-Eintraege wieder in owned Slots
- `insert` / `update` / `get` / `delete` behandeln typed persistente Stores separat
- `checkpoint` kompaktisiert typed Store WALs jetzt aktiv statt nur `fsync`

## Neue Regressionstests

Neu hinzugefuegt:

- [test_aiondb_store_heap_reopen_write.aiml](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/tests/test_aiondb_store_heap_reopen_write.aiml)
- [test_aiondb_store_heap_reopen_read.aiml](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/tests/test_aiondb_store_heap_reopen_read.aiml)

Diese Tests decken einen `Store<T>` mit:

- `intent: string`
- `dependencies: list<string>`

ab und pruefen:

- `insert`
- `checkpoint`
- Reopen im zweiten Lauf
- korrekte Rueckgabe der Heap-Felder

## Verifikation

Nach den Aenderungen wurden erfolgreich ausgefuehrt:

- Rebuild von `aiml-tests`
- [test_aiondb_store_heap_reopen_write.aiml](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/tests/test_aiondb_store_heap_reopen_write.aiml)
- [test_aiondb_store_heap_reopen_read.aiml](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/tests/test_aiondb_store_heap_reopen_read.aiml)
- [test_aiondb_store_reopen_write.aiml](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/tests/test_aiondb_store_reopen_write.aiml)
- [test_aiondb_store_reopen_read.aiml](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/tests/test_aiondb_store_reopen_read.aiml)
- [test_card_heap_persist.aiml](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/tests/test_card_heap_persist.aiml)

## Restlicher Unterschied zum alten Store-Pfad

Der klassische persistente scalar-`Store<T>` bleibt mmap-slotbasiert.

Der neue heap-sichere Pfad fuer typed Stores ist bewusst anders:

- Snapshot/Replay ueber serialisierte Blobs
- keine rohen heaphaltigen Struct-Bytes mehr in persistenten Slots

Das ist die eigentliche Ursache des Fixes.
