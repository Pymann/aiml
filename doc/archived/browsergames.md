# Hosting browser games with AIML

## Architecture

```
┌─────────────────────────────────────────────────┐
│  AIML HTTP server (spec/32_2_network_http_s.md)  │
│  srv.filesystem("/", ["dist/"], {})              │
│  → serves index.html + game.wasm + game.js      │
└─────────────────────────────────────────────────┘
          ↕ HTTP/HTTPS
┌─────────────────────────────────────────────────┐
│  Browser                                         │
│  → loads game.wasm (Emscripten output)           │
│  → connects to the mirror server via wss://      │
└─────────────────────────────────────────────────┘
```

## HTTP server code (AIML)

```aiml
- decl: [srv, http_server_handle, { call: [http.server, "0.0.0.0", 8080] }]
- call: [srv.filesystem, "/", ["dist/"], {}]
- call: [srv.start]
```

`dist/` contains the Emscripten output of the AIML game project.

## What already works

- HTTP server with `filesystem` middleware → serving static files ✓
- Mirror with `wss://` → browser-compatible multiplayer ✓
- `ws`/`wss` namespace → implemented ✓

## What is still missing

### 1. WASM compilation pipeline (the biggest gap)

The AIML AOT compiler must use `emcc` (Emscripten) instead of `clang` as the backend:

- `-s ASYNCIFY -s ASYNCIFY_IMPORTS=[engine_swap_internal]`
  → so that `engine.run` / `engine.swap` work in the browser (blocking loop via coroutine transformation)
- `-s USE_WEBGL2=1 -s FULL_ES3=1`
  → OpenGL ES 3 → WebGL 2 mapping
- Linker output: `game.wasm` + `game.js` (glue) + `index.html`

### 2. `engine.platform()` at runtime

Must return `"web"` in the WASM build (for transport decisions such as `tcp://` vs `ws://`).

### 3. Asset packaging

Emscripten needs a virtual file system:

```
emcc ... --preload-file assets/
```

Assets end up as a `.data` file next to the `.wasm` and are mounted into the virtual FS at startup.

## Multiplayer scenario (mirror)

```
[AIML mirror server]  tcp://0.0.0.0:7777   ← desktop clients
                      wss://0.0.0.0:7778   ← browser clients

[AIML HTTP server]    https://0.0.0.0:443  ← serves game.wasm
```

Client-side transport switch via `engine.platform()`:

```aiml
- decl: [platform, string, { call: [engine.platform] }]
- decl: [session, mirror_session]
- if: [{ eq: [platform, "web"] }, true]
  then:
    - set: [session, { call: [mirror.connect, "wss://gameserver.com:7778"] }]
  else:
    - set: [session, { call: [mirror.connect, "tcp://gameserver.com:7777"] }]
```

## Summary

The server side (HTTP + mirror) is already specified and implementable.
The missing gap is primarily the `emcc` target stage in the AIML AOT compiler.
