/* SPDX-License-Identifier: GPL-3.0-only */
/* BEATPEN's sounds: four synthesised drum kits of twelve pads, four mono basses with glide, the click, then the
 * loop's filter and a lo-fi stage on the way out. Everything is synthesised (no samples): kicks and toms are swept
 * sines, snares tone plus noise, hats and cymbals six square oscillators at metallic ratios, claps noise bursts,
 * the hip-hop scratch and brass stab and the beatbox voices are filtered sources and formants. Integers only. */
#include "fm1.h"
#include "beat.h"
#include "tables.h"

#define Q15 32767
#define ONE30 1073741824

static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

static inline int32_t sat15(int32_t x) { /* soft clip, flat at +-1.5 in */
  if (x >= 49152) return Q15;
  if (x <= -49152) return -Q15;
  int32_t x2 = (int32_t)(((int64_t)x * x) >> 15);
  int32_t x3 = (int32_t)(((int64_t)x2 * x) >> 15);
  return x - ((x3 * 4855) >> 15);
}

static uint32_t nz = 22222u;
static inline int32_t noise(void) {
  nz ^= nz << 13, nz ^= nz >> 17, nz ^= nz << 5;
  return (int32_t)nz >> 16;
}

/* a one-pole's coefficient for a corner at hz (Q15): w / (1 + w), w = 2 pi fc / fs */
static inline int32_t onepole_a(int32_t hz) {
  int32_t w = (clampi(hz, 1, 20000) * 4669) >> 10;
  return (int32_t)(((uint32_t)w << 15) / (uint32_t)(32768 + w)); /* (32-bit: w < 2^17) */
}

static inline uint32_t hz_inc(int32_t hz) { return (uint32_t)clampi(hz, 0, 20000) * 97391u; }

/* a decay to 1/e in ms, as a per-sample coefficient (Q16) */
static uint32_t coef(int32_t ms) {
  if (ms < 1) ms = 1;
  return 65536u - (uint32_t)(655360 / (ms * 441));
}
static inline int32_t decay(int32_t e, uint32_t c) { return (int32_t)(((int64_t)e * c) >> 16); }

/* a Chamberlin state-variable filter: f = 2 sin(pi fc / fs) (Q15), q = 1/Q (Q15); keep fc under ~5.5 kHz */
typedef struct {
  int32_t low, band;
} svf_t;
static inline int32_t svf_f(int32_t hz) { return (clampi(hz, 10, 5500) * 4669) >> 10; }
static inline void svf(svf_t *s, int32_t in, int32_t f, int32_t q, int32_t *lp, int32_t *bp, int32_t *hp) {
  s->low += (int32_t)(((int64_t)f * s->band) >> 15);
  int32_t h = in - s->low - (int32_t)(((int64_t)q * s->band) >> 15);
  s->band += (int32_t)(((int64_t)f * h) >> 15);
  s->band = clampi(s->band, -262144, 262144), s->low = clampi(s->low, -262144, 262144);
  if (lp) *lp = s->low;
  if (bp) *bp = s->band;
  if (hp) *hp = h;
}

/* ---------------------------------------------------------------- the kits */

enum { M_KICK, M_SNARE, M_METAL, M_TOM, M_CLAP, M_BELL, M_ZAP, M_SCRATCH, M_STAB, M_VOX, M_NOISE };

typedef struct {
  const char *name, *sh;
  uint8_t model;
  uint16_t f1, f2; /* Hz (see each model) */
  uint16_t pdec;   /* ms: the pitch or filter sweep */
  uint16_t dec;    /* ms: the level */
  uint16_t noise;  /* 0..1024 */
  uint16_t hp;     /* Hz: a high-pass (metal, snare noise) or the voice's pitch (VOX) */
  uint16_t lvl;    /* 0..1024 */
  uint8_t choke;   /* hats that cut each other */
} dsnd_t;

/* KICK f1 -> f2 sweep. SNARE tones f1, f2 + noise. METAL base f1 (205 = the classic ratios), hp. TOM f1 -> f2.
 * CLAP band f1. BELL squares f1, f2. ZAP f1 -> f2. SCRATCH saw f1, band f2. STAB root f1, filter from f2.
 * VOX formants f1, f2, pitch hp, noise = breath. NOISE a band sweeping f1 -> f2 */
