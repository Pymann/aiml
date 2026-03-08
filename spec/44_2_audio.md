# Kapitel 44.2 — Audio

> **Leitgedanke:** Der `audio`-Namespace basiert auf **miniaudio** (single-header C-Bibliothek,
> keine externen Abhängigkeiten). Derselbe AIML-Code läuft auf Desktop, Android und Browser.
> miniaudio wählt automatisch den optimalen Backend pro Plattform und dekodiert
> WAV, MP3, FLAC und OGG ohne zusätzliche Decoder-Bibliotheken.

---

## Plattform-Backends

| Plattform | Backend |
|-----------|---------|
| Linux | PulseAudio → ALSA → OSS (Fallback-Kette) |
| macOS | CoreAudio |
| Windows | WASAPI → DirectSound (Fallback) |
| Android | AAudio (API 26+) → OpenSL ES (Fallback) |
| Browser (WASM) | Web Audio API (via Emscripten) |

Kein `extern: "C"` oder plattformspezifischer Code in Usercode nötig.

---

## 1. Typen

| Typ | LLVM | Beschreibung |
|-----|------|--------------|
| `audio_sound` | `i8*` | Geladener und abspielbarer Sound (wraps `ma_sound`) |
| `audio_group` | `i8*` | Lautstärke-Gruppe für Sound-Kategorien (Musik, SFX, Ambient, …) |

---

## 2. Engine-Lifecycle

Die miniaudio-Engine wird beim ersten `audio.*`-Aufruf automatisch initialisiert.
Manuelles Init ist optional (z.B. für Konfiguration vor dem ersten Load).

| Aufruf | Beschreibung |
|--------|--------------|
| `audio.init()` | Engine explizit initialisieren (optional; sonst lazy beim ersten Aufruf) |
| `audio.shutdown()` | Engine herunterfahren, alle Sounds stoppen und Ressourcen freigeben |
| `audio.set_volume(v: f32)` | Master-Lautstärke (0.0 = stumm, 1.0 = voll) |

---

## 3. Sound laden

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `audio.load(p: path)` | `audio_sound` | Sound vollständig dekodieren und in RAM laden (kurze SFX) |
| `audio.load_stream(p: path)` | `audio_sound` | Sound streaming dekodieren (für Musik; kein vollständiges Dekodieren in RAM) |

Unterstützte Formate: **WAV**, **MP3**, **FLAC**, **OGG Vorbis** — alle via eingebettete
miniaudio-Decoder (dr_wav, dr_mp3, dr_flac, stb_vorbis). Keine `libvorbis` o.ä. nötig.

---

## 4. `audio_sound` — Methoden

### 4.1 Wiedergabe-Steuerung

| Methode | Argumente | Rückgabe | Beschreibung |
|---------|-----------|----------|--------------|
| `play` | — | — | Abspielen (von aktueller Position) |
| `pause` | — | — | Pausieren (Position bleibt) |
| `stop` | — | — | Stoppen und an Anfang zurückspulen |
| `set_loop` | `on: bool` | — | Endlos-Loop ein-/ausschalten |
| `set_volume` | `v: f32` | — | Lautstärke 0.0–1.0 |
| `set_pitch` | `p: f32` | — | Pitch-Multiplikator (1.0 = normal, 2.0 = eine Oktave höher) |
| `set_pan` | `p: f32` | — | Stereo-Panning: −1.0 = links, 0.0 = Mitte, +1.0 = rechts |
| `seek` | `seconds: f32` | — | Zu Zeitposition springen |
| `free` | — | — | Sound freigeben |

### 4.2 Zustand abfragen

| Methode | Rückgabe | Beschreibung |
|---------|----------|--------------|
| `is_playing` | `bool` | Wird gerade abgespielt? |
| `is_finished` | `bool` | Ende erreicht (ohne Loop)? |
| `duration` | `f32` | Gesamtdauer in Sekunden |
| `position` | `f32` | Aktuelle Abspielposition in Sekunden |

