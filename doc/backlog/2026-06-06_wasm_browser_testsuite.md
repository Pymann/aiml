# Browser WASM Testsuite (testsuite.html)

Date: 2026-06-06
Status: backlog
Predecessor: Node-based WASM testsuite (`run_wasm_tests.sh` + `tools/run_wasm_test.sh` + `tools/run_wasm_mirror_test.sh` + `tools/run_wasm_mirror_multifile_test.sh`) is in place and exercising 5+ test paths.

## Summary

We have a Node-based runner for WASM tests, but Node uses headless V8
which differs in subtle ways from the in-page V8 / SpiderMonkey that the
DataCreatures client actually runs under. Some bugs reproduce only in a
real browser environment (e.g. behavior around `requestAnimationFrame`,
WebGL contexts, `fetch`/`new WebSocket` semantics, the asyncify stack
interplay with browser event loop).

A browser testsuite would:

- Page-load each test as a standalone module.
- Capture stdout / exit code via custom stdout redirect.
- Render a pass/fail table and a per-test detail panel.
- Be reachable via the existing local server (`localhost:8080`).

## Architecture

1. Bake one `.{js,wasm}` artifact per test into `out/wasm_tests/<name>/`,
   plus an index file `out/wasm_tests/index.json` listing tests + expected
   exit codes.  Same artifacts already produced by `run_wasm_test.sh`
   except for the indexing.
2. Static `tests/wasm_testsuite.html` page that:
   - Fetches `index.json`.
   - For each test, instantiates the WASM module in an iframe (so a
     failed test doesn't poison the runner).
   - Hooks the Emscripten module's `print` / `printErr` to capture stdout.
   - Awaits exit (via `onAbort` or `exit` callback in the emscripten
     module) and records the code.
   - Renders a `<table>` with columns: test name, exit code, output, link
     to per-test detail view.
3. Hosting: drop the page under `tests/wasm_testsuite.html` and serve via
   `python -m http.server 8080`. URL: `http://localhost:8080/tests/wasm_testsuite.html`.

## Effort estimate

- Build pipeline (extend `run_wasm_tests.sh` to also emit `index.json`
  with metadata): 30 min
- `wasm_testsuite.html` + JS orchestrator: 1-2 h
- Iframe isolation + output capture quirks (emscripten's printErr / FS): 1 h

Total: ~2.5-3.5 h. Node runner stays in place for CI; browser is for
manual verification of browser-specific bugs.