static const dsnd_t KITS[PACKS][PADS] = {
    [PACK_ROK] = {
        {"KICK", "BD", M_KICK, 120, 52, 40, 260, 300, 0, 980, 0},
        {"SNARE", "SD", M_SNARE, 185, 330, 60, 170, 700, 900, 900, 0},
        {"HAT CLOSED", "CH", M_METAL, 300, 0, 0, 45, 300, 6500, 860, 1},
        {"HAT OPEN", "OH", M_METAL, 300, 0, 0, 300, 300, 6500, 760, 1},
        {"RIDE", "RD", M_METAL, 410, 0, 0, 700, 120, 3500, 560, 0},
        {"TOM LOW", "LT", M_TOM, 140, 95, 120, 280, 80, 0, 860, 0},
        {"TOM MID", "MT", M_TOM, 190, 130, 110, 240, 80, 0, 840, 0},
        {"TOM HIGH", "HT", M_TOM, 250, 175, 100, 210, 80, 0, 820, 0},
        {"CRASH", "CR", M_METAL, 360, 0, 0, 1100, 500, 4200, 470, 0},
        {"SIDESTICK", "SS", M_BELL, 1700, 840, 0, 25, 300, 0, 700, 0},
        {"COWBELL", "CB", M_BELL, 545, 815, 0, 160, 0, 0, 520, 0},
        {"KICK DOUBLE", "K2", M_KICK, 135, 58, 30, 170, 400, 0, 900, 0},
    },
    [PACK_TECHNO] = {
        {"KICK", "BD", M_KICK, 165, 47, 55, 420, 120, 0, 1020, 0},
        {"CLAP", "CP", M_CLAP, 1150, 0, 0, 190, 1024, 0, 900, 0},
        {"HAT CLOSED", "CH", M_METAL, 340, 0, 0, 38, 500, 7500, 800, 1},
        {"HAT OPEN", "OH", M_METAL, 340, 0, 0, 260, 500, 7500, 700, 1},
        {"SNARE", "SD", M_SNARE, 210, 360, 40, 140, 820, 1500, 860, 0},
        {"RIM", "RM", M_BELL, 1900, 520, 0, 18, 200, 0, 720, 0},
        {"TOM LOW", "LT", M_TOM, 120, 70, 90, 230, 0, 0, 860, 0},
        {"TOM HIGH", "HT", M_TOM, 220, 140, 80, 190, 0, 0, 820, 0},
        {"RIDE", "RD", M_METAL, 470, 0, 0, 600, 250, 5000, 560, 0},
        {"ZAP", "ZP", M_ZAP, 3200, 90, 45, 160, 0, 0, 640, 0},
        {"NOISE RISE", "NR", M_NOISE, 300, 4500, 400, 450, 1024, 0, 520, 0},
        {"BLIP", "BL", M_BELL, 1320, 1980, 0, 60, 0, 0, 520, 0},
    },
    [PACK_HIPHOP] = {
        {"BOOM", "BD", M_KICK, 95, 48, 70, 650, 60, 0, 1020, 0},
        {"SNARE", "SD", M_SNARE, 175, 290, 50, 220, 760, 700, 960, 0},
        {"HAT CLOSED", "CH", M_METAL, 250, 0, 0, 55, 650, 5500, 500, 1},
        {"HAT OPEN", "OH", M_METAL, 250, 0, 0, 360, 650, 5500, 500, 1},
        {"CLAP", "CP", M_CLAP, 950, 0, 0, 260, 1024, 0, 900, 0},
        {"RIM", "RM", M_BELL, 1600, 700, 0, 22, 250, 0, 680, 0},
        {"COWBELL", "CB", M_BELL, 540, 800, 0, 190, 0, 0, 500, 0},
        {"CONGA", "CG", M_TOM, 330, 290, 30, 160, 0, 0, 760, 0},
        {"SCRATCH", "SC", M_SCRATCH, 260, 1400, 0, 220, 380, 0, 820, 0},
        {"SCRATCH BACK", "S2", M_SCRATCH, 190, 1100, 1, 300, 380, 0, 820, 0},
        {"BRASS HIT", "BR", M_STAB, 233, 3800, 220, 420, 0, 0, 780, 0},
        {"BRASS LOW", "B2", M_STAB, 175, 2600, 260, 480, 0, 0, 780, 0},
    },
    [PACK_BEATBOX] = {
        {"BOOM", "BM", M_VOX, 180, 600, 60, 200, 100, 85, 1020, 0},
        {"KCH", "KC", M_VOX, 1900, 3300, 30, 150, 1024, 160, 900, 0},
        {"TS", "TS", M_VOX, 4200, 5300, 0, 50, 1024, 300, 620, 1},
        {"TSSS", "T3", M_VOX, 4000, 5400, 0, 260, 1024, 300, 620, 1},
        {"PFF", "PF", M_VOX, 900, 1800, 0, 120, 1024, 200, 820, 0},
        {"K CLICK", "K", M_VOX, 2600, 3600, 0, 20, 900, 400, 800, 0},
        {"OH", "OH", M_VOX, 520, 900, 120, 280, 80, 140, 760, 0},
        {"AH", "AH", M_VOX, 760, 1250, 120, 260, 80, 160, 760, 0},
        {"EE", "EE", M_VOX, 300, 2300, 120, 240, 80, 190, 700, 0},
        {"CHUH", "CU", M_VOX, 1300, 2400, 40, 180, 900, 150, 820, 0},
        {"THUMP", "TH", M_VOX, 140, 400, 40, 120, 200, 70, 960, 0},
        {"ZIP", "ZI", M_VOX, 2000, 3500, 25, 110, 400, 900, 700, 0},
    },
};

