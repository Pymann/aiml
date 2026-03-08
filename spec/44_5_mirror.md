# Kapitel 44.5 — Mirror (Netzwerk-Synchronisation)

> **Leitgedanke:** Mirror synchronisiert AIML-Modell-Instanzen und ihre Felder
> automatisch über das Netzwerk. Der Transport wird per URL-Schema gewählt —
> `tcp://` für Desktop/Android (raw Sockets via `net`-Namespace),
> `ws://` / `wss://` für alle Plattformen inkl. Browser (WebSocket via `ws`-Namespace).
> Spielcode sieht weder Transport noch Serialisierung — nur `session.update()` pro Frame
> und normale Feldzugriffe auf synchronisierte Objekte.

---

## Architektur

```
┌──────────────────────────────────────────────────────────────┐
│  Spielcode          player.health -= 10                      │
│                     session.update()                         │
├──────────────────────────────────────────────────────────────┤
│  Mirror-Layer       Dirty-Tracking → Delta-Pakete            │
│                     RPC-Stubs → serialisierte Calls          │
│                     Deserialize → Felder auf Replika setzen  │
├──────────────────────────────────────────────────────────────┤
│  Transport-Abstraction   tcp:// | ws:// | wss://             │
├───────────────────────────────┬──────────────────────────────┤
│  net Namespace (TCP)          │  ws / wss Namespace          │
│  Desktop + Android            │  Desktop + Android + Browser │
└───────────────────────────────┴──────────────────────────────┘
```

---

## 1. Typen

| Typ | LLVM | Beschreibung |
|-----|------|--------------|
| `mirror_session` | `i8*` | Aktive Netzwerk-Session (Host oder Client) |
| `mirror_peer` | `i8*` | Verbundener Peer (Gegenstelle) |

---

## 2. Model-Deklaration mit Sync

Felder und Methoden die über das Netzwerk gespiegelt werden sollen,
werden direkt in der `model`-Deklaration markiert:

```aiml
- model: Player
  sync: [pos, health, score]     # diese Felder werden automatisch repliziert
  rpc:  [take_damage, respawn]   # diese Methoden werden zu Peers weitergeleitet
  fields:
    - [pos,    vec3]
    - [health, i32]
    - [score,  i32]
    - [name,   string]           # nicht in sync: → nur lokal
    - [inv,    list<string>]     # nicht in sync: → nur lokal
```

- **`sync:`** — Liste von Feldnamen; der Compiler generiert automatisch
  Serialisierung/Deserialisierung für diese Felder
- **`rpc:`** — Liste von Methodennamen; Aufrufe werden an den Besitzer
  (oder alle Peers) weitergeleitet; Argumente werden mitkodiert
- Felder die nicht in `sync:` stehen werden niemals übertragen

Der Compiler generiert pro `sync:`-Modell:
- `__aiml_mirror_serialize_<Model>(obj*, buf*)` — nur geänderte Felder (Delta)
- `__aiml_mirror_deserialize_<Model>(obj*, buf*)` — empfangene Felder anwenden

---

## 3. Session-Erstellung

### 3.1 Transport-URL

| Schema | Transport | Plattform |
|--------|-----------|-----------|
| `tcp://host:port` | Raw TCP Socket (`net`-Namespace) | Desktop, Android |
| `ws://host:port` | WebSocket unverschlüsselt (`ws`-Namespace) | alle inkl. Browser |
| `wss://host:port` | WebSocket TLS (`wss`-Namespace) | alle inkl. Browser |

Im Browser (WASM) sind nur `ws://` und `wss://` verfügbar.
`tcp://` auf einem Browser-Target löst einen Laufzeitfehler aus.

### 3.2 Host / Listen

```aiml
- decl: [session, mirror_session, { call: [mirror.listen, url] }]
```

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `mirror.listen(url: string)` | `mirror_session` | Auf eingehende Verbindungen warten; nicht-blockierend |
| `mirror.listen_max(url: string, max_peers: i32)` | `mirror_session` | Mit Peer-Limit |

### 3.3 Client / Connect

```aiml
- decl: [session, mirror_session, { call: [mirror.connect, url] }]
```

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `mirror.connect(url: string)` | `mirror_session` | Verbindung aufbauen (blockiert bis verbunden oder Fehler) |
| `mirror.connect_timeout(url: string, ms: i32)` | `mirror_session` | Mit Timeout in ms |

### 3.4 Session schließen

