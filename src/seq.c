/* SPDX-License-Identifier: GPL-3.0-only */
/* BEATPEN's clock and looper, run in audio(): 24 ticks a beat, two bars (192 ticks, 32 sixteenth steps), four
 * patterns. Recording is an overdub: every pad hit and bass note is snapped to the QUANT grid and added to the
 * pattern, layer over layer, until REC is pressed again; UNDO swaps the pattern with its copy from before the
 * recording. The panel talks to it through a small queue, so the timing stays here, to the sample. */
#include "fm1.h"
#include "beat.h"

#ifndef RING_PUBLISH
#define RING_PUBLISH() __asm__ volatile("" ::: "memory") /* the entry is written before the index moves */
#endif

#define TICKS_BEAT 24
#define TICKS_STEP 6
#define LOOP_TICKS (STEPS * TICKS_STEP)

settings_t S = {.bpm = 96, .kit = PACK_HIPHOP, .bass = PACK_HIPHOP, .quant = 2, .glide = 20,
                .drum_lvl = 90, .bass_lvl = 80, .lofi = 22, .click_lvl = 60, .click = 1};
pattern_t PAT[PATTERNS];
volatile uint8_t seq_cur, seq_next, seq_playing, seq_recording, seq_step, seq_dirty;
volatile uint16_t seq_pad_flash;
volatile uint8_t seq_bass_note, seq_beat;

/* ---- the queue from the panel */
enum { EV_PAD, EV_BASS_ON, EV_BASS_OFF, EV_PLAY, EV_STOP, EV_REC_ON, EV_REC_OFF, EV_SELECT, EV_UNDO, EV_CLEAR };
typedef struct {
  uint8_t type, a;
} ev_t;
#define QN 32
static ev_t q[QN];
static volatile uint8_t qw, qr;

static void push(uint8_t type, uint8_t a) {
  uint8_t n = (uint8_t)((qw + 1) % QN);
  if (n == qr) return; /* full: drop it */
  q[qw].type = type, q[qw].a = a;
  RING_PUBLISH();
  qw = n;
}
void seq_play(int on) { push(on ? EV_PLAY : EV_STOP, 0); }
void seq_rec(int on) { push(on ? EV_REC_ON : EV_REC_OFF, 0); }
void seq_pad(int pad) { push(EV_PAD, (uint8_t)pad); }
void seq_bass(int note, int on) { push(on ? EV_BASS_ON : EV_BASS_OFF, (uint8_t)note); }
void seq_select(int p) { push(EV_SELECT, (uint8_t)p); }
void seq_undo(void) { push(EV_UNDO, 0); }
void seq_clear(void) { push(EV_CLEAR, 0); }

/* ---- the looper's state (audio() only) */
static uint32_t tick, acc; /* acc: samples into this tick, Q8 */
static uint16_t skip[STEPS];  /* hits recorded ahead of the playhead this pass: already heard */
static uint8_t skip_bass[STEPS];
static pattern_t undo;
static int8_t undo_pat = -1;
static uint8_t live_note, loop_bass_on, rec_bass_active, rec_bass_step;

static int grid_ticks(void) { return S.quant <= 0 ? TICKS_BEAT : S.quant == 1 ? TICKS_BEAT / 2 : TICKS_STEP; }

/* where a hit made now lands: the nearest grid line; ahead = the playhead has not reached it this pass */
static int snap(int *ahead) {
  int g = grid_ticks();
  int t = (int)tick;
  int qt = ((t + g / 2) / g) * g;
  *ahead = qt > t;
  if (qt >= LOOP_TICKS) qt -= LOOP_TICKS;
  return qt / TICKS_STEP;
}

static void loop_bass_release(void) {
  if (loop_bass_on && !live_note) snd_bass_off();
  loop_bass_on = 0;
  if (!live_note) seq_bass_note = 0;
}

static void snapshot(void) {
  undo = PAT[seq_cur];
  undo_pat = (int8_t)seq_cur;
}

static void play_step(int s) {
  pattern_t *p = &PAT[seq_cur];
  /* drums */
  uint16_t m = p->drum[s] & (uint16_t)~skip[s];
  skip[s] = 0;
  if (!(S.mute & 1) && m) {
    for (int i = 0; i < PADS; i++)
      if (m >> i & 1u) snd_pad(i);
    seq_pad_flash |= m;
  }
  /* the bass: a live key wins */
  if (seq_recording && live_note && rec_bass_active && s != rec_bass_step) p->bass[s] = TIE, seq_dirty++;
  if (!live_note) {
    uint8_t v = (S.mute & 2) ? 0 : p->bass[s];
    if (skip_bass[s]) skip_bass[s] = 0, v = TIE;
    if (v && v != TIE) {
      int prev = p->bass[(s + STEPS - 1) % STEPS];
      int n = v + S.transpose;
      snd_bass_on(n, loop_bass_on && prev != 0);
      loop_bass_on = 1, seq_bass_note = (uint8_t)n;
    } else if (!v) {
      loop_bass_release();
    }
  }
}