### 4.3 Gruppen-Zuweisung

| Methode | Argumente | Beschreibung |
|---------|-----------|--------------|
| `set_group` | `group: audio_group` | Sound einer Gruppe zuweisen; Gruppen-Lautstärke wirkt multiplikativ |

### 4.4 3D Positional Audio

| Methode | Argumente | Beschreibung |
|---------|-----------|--------------|
| `set_position` | `pos: vec3` | 3D-Position der Schallquelle in Weltkoordinaten |
| `set_velocity` | `vel: vec3` | Geschwindigkeit für Doppler-Effekt |
| `set_attenuation` | `model: string, min_dist: f32, max_dist: f32` | Lautstärkeabfall mit Distanz |
| `set_spatialization` | `on: bool` | 3D-Positionierung ein-/ausschalten (default: `false` bei Stereo-Sounds) |
| `set_directional_attenuation` | `factor: f32` | Richtwirkung der Quelle (0.0 = omnidirektional) |

`set_attenuation`-Modelle: `"linear"`, `"exponential"`, `"inverse"` (miniaudio-Standard).

---

## 5. `audio_group` — Methoden

| Methode | Argumente | Beschreibung |
|---------|-----------|--------------|
| `set_volume` | `v: f32` | Lautstärke aller Sounds in dieser Gruppe |
| `pause_all` | — | Alle Sounds der Gruppe pausieren |
| `resume_all` | — | Alle Sounds der Gruppe fortsetzen |
| `stop_all` | — | Alle Sounds der Gruppe stoppen |

Gruppen erstellen:

```aiml
- decl: [music_group, audio_group, { call: [audio.create_group] }]
- decl: [sfx_group,   audio_group, { call: [audio.create_group] }]
```

---

## 6. 3D-Listener

Der Listener entspricht der Kamera/dem Ohr des Spielers.

| Aufruf | Beschreibung |
|--------|--------------|
| `audio.set_listener(pos: vec3, forward: vec3, up: vec3)` | Position + Orientierung des Listeners setzen |
| `audio.set_listener_velocity(vel: vec3)` | Geschwindigkeit für Doppler |

Typischerweise einmal pro Frame mit Kamera-Daten aufgerufen.

---

## 7. Vollständiges Beispiel

```aiml
# ── Gruppen für Lautstärke-Kategorien ────────────────────────────────────────
- decl: [music_grp, audio_group, { call: [audio.create_group] }]
- decl: [sfx_grp,   audio_group, { call: [audio.create_group] }]
- decl: [amb_grp,   audio_group, { call: [audio.create_group] }]

# ── Sounds laden ──────────────────────────────────────────────────────────────
# Musik: streaming (große Datei, kein vollständiges Dekodieren in RAM)
- decl: [bgm,       audio_sound, { call: [audio.load_stream, "assets/music/theme.ogg"] }]
# Kurze SFX: vollständig in RAM (schnelles Auslösen, kein Seek-Latenz)
- decl: [snd_shoot, audio_sound, { call: [audio.load, "assets/sfx/shoot.wav"] }]
- decl: [snd_jump,  audio_sound, { call: [audio.load, "assets/sfx/jump.wav"] }]
- decl: [snd_amb,   audio_sound, { call: [audio.load_stream, "assets/amb/wind.ogg"] }]

# ── Gruppen zuweisen ──────────────────────────────────────────────────────────
- call: [bgm.set_group,       music_grp]
- call: [snd_shoot.set_group, sfx_grp]
- call: [snd_jump.set_group,  sfx_grp]
- call: [snd_amb.set_group,   amb_grp]

# ── Initiale Lautstärken ──────────────────────────────────────────────────────
- call: [music_grp.set_volume, 0.7]
- call: [sfx_grp.set_volume,   1.0]
- call: [amb_grp.set_volume,   0.4]

# ── Musik starten ─────────────────────────────────────────────────────────────
- call: [bgm.set_loop, true]
- call: [bgm.play]
- call: [snd_amb.set_loop, true]
- call: [snd_amb.play]

# ── Game-Loop ─────────────────────────────────────────────────────────────────
- def:
    name: update
    args: [[dt, f32]]
    body:
      - decl: [jumped, bool, { call: [input.action_pressed, "jump"] }]
      - decl: [fired,  bool, { call: [input.action_pressed, "fire"] }]

      - if: [{ ==: [jumped, true] }, true]
        then:
          - call: [snd_jump.play]

      - if: [{ ==: [fired, true] }, true]
        then:
          - call: [snd_shoot.play]

      # Listener mit Kamera synchronisieren
      - call: [audio.set_listener, cam_pos, cam_forward, cam_up]

- def:
    name: render
    args: []
    body:
      - call: [scene_draw]

- decl: [win, window_handle, { call: [engine.create_window, 1280, 720, "Game"] }]
- call: [input.init, win]
- call: [engine.run, win, update, render]

# ── Aufräumen ─────────────────────────────────────────────────────────────────
- call: [bgm.free]
- call: [snd_shoot.free]
- call: [snd_jump.free]
- call: [snd_amb.free]
- call: [audio.shutdown]
- call: [engine.destroy_window, win]
```