| Aufruf | Beschreibung |
|--------|--------------|
| `session.close()` | Session und alle Peer-Verbindungen schließen |
| `session.is_connected()` | `bool` — Session aktiv und mindestens ein Peer verbunden |
| `session.peer_count()` | `i32` — Anzahl verbundener Peers |

---

## 4. Objekte registrieren

```aiml
- decl: [net_id, i32, { call: [session.register, obj] }]
```

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `session.register(obj)` | `i32` | Objekt registrieren; gibt eine netzwerkweite ID zurück |
| `session.register_id(obj, id: i32)` | — | Objekt mit fester ID registrieren (für vordefinierte Objekte wie Welt-Entities) |
| `session.unregister(obj)` | — | Objekt aus der Synchronisation entfernen |

Registrierung teilt Peers automatisch mit, dass dieses Objekt existiert.
Der registrierende Peer ist der **Besitzer** (Owner) — nur er sendet Updates.
Andere Peers empfangen Updates und dürfen `sync:`-Felder nur lesen.

---

## 5. Frame-Update

```aiml
- call: [session.update]
```

`session.update()` wird einmal pro Frame aufgerufen (typischerweise am Anfang von `update(dt)`):

1. Eingehende Pakete empfangen und verarbeiten
2. Geänderte `sync:`-Felder eigener Objekte als Delta-Pakete senden
3. RPC-Aufrufe von Peers ausführen
4. Peer-Events (connect/disconnect) in Queue einreihen

---

## 6. Sync-Modi

| Aufruf | Beschreibung |
|--------|--------------|
| `session.set_sync_rate(obj, hz: f32)` | Sync-Frequenz für dieses Objekt in Hz; Default = `session.update()`-Rate |
| `session.sync_now(obj)` | Sofortiges Delta-Paket senden (unabhängig von Rate) |
| `session.set_sync_mode(obj, mode: string)` | `"auto"` (bei jedem `update` wenn dirty) oder `"manual"` (nur via `sync_now`) |

---

## 7. RPCs

RPC-Methoden werden wie normale Methoden aufgerufen.
Intern serialisiert der Compiler-generierte Stub die Argumente und sendet sie.

```aiml
# Auf dem aufrufenden Peer (Owner oder beliebiger Peer je nach RPC-Ziel):
- call: [player.take_damage, 25]

# Auf dem Ziel-Peer wird take_damage(25) ausgeführt
```

### RPC-Richtung

| Aufruf | Beschreibung |
|--------|--------------|
| `session.set_rpc_target(obj, method: string, target: string)` | `"owner"` (Default), `"all"`, `"others"`, `"server"` |

- **`"owner"`**: RPC wird zum Besitzer des Objekts weitergeleitet (häufigster Fall: Client → Server)
- **`"all"`**: RPC wird an alle Peers gesendet inkl. lokalem Ausführen
- **`"others"`**: RPC wird an alle anderen Peers gesendet, lokal nicht ausgeführt
- **`"server"`**: immer an den Host (auch wenn Aufrufer selbst Host ist → lokal)

---

## 8. Peer-Events

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `session.has_event()` | `bool` | Peer-Event-Queue nicht leer |
| `session.next_event()` | `mirror_event` | Nächstes Event konsumieren |

```aiml
model: mirror_event
fields:
  - [type,    string]        # "peer_connected" | "peer_disconnected" | "object_registered" | "object_unregistered"
  - [peer,    mirror_peer]   # betroffener Peer
  - [net_id,  i32]           # betroffene Objekt-ID (bei object_*)
```

---

## 9. Vollständiges Beispiel

### Host (Server)

```aiml
# Session auf TCP und WS gleichzeitig öffnen
- decl: [tcp_session, mirror_session, { call: [mirror.listen, "tcp://0.0.0.0:7777"] }]
- decl: [ws_session,  mirror_session, { call: [mirror.listen, "ws://0.0.0.0:7778"]  }]

# Spieler-Objekte erstellen und registrieren
- decl: [p1, Player]
- set:  [p1.name, "Server-Player"]
- set:  [p1.pos,  { call: [math.vec3, 0.0, 1.0, 0.0] }]
- set:  [p1.health, 100]
- decl: [p1_id, i32, { call: [tcp_session.register, p1] }]

- def:
    name: update
    args: [[dt, f32]]
    body:
      # Eingehende Pakete verarbeiten, Delta-Updates senden
      - call: [tcp_session.update]
      - call: [ws_session.update]

      # Peer-Events abarbeiten
      - while:
          condition: { call: [tcp_session.has_event] }
          body:
            - decl: [ev, mirror_event, { call: [tcp_session.next_event] }]
            - if: [{ eq: [ev.type, "peer_connected"] }, true]
              then:
                - print: ["New peer connected"]

      # Spieler bewegen (automatisch an alle Peers gesendet weil pos in sync:)
      - decl: [fwd, f32, { call: [input.axis, "forward"] }]
      - set:  [p1.pos, { call: [math.vec3,
                          { "+": [p1.pos.x, { "*": [fwd, dt] }] },
                          p1.pos.y,
                          p1.pos.z] }]
```

