# Chapter 44.2 — Audio

> **Core idea:** The `audio` namespace is based on **miniaudio** (single-header C library,
> no external dependencies). The same AIML code runs on desktop, Android, and browser.
> miniaudio automatically selects the optimal backend per platform and decodes
> WAV, MP3, FLAC, and OGG without additional decoder libraries.

---

## Platform Backends

| Platform | Backend |
|----------|---------|
| Linux | PulseAudio → ALSA → OSS (fallback chain) |
| macOS | CoreAudio |
| Windows | WASAPI → DirectSound (fallback) |
| Android | AAudio (API 26+) → OpenSL ES (fallback) |
| Browser (WASM) | Web Audio API (via Emscripten) |

No `extern: "C"` or platform-specific code needed in user code.

---

## 1. Types

| Type | LLVM | Description |
|------|------|-------------|
| `audio_sound` | `i8*` | Loaded and playable sound (wraps `ma_sound`) |
| `audio_group` | `i8*` | Volume group for sound categories (music, SFX, ambient, …) |

---

## 2. Engine Lifecycle

The miniaudio engine is automatically initialized on the first `audio.*` call.
Manual init is optional (e.g. for configuration before the first load).

| Call | Description |
|------|-------------|
| `audio.init()` | Explicitly initialize the engine (optional; otherwise lazy on first call) |
| `audio.shutdown()` | Shut down the engine, stop all sounds and free resources |
| `audio.set_volume(v: f32)` | Master volume (0.0 = mute, 1.0 = full) |

---

## 3. Loading Sounds

| Call | Return | Description |
|------|--------|-------------|
| `audio.load(p: path)` | `audio_sound` | Fully decode and load sound into RAM (short SFX) |
| `audio.load_stream(p: path)` | `audio_sound` | Stream-decode sound (for music; no full decoding into RAM) |

Supported formats: **WAV**, **MP3**, **FLAC**, **OGG Vorbis** — all via embedded
miniaudio decoders (dr_wav, dr_mp3, dr_flac, stb_vorbis). No `libvorbis` or similar needed.

---

## 4. `audio_sound` — Methods

### 4.1 Playback Control

| Method | Arguments | Return | Description |
|--------|-----------|--------|-------------|
| `play` | — | — | Play (from current position) |
| `pause` | — | — | Pause (position is kept) |
| `stop` | — | — | Stop and rewind to start |
| `set_loop` | `on: bool` | — | Enable/disable infinite looping |
| `set_volume` | `v: f32` | — | Volume 0.0–1.0 |
| `set_pitch` | `p: f32` | — | Pitch multiplier (1.0 = normal, 2.0 = one octave higher) |
| `set_pan` | `p: f32` | — | Stereo panning: −1.0 = left, 0.0 = center, +1.0 = right |
| `seek` | `seconds: f32` | — | Jump to time position |
| `free` | — | — | Free the sound |

### 4.2 Querying State

| Method | Return | Description |
|--------|--------|-------------|
| `is_playing` | `bool` | Is currently playing? |
| `is_finished` | `bool` | Has reached the end (without loop)? |
| `duration` | `f32` | Total duration in seconds |
| `position` | `f32` | Current playback position in seconds |

### 4.3 Group Assignment

| Method | Arguments | Description |
|--------|-----------|-------------|
| `set_group` | `group: audio_group` | Assign sound to a group; group volume applies multiplicatively |

### 4.4 3D Positional Audio

| Method | Arguments | Description |
|--------|-----------|-------------|
| `set_position` | `pos: vec3` | 3D position of the sound source in world coordinates |
| `set_velocity` | `vel: vec3` | Velocity for Doppler effect |
| `set_attenuation` | `model: string, min_dist: f32, max_dist: f32` | Volume falloff with distance |
| `set_spatialization` | `on: bool` | Enable/disable 3D positioning (default: `false` for stereo sounds) |
| `set_directional_attenuation` | `factor: f32` | Directionality of the source (0.0 = omnidirectional) |

`set_attenuation` models: `"linear"`, `"exponential"`, `"inverse"` (miniaudio standard).

---

## 5. `audio_group` — Methods

| Method | Arguments | Description |
|--------|-----------|-------------|
| `set_volume` | `v: f32` | Volume of all sounds in this group |
| `pause_all` | — | Pause all sounds in the group |
| `resume_all` | — | Resume all sounds in the group |
| `stop_all` | — | Stop all sounds in the group |

Creating groups:

```aiml
- decl: [music_group, audio_group, { call: [audio.create_group] }]
- decl: [sfx_group,   audio_group, { call: [audio.create_group] }]
```

---

## 6. 3D Listener

The listener represents the camera/ear of the player.