const char *const PACK_NAMES[PACKS] = {"ROK", "TECHNO", "HIPHOP", "BEATBOX"};
const char *const BASS_NAMES[PACKS] = {"ROK", "TECHNO", "HIPHOP", "BEATBOX"};

const char *snd_pad_name(int kit, int pad) { return KITS[clampi(kit, 0, PACKS - 1)][clampi(pad, 0, PADS - 1)].name; }
const char *snd_pad_short(int kit, int pad) { return KITS[clampi(kit, 0, PACKS - 1)][clampi(pad, 0, PADS - 1)].sh; }

/* ---------------------------------------------------------------- drum voices */

#define VOICES 8

typedef struct {
  const dsnd_t *s;
  int8_t pad;
  uint32_t t;
  int32_t env, penv; /* Q30 */
  uint32_t ac, pc;   /* Q16 */
  uint32_t ph[6];
  int32_t hp1, hp2, hpa; /* hpa: the high-pass's coefficient, set at the hit */
  svf_t f1, f2;
} voice_t;

static voice_t V[VOICES];
static uint32_t age;
static uint32_t vage[VOICES];

/* the six metallic partials, as ratios to 205.3 Hz (x 1024) */
static const uint16_t METAL_R[6] = {1024, 1518, 1844, 2607, 2693, 3990};

static void voice_start(voice_t *v, const dsnd_t *s, int pad) {
  v->s = s, v->pad = (int8_t)pad, v->t = 0;
  v->env = ONE30, v->penv = ONE30;
  v->ac = coef(s->dec), v->pc = coef(s->pdec ? s->pdec : 1);
  for (int i = 0; i < 6; i++) v->ph[i] = (uint32_t)i * 715827883u;
  v->hp1 = v->hp2 = 0, v->hpa = onepole_a(s->hp ? s->hp : 1);
  v->f1 = (svf_t){0, 0}, v->f2 = (svf_t){0, 0};
}

/* from audio() (seq.c): the looper's hits and the live ones it passes on */
void snd_pad(int pad) {
  if (pad < 0 || pad >= PADS) return;
  const dsnd_t *s = &KITS[clampi(S.kit, 0, PACKS - 1)][pad];
  int best = -1;
  for (int i = 0; i < VOICES; i++) {
    if (V[i].s && V[i].pad == pad) best = i; /* the same pad: retrigger */
    if (s->choke && V[i].s && V[i].s->choke == s->choke && V[i].pad != pad) V[i].ac = coef(4); /* choke */
  }
  if (best < 0)
    for (int i = 0; i < VOICES; i++)
      if (!V[i].s) {
        best = i;
        break;
      }
  if (best < 0) { /* steal the oldest */
    best = 0;
    for (int i = 1; i < VOICES; i++)
      if (vage[i] < vage[best]) best = i;
  }
  voice_start(&V[best], s, pad);
  vage[best] = ++age;
}