### Client (Browser-kompatibel via WebSocket)

```aiml
# Browser: ws:// verwenden (kein tcp:// im Browser)
- decl: [platform, string, { call: [engine.platform] }]
- decl: [session,  mirror_session]

- if: [{ eq: [platform, "web"] }, true]
  then:
    - set: [session, { call: [mirror.connect, "ws://gameserver.com:7778"] }]
  else:
    - set: [session, { call: [mirror.connect, "tcp://gameserver.com:7777"] }]

- decl: [my_player, Player]
- set:  [my_player.name, "Client-Player"]
- decl: [my_id, i32, { call: [session.register, my_player] }]

- def:
    name: update
    args: [[dt, f32]]
    body:
      - call: [session.update]

      # Replizierte Server-Objekte sind nach session.update aktuell
      # p1 (Server-Player) wird automatisch aktualisiert — keine extra Logik nötig
      - call: [render_player, my_player]

      # RPC: Schaden nehmen → wird zum Server weitergeleitet
      - decl: [hit, bool, { call: [check_hit] }]
      - if: [{ ==: [hit, true] }, true]
        then:
          - call: [my_player.take_damage, 10]   # → RPC zum Owner (Server)
```

---

## 10. Paketformat (binär)

```
Header (6 Bytes):
  [net_id:   u32]   Objekt-ID
  [type:     u8]    0x01=delta, 0x02=rpc, 0x03=register, 0x04=unregister
  [reserved: u8]

Delta-Paket:
  [field_mask: u32]   Bitmask welche Felder enthalten sind (max. 32 sync-Felder)
  [field_data: ...]   Serialisierte Feldwerte in Deklarationsreihenfolge

RPC-Paket:
  [method_id: u8]     Index der RPC-Methode in der rpc:-Liste
  [arg_data:  ...]    Serialisierte Argumente
```

Primitive Typen werden little-endian kodiert.
`vec3` = 3 × f32 (12 Bytes), `mat4` = 16 × f32 (64 Bytes), `string` = u16-Länge + UTF-8-Bytes.

---

## 11. Implementierungshinweise

### 11.1 Dirty-Tracking

Pro registriertem Objekt hält der Mirror-Layer einen Shadow-Buffer
der letzten gesendeten `sync:`-Feldwerte. `session.update()` vergleicht
aktuellen Zustand mit Shadow → erzeugt `field_mask` für geänderte Felder →
sendet Delta-Paket nur wenn mindestens ein Feld geändert.

### 11.2 Transport-Abstraktion

```cpp
struct AimlMirrorTransport {
    enum Type { TCP, WS, WSS } type;
    union {
        int tcp_fd;          // AimlSocket.fd
        AimlWs* ws;          // WebSocket-Handle
    };
    bool is_listener;
};
```

`mirror_send` / `mirror_recv` delegieren intern an
`aiml_net_send` / `aiml_ws_send` je nach `type`.

### 11.3 Objekt-Lookup

```cpp
unordered_map<uint32_t, void*>   net_id_to_obj;   // net_id → Objekt-Pointer
unordered_map<void*, uint32_t>   obj_to_net_id;   // Objekt-Pointer → net_id
unordered_map<uint32_t, AimlMirrorMeta*> meta;    // net_id → serialize/deserialize fn-ptrs
```

`AimlMirrorMeta` enthält die vom Compiler generierten Funktionszeiger für
Serialisierung, Deserialisierung und RPC-Dispatch.

### 11.4 Interpolation (optional, v2)

Für `vec3`/`vec2`/`f32`-Felder mit hoher Sync-Rate kann clientseitig
lineare Interpolation zwischen empfangenen Werten aktiviert werden:

```aiml
- call: [session.set_interpolation, obj, "pos", true]
```

In v1 werden Werte sofort gesetzt (kein Smoothing).
