# Browser Games mit AIML hosten

## Architektur

```
┌─────────────────────────────────────────────────┐
│  AIML HTTP-Server (spec/32_2_network_http_s.md)  │
│  srv.filesystem("/", ["dist/"], {})              │
│  → serviert index.html + game.wasm + game.js    │
└─────────────────────────────────────────────────┘
          ↕ HTTP/HTTPS
┌─────────────────────────────────────────────────┐
│  Browser                                         │
│  → lädt game.wasm (Emscripten-Output)            │
│  → verbindet sich via wss:// zum Mirror-Server   │
└─────────────────────────────────────────────────┘
```

## HTTP-Server-Code (AIML)

```aiml
- decl: [srv, http_server_handle, { call: [http.server, "0.0.0.0", 8080] }]
- call: [srv.filesystem, "/", ["dist/"], {}]
- call: [srv.start]
```

`dist/` enthält den Emscripten-Output des AIML-Game-Projekts.

## Was bereits funktioniert

- HTTP-Server mit `filesystem`-Middleware → statische Files servieren ✓
- Mirror mit `wss://` → Browser-kompatibles Multiplayer ✓
- `ws`/`wss` Namespace → implementiert ✓

## Was noch fehlt

### 1. WASM-Compilation-Pipeline (größte Lücke)

Der AIML AOT-Compiler muss `emcc` (Emscripten) statt `clang` als Backend nutzen:

- `-s ASYNCIFY -s ASYNCIFY_IMPORTS=[engine_swap_internal]`
  → damit `engine.run` / `engine.swap` im Browser funktioniert (blockierender Loop via Coroutinen-Transformation)
- `-s USE_WEBGL2=1 -s FULL_ES3=1`
  → OpenGL ES 3 → WebGL 2 Mapping
- Linker-Ausgabe: `game.wasm` + `game.js` (Glue) + `index.html`

### 2. `engine.platform()` zur Laufzeit

Muss im WASM-Build `"web"` zurückgeben (für transport-Entscheidungen wie `tcp://` vs `ws://`).

### 3. Asset-Packaging

Emscripten braucht ein virtuelles Dateisystem:

```
emcc ... --preload-file assets/
```

Assets landen als `.data`-Datei neben der `.wasm`, werden beim Start ins virtuelle FS gemountet.

## Multiplayer-Szenario (Mirror)

```
[AIML Mirror-Server]  tcp://0.0.0.0:7777   ← Desktop-Clients
                      wss://0.0.0.0:7778   ← Browser-Clients

[AIML HTTP-Server]    https://0.0.0.0:443  ← serviert game.wasm
```

Client-seitiger Transport-Switch via `engine.platform()`:

```aiml
- decl: [platform, string, { call: [engine.platform] }]
- decl: [session, mirror_session]
- if: [{ eq: [platform, "web"] }, true]
  then:
    - set: [session, { call: [mirror.connect, "wss://gameserver.com:7778"] }]
  else:
    - set: [session, { call: [mirror.connect, "tcp://gameserver.com:7777"] }]
```

## Zusammenfassung

Die Server-Seite (HTTP + Mirror) ist bereits spezifiziert und umsetzbar.
Die fehlende Lücke ist primär die `emcc`-Zielstufe im AIML AOT-Compiler.