static int32_t voice_sample(voice_t *v) {
  const dsnd_t *s = v->s;
  int32_t e15 = v->env >> 15, p15 = v->penv >> 15, out = 0;
  switch (s->model) {
    case M_KICK:
    case M_TOM:
    case M_ZAP: {
      int32_t hz = s->f2 + (((s->f1 - s->f2) * p15) >> 15);
      v->ph[0] += hz_inc(hz);
      out = sine(v->ph[0]);
      if (s->model == M_KICK && v->t < 130) out += (noise() * s->noise) >> 10; /* the beater */
      else if (s->model == M_TOM) out += (noise() * s->noise) >> 13;
      if (s->model == M_KICK) out = sat15(out + (out >> 1)); /* a little drive */
      break;
    }
    case M_SNARE: {
      v->ph[0] += hz_inc(s->f1), v->ph[1] += hz_inc(s->f2);
      int32_t tone = ((sine(v->ph[0]) + (sine(v->ph[1]) >> 1)) * p15) >> 15;
      int32_t n = noise();
      v->hp1 += ((n - v->hp1) * v->hpa) >> 15;
      n -= v->hp1;
      out = ((tone * (1024 - s->noise)) >> 10) + ((n * s->noise) >> 10);
      break;
    }
    case M_METAL: {
      int32_t sum = 0;
      for (int i = 0; i < 6; i++) {
        v->ph[i] += hz_inc((s->f1 * METAL_R[i]) >> 10);
        sum += v->ph[i] < 2147483648u ? 5461 : -5461;
      }
      sum = ((sum * (1024 - s->noise)) >> 10) + ((noise() * s->noise) >> 10);
      int32_t a = v->hpa;
      v->hp1 += ((sum - v->hp1) * a) >> 15;
      sum -= v->hp1;
      v->hp2 += ((sum - v->hp2) * a) >> 15;
      out = sat15((sum - v->hp2) * 4);
      break;
    }
    case M_CLAP: {
      int32_t lp, bp;
      svf(&v->f1, noise(), svf_f(s->f1), 9000, &lp, &bp, 0);
      out = bp * 2;
      if (v->t < 3 * 441) { /* three quick hand claps, then the room */
        int32_t k = 441 - (int32_t)(v->t % 441);
        e15 = (k * 74) >> 0;
        e15 = clampi(e15, 0, Q15);
        v->env = ONE30;
      }
      break;
    }
    case M_BELL: {
      v->ph[0] += hz_inc(s->f1), v->ph[1] += hz_inc(s->f2);
      int32_t sq = (v->ph[0] < 2147483648u ? 12000 : -12000) + (v->ph[1] < 2147483648u ? 12000 : -12000);
      int32_t lp, bp;
      svf(&v->f1, sq + ((noise() * s->noise) >> 10), svf_f(2600), 16000, &lp, &bp, 0);
      out = bp * 2;
      break;
    }
    case M_SCRATCH: { /* a record pushed forward and pulled back: the speed is a fast triangle */
      if (v->t == 0) v->ph[4] = 0xFFFFFFFFu / ((uint32_t)s->dec * 441u / 10u + 1u); /* the gesture's rate */
      uint64_t pp = (uint64_t)v->t * v->ph[4];
      uint32_t ph = pp > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)pp;
      int32_t tri = (ph >> 31) ? (int32_t)(~ph >> 16) : (int32_t)(ph >> 16); /* 0..32767 .. 0 */
      if (s->pdec) tri = Q15 - tri;                                          /* the pull back first */
      int32_t m = 6000 + ((tri * 26767) >> 15);
      v->ph[0] += hz_inc((s->f1 * m) >> 15);
      int32_t saw = (int32_t)(v->ph[0] >> 16) - 32768;
      int32_t src = ((saw * (1024 - s->noise)) >> 10) + ((noise() * s->noise) >> 10);
      int32_t lp, bp;
      svf(&v->f1, src, svf_f((s->f2 * m) >> 15), 7000, &lp, &bp, 0);
      out = bp * 2;
      if (ph > 4100000000u) out = 0; /* the hand stops */
      break;
    }
    case M_STAB: { /* three saws (root, fifth, octave) through a closing low-pass */
      static const uint16_t R[3] = {1024, 1534, 2051};
      int32_t sum = 0;
      for (int i = 0; i < 3; i++) {
        v->ph[i] += hz_inc((s->f1 * R[i]) >> 10);
        sum += ((int32_t)(v->ph[i] >> 16) - 32768) / 3;
      }
      int32_t fc = 120 + ((s->f2 * p15) >> 15), lp;
      svf(&v->f1, sum, svf_f(fc), 14000, &lp, 0, 0);
      svf(&v->f2, lp, svf_f(fc), 20000, &lp, 0, 0);
      out = sat15(lp * 2);
      break;
    }
    case M_VOX: { /* a mouth: a buzz (pitch hp) or breath through two formants f1, f2 */
      int32_t pitch = s->hp + ((s->hp * p15) >> 16);
      v->ph[0] += hz_inc(pitch);
      int32_t buzz = (int32_t)(v->ph[0] >> 16) - 32768;
      int32_t src = ((buzz * (1024 - s->noise)) >> 10) + ((noise() * s->noise) >> 10);
      int32_t b1, b2;
      svf(&v->f1, src, svf_f(s->f1), 5000, 0, &b1, 0);
      svf(&v->f2, src, svf_f(s->f2), 6500, 0, &b2, 0);
      out = sat15(b1 + (b2 >> 1));
      break;
    }
    case M_NOISE: {
      int32_t fc = s->f2 + (((s->f1 - s->f2) * p15) >> 15), bp;
      svf(&v->f1, noise(), svf_f(fc), 6000, 0, &bp, 0);
      int32_t swell = clampi((int32_t)(v->t / 7), 0, Q15); /* it swells, then fades */
      if (swell < e15) e15 = swell;
      out = bp * 2;
      break;
    }
  }
  v->env = decay(v->env, v->ac);
  v->penv = decay(v->penv, v->pc);
  v->t++;
  if (v->env < 16384) v->s = 0; /* faded out */
  return (int32_t)(((int64_t)out * e15 >> 15) * s->lvl >> 10);
}

