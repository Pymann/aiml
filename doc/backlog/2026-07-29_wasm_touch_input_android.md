# WASM/Browser: Touch-Input für Mobil (Android/iOS)

Date: 2026-07-29
Status: backlog

## Summary

Der WASM/Browser-Build (emscripten + WebGL2, `src/sys/engine_runtime.cpp`)
rendert und vernetzt bereits portabel — aber das **Input-Handling kennt nur
Maus + Tastatur**. Es gibt keinen Touch-/Pointer-Event-Pfad. Auf einem
Android-/iOS-Browser funktioniert damit **kein Drag & Drop** — und 9rooms_v2
ist stark drag-lastig (Karten aus der Hand in Zonen ziehen, Angreifen per Drag,
Ausrüsten, Bewegen). Desktop-Browser (Windows/macOS/Linux) sind ok, weil dort
Maus-Events kommen.

## Was fehlt

- Emscripten Touch-Events registrieren: `emscripten_set_touchstart_callback` /
  `touchmove` / `touchend` (bzw. Pointer-Events `pointerdown/move/up`, die Maus
  UND Touch vereinheitlichen — vermutlich der sauberere Weg).
- Diese auf denselben internen Input-State mappen wie die Maus (Position +
  „primary button down"), damit die bestehende Drag-Logik (pick → move → drop)
  ohne Änderung greift.
- Multi-Touch vorerst ignorieren (nur der erste Finger = Cursor).
- Optional: Long-Press → Rechtsklick-Äquivalent (Ability-Picker etc.), da es
  auf Touch keine zweite Maustaste gibt.

## Umfang / Ort

- `src/sys/engine_runtime.cpp` (emscripten-Zweig, `#if defined(__EMSCRIPTEN__)`)
  — dort werden schon Canvas-Größe/WebGL init gehandhabt; Touch-Callbacks dazu.
- Kein AIML-seitiger Change nötig, wenn Touch auf den Maus-State gemappt wird.

## Kontext

Aufgekommen bei der Frage „läuft 9rooms_v2 als Browser-Game auf Windows/Android?"
(2026-07-29): Windows-Browser ja, Android nur eingeschränkt — genau wegen des
fehlenden Touch-Inputs. Netzwerk ist portabel (WS-Client hat `#ifdef
__EMSCRIPTEN__`-Pfad in `ws_runtime.cpp`; nur `ws://`/`wss://`, kein rohes TCP).