---

## 8. 3D-Audio Beispiel

```aiml
# Schuss-Sound 3D positioniert (z.B. entfernter Gegner)
- decl: [snd_distant_shot, audio_sound, { call: [audio.load, "assets/sfx/shot_distant.wav"] }]
- call: [snd_distant_shot.set_spatialization, true]
- call: [snd_distant_shot.set_attenuation, "linear", 1.0, 50.0]

# Beim Abfeuern: Position des Gegners setzen und abspielen
- call: [snd_distant_shot.set_position, enemy_pos]
- call: [snd_distant_shot.play]

# Jeden Frame: Listener = Kamera
- call: [audio.set_listener, cam_pos, cam_forward, cam_up]
```

---

## 9. Implementierungshinweise

### 9.1 miniaudio-Integration

`src/vendor/miniaudio.h` wird einmalig mit den nötigen Defines eingebunden:

```cpp
// In audio_runtime.cpp (einmalig):
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_GENERATION         // Keine Synthesizer nötig
#include "../vendor/miniaudio.h"
```

Alle anderen Translation Units `#include "audio_runtime.h"` ohne das Implementation-Define.

### 9.2 Interne Strukturen

```cpp
struct AimlAudioEngine {
    ma_engine engine;
    bool initialized;
};

struct AimlAudioSound {
    ma_sound sound;
};

struct AimlAudioGroup {
    ma_sound_group group;
};
```

### 9.3 Lazy Init

```cpp
// In jedem audio_* C-Funktion am Anfang:
aiml_audio_ensure_init();  // no-op wenn bereits initialisiert
```

### 9.4 Web Audio (Browser)

miniaudio hat einen nativen Emscripten-Backend (`ma_backend_webaudio`).
Einschränkung: Browser erlauben Audio erst nach einer User-Interaction
(Klick, Tastendruck). `audio.init()` sollte daher erst nach dem ersten
Input-Event aufgerufen werden — oder `engine.run` triggert es beim
ersten `input.*`-Aufruf.

### 9.5 Abhängigkeiten

Keine externen Bibliotheken. `miniaudio.h` wird in `src/vendor/` eingecheckt.
Systembibliotheken die der Linker benötigt (plattformabhängig, miniaudio-Doku):

| Plattform | Linker-Flags |
|-----------|-------------|
| Linux | `-lpthread -lm -ldl` |
| macOS | `-framework CoreAudio -framework AudioToolbox` |
| Windows | `-lole32 -lwinmm` |
| Android | `-lOpenSLES` (API < 26) oder `-laaudio` (API ≥ 26) |
| Browser | kein extra Flag (Emscripten built-in) |
