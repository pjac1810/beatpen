/* SPDX-License-Identifier: GPL-3.0-only */
/* BEATPEN: a stylus-style beat maker for the M-VAVE FM-1, after the Stylophone BEAT: 12 pads, DRUMS and BASS
 * modes, four sound packs (ROK, TECHNO, HIPHOP, BEATBOX), a 2-bar quantised looper recorded in layers, four
 * patterns, a click, transpose, mute and a filter on the loop. Shared by the panel (main.c), the sounds (sound.c)
 * and the looper (seq.c). */
#ifndef BEATPEN_BEAT_H
#define BEATPEN_BEAT_H
#include <stdint.h>

#define PADS 12
#define PACKS 4
#define STEPS 32 /* two bars of sixteenths */
#define PATTERNS 4
#define TIE 255u /* a bass step that holds the note before it */

enum { PACK_ROK, PACK_TECHNO, PACK_HIPHOP, PACK_BEATBOX };

/* ---- sound.c: the voices */
void snd_init(void);
void snd_pad(int pad);                   /* hit a pad of the current kit */
void snd_bass_on(int note, int glide);   /* the mono bass: a note (glide: from the one sounding, if any) */
void snd_bass_off(void);
void snd_click(int accent);
void snd_render(int32_t *out, uint32_t frames); /* adds to out, L R L R */
const char *snd_pad_name(int kit, int pad);     /* "KICK" .. */
const char *snd_pad_short(int kit, int pad);    /* "BD" .. two letters */
extern const char *const PACK_NAMES[PACKS];
extern const char *const BASS_NAMES[PACKS];

/* ---- seq.c: the clock and the looper, run from audio() */
typedef struct {
  uint16_t drum[STEPS]; /* bit p: pad p at this step */
  uint8_t bass[STEPS];  /* 0 none, 1..127 a note, TIE */
} pattern_t;

void seq_render(int32_t *out, uint32_t frames); /* the clock, the loop, then the sounds */

/* the panel's requests (loop() writes, audio() takes) */
void seq_play(int on);
void seq_rec(int on);
void seq_pad(int pad);             /* a live pad hit: sounds now, recorded if recording */
void seq_bass(int note, int on);   /* a live bass key (MIDI note), down or up */
void seq_select(int pattern);      /* now when stopped, at the end of the loop when playing */
void seq_undo(void);               /* the pattern as it was before the last recording */
void seq_clear(void);              /* the current pattern, empty */

/* the settings: written by the panel, read by audio() */
typedef struct {
  volatile int16_t bpm;      /* 50..220 */
  volatile int8_t kit, bass; /* sound packs */
  volatile int8_t transpose; /* the loop's bass, semitones -12..12 */
  volatile int16_t filter;   /* -100 (low-pass) .. 0 (off) .. 100 (high-pass) */
  volatile int8_t quant;     /* 0: 1/4, 1: 1/8, 2: 1/16 */
  volatile int8_t glide;     /* 0..100 */
  volatile int8_t drum_lvl, bass_lvl, lofi, click_lvl; /* 0..100 */
  volatile int8_t click;     /* the click track on */
  volatile int8_t mute;      /* bit 0 drums, bit 1 bass (the loop only) */
} settings_t;
extern settings_t S;

/* what the panel shows (audio() writes) */
extern pattern_t PAT[PATTERNS];
extern volatile uint8_t seq_cur, seq_next, seq_playing, seq_recording, seq_step, seq_dirty;
extern volatile uint16_t seq_pad_flash;   /* bit p: pad p sounded lately (the panel clears it) */
extern volatile uint8_t seq_bass_note;    /* the bass note sounding, 0 none */
extern volatile uint8_t seq_beat;         /* bumped on every beat */

#endif