/* ---------------------------------------------------------------- the bass */

static struct {
  volatile int32_t target; /* semitone x 256 */
  volatile uint8_t gate, trig, slide;
  int32_t pitch;
  int32_t env, fenv; /* Q30 */
  uint32_t ph, vib;
  uint32_t t;
  svf_t f1, f2;
  int sounding;
} B;

void snd_bass_on(int note, int glide) {
  B.target = clampi(note, 12, 108) * 256;
  B.slide = (uint8_t)(glide && B.sounding);
  B.trig = 1, B.gate = 1;
}
void snd_bass_off(void) { B.gate = 0; }

static uint32_t note_inc_q8(int32_t p) { /* a pitch in semitones x 256 */
  int n = p >> 8, f = p & 255;
  uint32_t a = note_inc(n), b = note_inc(n + 1);
  return a + (uint32_t)(((uint64_t)(b - a) * (uint32_t)f) >> 8);
}

static int32_t bass_sample(void) {
  int kind = clampi(S.bass, 0, PACKS - 1);
  if (B.trig) {
    B.trig = 0;
    if (!B.slide) B.pitch = B.target, B.t = 0; /* the attack starts from where the level is: no click */
    B.fenv = ONE30;
    B.sounding = 1;
  }
  if (!B.sounding) return 0;
  /* glide toward the target */
  int32_t gl = clampi(S.glide, 0, 100);
  if (gl == 0 || !B.slide) B.pitch = B.target;
  else B.pitch += (int32_t)(((int64_t)(B.target - B.pitch) * (int32_t)(65536 - coef(gl * 4))) >> 16);
  int32_t p = B.pitch;
  if (kind == PACK_HIPHOP && B.t < 2000) p += (7 * 256 * (2000 - (int32_t)B.t)) / 2000; /* the 808's drop */
  if (kind == PACK_BEATBOX && B.t > 8820) { /* a hum's vibrato after 200 ms */
    B.vib += 486958u;                           /* ~5 Hz */
    p += sine(B.vib) >> 10;
  }
  B.ph += note_inc_q8(p);
  int32_t s, lp, bp;
  int32_t f15 = B.fenv >> 15;
  switch (kind) {
    case PACK_ROK: { /* a picked bass: saw, low-pass that closes after the pick */
      s = (int32_t)(B.ph >> 16) - 32768;
      svf(&B.f1, s, svf_f(260 + ((2400 * f15) >> 15)), 23000, &lp, 0, 0);
      s = lp;
      B.fenv = decay(B.fenv, coef(150));
      break;
    }
    case PACK_TECHNO: { /* an acid line: square, resonant low-pass with a snappy envelope */
      s = B.ph < 2147483648u ? 20000 : -20000;
      svf(&B.f1, s, svf_f(220 + ((3300 * f15) >> 15)), 5200, &lp, 0, 0);
      s = sat15(lp);
      B.fenv = decay(B.fenv, coef(180));
      break;
    }
    case PACK_HIPHOP: /* an 808: a driven sine */
      s = sat15(sine(B.ph) + (sine(B.ph) >> 1));
      break;
    default: { /* a voice humming "oo" */
      s = (int32_t)(B.ph >> 16) - 32768;
      svf(&B.f1, s, svf_f(330), 5000, 0, &bp, 0);
      svf(&B.f2, s, svf_f(820), 7000, 0, &lp, 0);
      s = sat15(bp + (lp >> 2));
      break;
    }
  }
  /* the level: attack, decay to a sustain while held, release */
  int32_t sus = kind == PACK_HIPHOP ? 0 : kind == PACK_ROK ? ONE30 / 2 : (ONE30 / 10) * 8;
  if (B.gate) {
    if (B.t < 88) B.env += (ONE30 - B.env) >> 5; /* 2 ms attack */
    else B.env = sus + decay(B.env - sus, coef(kind == PACK_HIPHOP ? 1500 : 300));
  } else {
    B.env = decay(B.env, coef(kind == PACK_HIPHOP ? 250 : kind == PACK_TECHNO ? 30 : 110));
    if (B.env < 65536) B.sounding = 0, B.env = 0;
  }
  B.t++;
  return (int32_t)(((int64_t)s * (B.env >> 15)) >> 15);
}

