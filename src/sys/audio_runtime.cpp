/*
 * audio_runtime.cpp — AIML audio namespace (44.2)
 * Uses miniaudio (single-header) — MINIAUDIO_IMPLEMENTATION defined here ONLY.
 */

/* ── miniaudio single-header implementation ──────────────────────────────── */
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_GENERATION         /* no synthesizers needed */
#include "../vendor/miniaudio.h"

#include "audio_runtime.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>

/* ── Internal structs ─────────────────────────────────────────────────────── */

struct AimlAudioEngine {
    ma_engine engine;
    bool      initialized;
};

struct AimlAudioSound {
    ma_sound sound;
    bool     initialized;
};

struct AimlAudioGroup {
    ma_sound_group group;
    bool           initialized;
};

/* ── Global engine state ─────────────────────────────────────────────────── */

static AimlAudioEngine g_audio = {};

static void aiml_audio_ensure_init() {
    if (g_audio.initialized) return;
    ma_engine_config cfg = ma_engine_config_init();
    ma_result r = ma_engine_init(&cfg, &g_audio.engine);
    if (r != MA_SUCCESS) {
        fprintf(stderr, "[audio] ma_engine_init failed: %d\n", (int)r);
        return;
    }
    g_audio.initialized = true;
}

/* ── Engine lifecycle ─────────────────────────────────────────────────────── */

void aiml_audio_init(void) {
    aiml_audio_ensure_init();
}

void aiml_audio_shutdown(void) {
    if (!g_audio.initialized) return;
    ma_engine_uninit(&g_audio.engine);
    g_audio.initialized = false;
}

void aiml_audio_set_volume(float v) {
    aiml_audio_ensure_init();
    ma_engine_set_volume(&g_audio.engine, v);
}

/* ── Load ─────────────────────────────────────────────────────────────────── */

void* aiml_audio_load(const char* path) {
    aiml_audio_ensure_init();
    if (!path) return nullptr;
    AimlAudioSound* s = (AimlAudioSound*)calloc(1, sizeof(AimlAudioSound));
    if (!s) return nullptr;
    ma_result r = ma_sound_init_from_file(&g_audio.engine, path,
                                          MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION,
                                          nullptr, nullptr, &s->sound);
    if (r != MA_SUCCESS) {
        fprintf(stderr, "[audio] load('%s') failed: %d\n", path, (int)r);
        free(s);
        return nullptr;
    }
    s->initialized = true;
    return s;
}

void* aiml_audio_load_stream(const char* path) {
    aiml_audio_ensure_init();
    if (!path) return nullptr;
    AimlAudioSound* s = (AimlAudioSound*)calloc(1, sizeof(AimlAudioSound));
    if (!s) return nullptr;
    /* streaming: no MA_SOUND_FLAG_DECODE — decode on the fly */
    ma_result r = ma_sound_init_from_file(&g_audio.engine, path,
                                          MA_SOUND_FLAG_NO_SPATIALIZATION,
                                          nullptr, nullptr, &s->sound);
    if (r != MA_SUCCESS) {
        fprintf(stderr, "[audio] load_stream('%s') failed: %d\n", path, (int)r);
        free(s);
        return nullptr;
    }
    s->initialized = true;
    return s;
}

/* ── Groups ───────────────────────────────────────────────────────────────── */

void* aiml_audio_create_group(void) {
    aiml_audio_ensure_init();
    AimlAudioGroup* g = (AimlAudioGroup*)calloc(1, sizeof(AimlAudioGroup));
    if (!g) return nullptr;
    ma_result r = ma_sound_group_init(&g_audio.engine, 0, nullptr, &g->group);
    if (r != MA_SUCCESS) {
        fprintf(stderr, "[audio] create_group failed: %d\n", (int)r);
        free(g);
        return nullptr;
    }
    g->initialized = true;
    return g;
}

/* ── audio_sound: helpers ─────────────────────────────────────────────────── */

static inline AimlAudioSound* snd(void* p) { return (AimlAudioSound*)p; }
static inline bool snd_ok(void* p) { return p && snd(p)->initialized; }

/* ── audio_sound: playback control ───────────────────────────────────────── */

void aiml_audio_sound_play(void* p) {
    if (!snd_ok(p)) return;
    ma_sound_start(&snd(p)->sound);
}

void aiml_audio_sound_pause(void* p) {
    if (!snd_ok(p)) return;
    ma_sound_stop(&snd(p)->sound);
}

void aiml_audio_sound_stop(void* p) {
    if (!snd_ok(p)) return;
    ma_sound_stop(&snd(p)->sound);
    ma_sound_seek_to_pcm_frame(&snd(p)->sound, 0);
}

void aiml_audio_sound_set_loop(void* p, int32_t on) {
    if (!snd_ok(p)) return;
    ma_sound_set_looping(&snd(p)->sound, on ? MA_TRUE : MA_FALSE);
}

void aiml_audio_sound_set_volume(void* p, float v) {
    if (!snd_ok(p)) return;
    ma_sound_set_volume(&snd(p)->sound, v);
}

void aiml_audio_sound_set_pitch(void* p, float pitch) {
    if (!snd_ok(p)) return;
    ma_sound_set_pitch(&snd(p)->sound, pitch);
}

