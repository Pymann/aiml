#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * audio_runtime.h — AIML audio namespace (44.2)
 *
 * Based on miniaudio (single-header, no external dependencies).
 * Supports: WAV, MP3, FLAC, OGG Vorbis
 * Linux:    PulseAudio → ALSA → OSS
 *
 * audio_sound  = AimlAudioSound*  (opaque i8*)
 * audio_group  = AimlAudioGroup*  (opaque i8*)
 */

/* ── Engine lifecycle ─────────────────────────────────────────────────────── */

/* Explicit init (optional; lazy init happens on first audio.* call) */
void aiml_audio_init    (void);
void aiml_audio_shutdown(void);
void aiml_audio_set_volume(float v);  /* master volume 0.0–1.0 */

/* ── Load ─────────────────────────────────────────────────────────────────── */

void* aiml_audio_load       (const char* path);   /* returns AimlAudioSound* */
void* aiml_audio_load_stream(const char* path);   /* returns AimlAudioSound* (streaming) */

/* ── Groups ───────────────────────────────────────────────────────────────── */

void* aiml_audio_create_group(void);  /* returns AimlAudioGroup* */

/* ── audio_sound methods ──────────────────────────────────────────────────── */

void    aiml_audio_sound_play    (void* snd);
void    aiml_audio_sound_pause   (void* snd);
void    aiml_audio_sound_stop    (void* snd);
void    aiml_audio_sound_set_loop  (void* snd, int32_t on);
void    aiml_audio_sound_set_volume(void* snd, float v);
void    aiml_audio_sound_set_pitch (void* snd, float p);
void    aiml_audio_sound_set_pan   (void* snd, float p);
void    aiml_audio_sound_seek      (void* snd, float seconds);
void    aiml_audio_sound_free      (void* snd);

int32_t aiml_audio_sound_is_playing (void* snd);
int32_t aiml_audio_sound_is_finished(void* snd);
float   aiml_audio_sound_duration   (void* snd);
float   aiml_audio_sound_position   (void* snd);

void    aiml_audio_sound_set_group  (void* snd, void* grp);

/* 3D positional */
void    aiml_audio_sound_set_position            (void* snd, float x, float y, float z);
void    aiml_audio_sound_set_velocity            (void* snd, float x, float y, float z);
void    aiml_audio_sound_set_attenuation         (void* snd, const char* model, float min_dist, float max_dist);
void    aiml_audio_sound_set_spatialization      (void* snd, int32_t on);
void    aiml_audio_sound_set_directional_attenuation(void* snd, float factor);

/* ── audio_group methods ──────────────────────────────────────────────────── */

void aiml_audio_group_set_volume (void* grp, float v);
void aiml_audio_group_pause_all  (void* grp);
void aiml_audio_group_resume_all (void* grp);
void aiml_audio_group_stop_all   (void* grp);

/* ── 3D Listener ─────────────────────────────────────────────────────────── */

void aiml_audio_set_listener         (float px, float py, float pz,
                                      float fx, float fy, float fz,
                                      float ux, float uy, float uz);
void aiml_audio_set_listener_velocity(float vx, float vy, float vz);

#ifdef __cplusplus
}
#endif