/* ---------------------------------------------------------------- the click */

static uint32_t ck_ph, ck_inc;
static int32_t ck_env;
void snd_click(int accent) { ck_inc = hz_inc(accent ? 2000 : 1400), ck_env = ONE30, ck_ph = 0; }

/* ---------------------------------------------------------------- the way out */

static int32_t fl1, fl2, fs1, fs2; /* the loop filter's two one-poles, Q8 */
static int32_t lf_hold, lf_cnt;

void snd_init(void) {
  for (int i = 0; i < VOICES; i++) V[i].s = 0;
  B.sounding = 0, B.env = 0;
}

void snd_render(int32_t *out, uint32_t frames) {
  int32_t dl = clampi(S.drum_lvl, 0, 100) * 10, bl = clampi(S.bass_lvl, 0, 100) * 10, cl = clampi(S.click_lvl, 0, 100) * 8;
  int32_t flt = clampi(S.filter, -100, 100), lofi = clampi(S.lofi, 0, 100);
  /* the filter: left low-pass 12 kHz -> 120 Hz, right high-pass 30 Hz -> 3 kHz; the centre is off */
  int32_t G = 0, mode = 0;
  if (flt < -3) mode = 1, G = FILT_G[clampi(950 - (((-flt) - 3) * 640) / 97, 0, 1023)];
  else if (flt > 3) mode = 2, G = FILT_G[clampi(60 + ((flt - 3) * 560) / 97, 0, 1023)];
  int32_t dec = 1 + lofi / 18, bits = lofi / 9; /* hold 1..6 samples, drop 0..11 bits */
  for (uint32_t i = 0; i < frames; i++) {
    int32_t d = 0;
    for (int k = 0; k < VOICES; k++)
      if (V[k].s) d += voice_sample(&V[k]);
    int32_t b = bass_sample();
    int32_t c = 0;
    if (ck_env > 65536) {
      ck_ph += ck_inc;
      c = (sine(ck_ph) * (ck_env >> 15)) >> 15;
      ck_env = decay(ck_env, coef(12));
    }
    int32_t x = ((d * dl) >> 10) + ((b * bl) >> 10) + ((c * cl) >> 10);
    if (mode) { /* two TPT one-poles: 12 dB/oct */
      int32_t X = x * 256, v, y1, y2;
      v = (int32_t)(((int64_t)(X - fs1) * G) >> 16), y1 = v + fs1, fs1 = y1 + v;
      v = (int32_t)(((int64_t)(y1 - fs2) * G) >> 16), y2 = v + fs2, fs2 = y2 + v;
      if (mode == 1) x = y2 >> 8;
      else { /* high-pass: what the cascade leaves */
        int32_t h1 = X - y1;
        v = (int32_t)(((int64_t)(h1 - fl1) * G) >> 16), fl2 = v + fl1, fl1 = fl2 + v;
        x = (h1 - fl2) >> 8;
      }
    }
    /* lo-fi: fewer samples, fewer bits */
    if (++lf_cnt >= dec) lf_cnt = 0, lf_hold = x;
    x = lf_hold;
    if (bits) x &= ~((1 << bits) - 1);
    x = sat15((x * 3) >> 2);
    out[2 * i] += x;
    out[2 * i + 1] += x;
  }
}