| Call | Description |
|------|-------------|
| `audio.set_listener(pos: vec3, forward: vec3, up: vec3)` | Set position + orientation of the listener |
| `audio.set_listener_velocity(vel: vec3)` | Velocity for Doppler |

Typically called once per frame with camera data.

---

## 7. Complete Example

```aiml
# ── Groups for volume categories ───────────────────────────────────────────────
- decl: [music_grp, audio_group, { call: [audio.create_group] }]
- decl: [sfx_grp,   audio_group, { call: [audio.create_group] }]
- decl: [amb_grp,   audio_group, { call: [audio.create_group] }]

# ── Load sounds ────────────────────────────────────────────────────────────────
# Music: streaming (large file, no full decoding into RAM)
- decl: [bgm,       audio_sound, { call: [audio.load_stream, "assets/music/theme.ogg"] }]
# Short SFX: fully in RAM (fast triggering, no seek latency)
- decl: [snd_shoot, audio_sound, { call: [audio.load, "assets/sfx/shoot.wav"] }]
- decl: [snd_jump,  audio_sound, { call: [audio.load, "assets/sfx/jump.wav"] }]
- decl: [snd_amb,   audio_sound, { call: [audio.load_stream, "assets/amb/wind.ogg"] }]

# ── Assign groups ──────────────────────────────────────────────────────────────
- call: [bgm.set_group,       music_grp]
- call: [snd_shoot.set_group, sfx_grp]
- call: [snd_jump.set_group,  sfx_grp]
- call: [snd_amb.set_group,   amb_grp]

# ── Initial volumes ────────────────────────────────────────────────────────────
- call: [music_grp.set_volume, 0.7]
- call: [sfx_grp.set_volume,   1.0]
- call: [amb_grp.set_volume,   0.4]

# ── Start music ────────────────────────────────────────────────────────────────
- call: [bgm.set_loop, true]
- call: [bgm.play]
- call: [snd_amb.set_loop, true]
- call: [snd_amb.play]

# ── Game loop ──────────────────────────────────────────────────────────────────
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

      # Synchronize listener with camera
      - call: [audio.set_listener, cam_pos, cam_forward, cam_up]

- def:
    name: render
    args: []
    body:
      - call: [scene_draw]

- decl: [win, window_handle, { call: [engine.create_window, 1280, 720, "Game"] }]
- call: [input.init, win]
- call: [engine.run, win, update, render]

# ── Cleanup ────────────────────────────────────────────────────────────────────
- call: [bgm.free]
- call: [snd_shoot.free]
- call: [snd_jump.free]
- call: [snd_amb.free]
- call: [audio.shutdown]
- call: [engine.destroy_window, win]
```

---

## 8. 3D Audio Example

```aiml
# Gunshot sound positioned in 3D (e.g. distant enemy)
- decl: [snd_distant_shot, audio_sound, { call: [audio.load, "assets/sfx/shot_distant.wav"] }]
- call: [snd_distant_shot.set_spatialization, true]
- call: [snd_distant_shot.set_attenuation, "linear", 1.0, 50.0]

# When firing: set enemy position and play
- call: [snd_distant_shot.set_position, enemy_pos]
- call: [snd_distant_shot.play]

# Every frame: listener = camera
- call: [audio.set_listener, cam_pos, cam_forward, cam_up]
```

---

## 9. Implementation Notes

### 9.1 miniaudio Integration

`src/vendor/miniaudio.h` is included once with the required defines:

```cpp
// In audio_runtime.cpp (once):
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_GENERATION         // No synthesizers needed
#include "../vendor/miniaudio.h"
```

All other translation units `#include "audio_runtime.h"` without the implementation define.

### 9.2 Internal Structures

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
// At the start of every audio_* C function:
aiml_audio_ensure_init();  // no-op if already initialized
```

### 9.4 Web Audio (Browser)

miniaudio has a native Emscripten backend (`ma_backend_webaudio`).
Restriction: browsers only allow audio after a user interaction
(click, keypress). `audio.init()` should therefore only be called after the first
input event — or `engine.run` triggers it on the first `input.*` call.

### 9.5 Dependencies

No external libraries. `miniaudio.h` is checked in to `src/vendor/`.
System libraries required by the linker (platform-dependent, see miniaudio docs):

| Platform | Linker Flags |
|----------|-------------|
| Linux | `-lpthread -lm -ldl` |
| macOS | `-framework CoreAudio -framework AudioToolbox` |
| Windows | `-lole32 -lwinmm` |
| Android | `-lOpenSLES` (API < 26) or `-laaudio` (API ≥ 26) |
| Browser | no extra flag (Emscripten built-in) |