static void on_tick(void) {
  if (tick % TICKS_BEAT == 0) {
    seq_beat++;
    if (S.click) snd_click(tick % (TICKS_BEAT * 4) == 0);
  }
  if (tick % TICKS_STEP == 0) {
    seq_step = (uint8_t)(tick / TICKS_STEP);
    play_step(seq_step);
  }
}

static void take_events(void) {
  while (qr != qw) {
    ev_t e = q[qr];
    qr = (uint8_t)((qr + 1) % QN);
    int ahead, s;
    switch (e.type) {
      case EV_PAD:
        snd_pad(e.a);
        seq_pad_flash |= (uint16_t)(1u << e.a);
        if (seq_recording && seq_playing) {
          s = snap(&ahead);
          PAT[seq_cur].drum[s] |= (uint16_t)(1u << e.a);
          if (ahead) skip[s] |= (uint16_t)(1u << e.a);
          seq_dirty++;
        }
        break;
      case EV_BASS_ON:
        snd_bass_on(e.a, live_note != 0 || loop_bass_on);
        live_note = e.a, seq_bass_note = e.a, loop_bass_on = 0;
        if (seq_recording && seq_playing) {
          s = snap(&ahead);
          PAT[seq_cur].bass[s] = (uint8_t)(e.a - S.transpose); /* stored as played under this transpose */
          rec_bass_step = (uint8_t)s, rec_bass_active = 1;
          if (ahead) skip_bass[s] = 1;
          seq_dirty++;
        }
        break;
      case EV_BASS_OFF:
        if (e.a == live_note) {
          snd_bass_off();
          live_note = 0, seq_bass_note = 0, rec_bass_active = 0;
        }
        break;
      case EV_PLAY:
        if (!seq_playing) {
          seq_playing = 1, tick = 0, acc = 0;
          for (int i = 0; i < STEPS; i++) skip[i] = 0, skip_bass[i] = 0;
          on_tick();
        }
        break;
      case EV_STOP:
        seq_playing = 0, seq_recording = 0;
        loop_bass_release();
        if (seq_next != seq_cur) seq_cur = seq_next;
        break;
      case EV_REC_ON:
        snapshot();
        seq_recording = 1;
        if (!seq_playing) { /* REC while stopped starts the loop too */
          seq_playing = 1, tick = 0, acc = 0;
          on_tick();
        }
        break;
      case EV_REC_OFF:
        seq_recording = 0, rec_bass_active = 0;
        break;
      case EV_SELECT:
        seq_next = (uint8_t)(e.a % PATTERNS);
        if (!seq_playing) seq_cur = seq_next;
        break;
      case EV_UNDO:
        if (undo_pat == (int8_t)seq_cur) { /* swap: again is redo */
          pattern_t t = PAT[seq_cur];
          PAT[seq_cur] = undo, undo = t;
          seq_dirty++;
        }
        break;
      case EV_CLEAR:
        snapshot();
        for (int i = 0; i < STEPS; i++) PAT[seq_cur].drum[i] = 0, PAT[seq_cur].bass[i] = 0;
        loop_bass_release();
        seq_dirty++;
        break;
    }
  }
}

void seq_render(int32_t *out, uint32_t frames) {
  take_events();
  int32_t bpm = S.bpm < 40 ? 40 : S.bpm > 240 ? 240 : S.bpm;
  uint32_t spt = 28224000u / (uint32_t)bpm; /* samples per tick, Q8: 44100 x 60 / (bpm x 24) */
  uint32_t done = 0;
  while (done < frames) {
    if (!seq_playing) {
      snd_render(out + 2 * done, frames - done);
      break;
    }
    if (acc >= spt) acc = spt - 1;
    uint32_t until = (spt - acc + 255u) >> 8, n = frames - done;
    if (until < n) n = until;
    if (n == 0) n = 1;
    snd_render(out + 2 * done, n);
    done += n, acc += n << 8;
    if (acc >= spt) {
      acc -= spt;
      tick = (tick + 1) % LOOP_TICKS;
      if (tick == 0 && seq_next != seq_cur) { /* the loop's end: the queued pattern */
        loop_bass_release();
        seq_cur = seq_next;
      }
      on_tick();
    }
  }
}