void aiml_audio_sound_set_pan(void* p, float pan) {
    if (!snd_ok(p)) return;
    ma_sound_set_pan(&snd(p)->sound, pan);
}

void aiml_audio_sound_seek(void* p, float seconds) {
    if (!snd_ok(p)) return;
    ma_uint32 srate = ma_engine_get_sample_rate(&g_audio.engine);
    ma_uint64 frame = (ma_uint64)(seconds * srate);
    ma_sound_seek_to_pcm_frame(&snd(p)->sound, frame);
}

void aiml_audio_sound_free(void* p) {
    if (!snd_ok(p)) return;
    ma_sound_uninit(&snd(p)->sound);
    free(p);
}

/* ── audio_sound: state queries ───────────────────────────────────────────── */

int32_t aiml_audio_sound_is_playing(void* p) {
    if (!snd_ok(p)) return 0;
    return ma_sound_is_playing(&snd(p)->sound) ? 1 : 0;
}

int32_t aiml_audio_sound_is_finished(void* p) {
    if (!snd_ok(p)) return 1;
    return ma_sound_at_end(&snd(p)->sound) ? 1 : 0;
}

float aiml_audio_sound_duration(void* p) {
    if (!snd_ok(p)) return 0.0f;
    float dur = 0.0f;
    ma_sound_get_length_in_seconds(&snd(p)->sound, &dur);
    return dur;
}

float aiml_audio_sound_position(void* p) {
    if (!snd_ok(p)) return 0.0f;
    float pos = 0.0f;
    ma_sound_get_cursor_in_seconds(&snd(p)->sound, &pos);
    return pos;
}

/* ── audio_sound: group assignment ───────────────────────────────────────── */

void aiml_audio_sound_set_group(void* p, void* grp) {
    if (!snd_ok(p)) return;
    AimlAudioGroup* g = (AimlAudioGroup*)grp;
    if (!g || !g->initialized) return;
    /* miniaudio: attach node to group */
    ma_node_attach_output_bus(&snd(p)->sound, 0, &g->group, 0);
}

/* ── audio_sound: 3D positional ───────────────────────────────────────────── */

void aiml_audio_sound_set_position(void* p, float x, float y, float z) {
    if (!snd_ok(p)) return;
    ma_sound_set_position(&snd(p)->sound, x, y, z);
}

void aiml_audio_sound_set_velocity(void* p, float x, float y, float z) {
    if (!snd_ok(p)) return;
    ma_sound_set_velocity(&snd(p)->sound, x, y, z);
}

void aiml_audio_sound_set_attenuation(void* p, const char* model, float min_dist, float max_dist) {
    if (!snd_ok(p)) return;
    ma_attenuation_model m = ma_attenuation_model_linear;
    if (model) {
        if (strcmp(model, "inverse")     == 0) m = ma_attenuation_model_inverse;
        else if (strcmp(model, "exponential") == 0) m = ma_attenuation_model_exponential;
        /* "linear" is default */
    }
    ma_sound_set_attenuation_model(&snd(p)->sound, m);
    ma_sound_set_min_distance(&snd(p)->sound, min_dist);
    ma_sound_set_max_distance(&snd(p)->sound, max_dist);
}

void aiml_audio_sound_set_spatialization(void* p, int32_t on) {
    if (!snd_ok(p)) return;
    ma_sound_set_spatialization_enabled(&snd(p)->sound, on ? MA_TRUE : MA_FALSE);
}

void aiml_audio_sound_set_directional_attenuation(void* p, float factor) {
    if (!snd_ok(p)) return;
    ma_sound_set_directional_attenuation_factor(&snd(p)->sound, factor);
}

/* ── audio_group methods ──────────────────────────────────────────────────── */

static inline AimlAudioGroup* grp(void* p) { return (AimlAudioGroup*)p; }
static inline bool grp_ok(void* p) { return p && grp(p)->initialized; }

void aiml_audio_group_set_volume(void* p, float v) {
    if (!grp_ok(p)) return;
    ma_sound_group_set_volume(&grp(p)->group, v);
}

void aiml_audio_group_pause_all(void* p) {
    if (!grp_ok(p)) return;
    ma_sound_group_stop(&grp(p)->group);
}

void aiml_audio_group_resume_all(void* p) {
    if (!grp_ok(p)) return;
    ma_sound_group_start(&grp(p)->group);
}

void aiml_audio_group_stop_all(void* p) {
    if (!grp_ok(p)) return;
    ma_sound_group_stop(&grp(p)->group);
}

/* ── 3D Listener ─────────────────────────────────────────────────────────── */

void aiml_audio_set_listener(float px, float py, float pz,
                              float fx, float fy, float fz,
                              float ux, float uy, float uz) {
    aiml_audio_ensure_init();
    ma_engine_listener_set_position  (&g_audio.engine, 0, px, py, pz);
    ma_engine_listener_set_direction (&g_audio.engine, 0, fx, fy, fz);
    ma_engine_listener_set_world_up  (&g_audio.engine, 0, ux, uy, uz);
}

void aiml_audio_set_listener_velocity(float vx, float vy, float vz) {
    aiml_audio_ensure_init();
    ma_engine_listener_set_velocity(&g_audio.engine, 0, vx, vy, vz);
}
