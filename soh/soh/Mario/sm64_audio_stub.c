/**
 * audio_stub.c - Minimal stubs for libsm64 audio symbols.
 *
 * The full SM64 audio synthesis engine (decomp/audio/) is excluded to keep
 * the WASM binary small enough to link within GitHub Actions memory limits.
 * This file provides the minimal symbols that Mario action code references.
 *
 * We never call sm64_audio_tick() so audio produces no output; Mario's
 * jump/footstep sounds are forwarded to the registered callback only.
 */

#include "libsm64.h"

/* ---------------------------------------------------------------------- */
/* Symbols from play_sound.h / load_audio_data.h                          */
/* ---------------------------------------------------------------------- */

SM64PlaySoundFunctionPtr g_play_sound_func = 0;
int g_is_audio_initialized = 0;

/* Called by Mario action code for jumps, footsteps, etc. */
void play_sound(unsigned int soundBits, float* pos) {
    if (g_play_sound_func) {
        g_play_sound_func(soundBits, pos);
    }
}

/* Called by sm64_global_init() path — no-op without the audio engine */
void load_audio_banks(const unsigned char* rom) {
    (void)rom;
}

/* ---------------------------------------------------------------------- */
/* Symbols from decomp/game/sound_init.c                                  */
/* sound_init.c includes audio/external.h which pulls in the full engine; */
/* stub these functions here instead.                                      */
/* ---------------------------------------------------------------------- */

void reset_volume(void) {}
void lower_background_noise(int a) { (void)a; }
void raise_background_noise(int a) { (void)a; }
void disable_background_sound(void) {}
void enable_background_sound(void) {}
void set_sound_mode(unsigned short soundMode) { (void)soundMode; }
void play_menu_sounds(short soundMenuFlags) { (void)soundMenuFlags; }
void fadeout_music(short fadeOutTime) { (void)fadeOutTime; }
void fadeout_level_music(short fadeTimer) { (void)fadeTimer; }
