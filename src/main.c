/* SPDX-License-Identifier: GPL-3.0-only */
/* BEATPEN: a stylus-style beat maker for the M-VAVE FM-1, after the Stylophone BEAT.
 *
 * The panel (README.md has the full map):
 *   keys            the 12 pads (DRUMS), repeated along the keyboard; or the bass, chromatic (BASS)
 *   EDIT            DRUMS <-> BASS                 OCT- OCT+    the bass's octave
 *   PRESETS         the drum kit: ROK, TECHNO, HIPHOP, BEATBOX       ALGORITHM   the bass sound, the same four
 *   SELECT          the tempo                      PLAY  start / stop      REC   record layers (stopped: starts too)
 *   FX SCL ENV LFO  patterns 1..4 (playing: at the end of the loop)
 *   ARP             the click                      GLO   mute: drums, bass, both, none (the loop only)
 *   SEQ             undo the last recording (again: redo); held: clear the pattern
 *   HOME            the knobs' other page          SAVE  keep the patterns and settings in flash
 *   KNOB1..4        TRANSPOSE, FILTER, QUANT, GLIDE / DRUMS, BASS, LO-FI, CLICK levels
 *   MASTER          the volume
 *   MIDI            channel 10: the pads (note 36 = pad 1); other channels: as the mode */
#include "fm1.h"
#include "beat.h"

/* ---------------------------------------------------------------- colours */

#define C_BG RGB(14, 14, 16)
#define C_HEAD RGB(26, 26, 30)
#define C_TEXT RGB(236, 232, 220)
#define C_DIM RGB(120, 118, 112)
#define C_FAINT RGB(50, 50, 56)
#define C_PLATE RGB(150, 152, 158)
#define C_PLATE_D RGB(96, 98, 104)
#define C_PAD RGB(206, 208, 214)
#define C_PAD_HI RGB(244, 245, 248)
#define C_DRUM RGB(255, 120, 60)
#define C_BASS RGB(70, 196, 255)
#define C_REC RGB(240, 54, 64)
#define C_PLAY RGB(110, 220, 120)
#define C_GOLD RGB(244, 200, 80)

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static char *put(char *d, const char *s) {
  while (*s) *d++ = *s++;
  *d = 0;
  return d;
}
static char *put_int(char *d, int32_t v) {
  char b[12];
  format_int(b, v);
  return put(d, b);
}
static int center_text(int cx, int y, const char *s, uint16_t c, int font) {
  return text(cx - text_width(s, font) / 2, y, s, c, font);
}

/* ---------------------------------------------------------------- the knobs */

typedef struct {
  const char *label;
  volatile int8_t *v8;
  volatile int16_t *v16;
  int16_t lo, hi, step;
} knob_t;

static const knob_t KNOBS[2][4] = {
    {{"TRANSP", &S.transpose, 0, -12, 12, 1},
     {"FILTER", 0, &S.filter, -100, 100, 5},
     {"QUANT", &S.quant, 0, 0, 2, 1},
     {"GLIDE", &S.glide, 0, 0, 100, 5}},
    {{"DRUMS", &S.drum_lvl, 0, 0, 100, 5},
     {"BASS", &S.bass_lvl, 0, 0, 100, 5},
     {"LO-FI", &S.lofi, 0, 0, 100, 5},
     {"CLICK", &S.click_lvl, 0, 0, 100, 5}},
};

static int32_t knob_get(const knob_t *k) { return k->v8 ? *k->v8 : *k->v16; }
static void knob_set(const knob_t *k, int32_t v) {
  v = clampi(v, k->lo, k->hi);
  if (k->v8) *k->v8 = (int8_t)v;
  else *k->v16 = (int16_t)v;
}

static void knob_text(char *d, int page, int i) {
  int32_t v = knob_get(&KNOBS[page][i]);
  if (page == 0 && i == 0) put_int(put(d, v > 0 ? "+" : ""), v);
  else if (page == 0 && i == 1) {
    if (v == 0) put(d, "OFF");
    else put_int(put(d, v < 0 ? "LP " : "HP "), v < 0 ? -v : v);
  } else if (page == 0 && i == 2) put(d, v == 0 ? "1/4" : v == 1 ? "1/8" : "1/16");
  else put_int(d, v);
}

/* ---------------------------------------------------------------- what is kept in flash */

#define STORE_MAGIC 0x42545031u /* "BTP1" */
static struct {
  uint32_t magic;
  int16_t bpm, filter;
  int8_t kit, bass, transpose, quant, glide, drum_lvl, bass_lvl, lofi, click_lvl, click, oct, cur;
  pattern_t pat[PATTERNS];
} store;

static int8_t oct; /* the bass keys' octave */

static void store_save(void) {
  store.magic = STORE_MAGIC;
  store.bpm = S.bpm, store.filter = S.filter, store.kit = S.kit, store.bass = S.bass, store.transpose = S.transpose;
  store.quant = S.quant, store.glide = S.glide, store.drum_lvl = S.drum_lvl, store.bass_lvl = S.bass_lvl;
  store.lofi = S.lofi, store.click_lvl = S.click_lvl, store.click = S.click, store.oct = oct, store.cur = (int8_t)seq_cur;
  for (int p = 0; p < PATTERNS; p++) store.pat[p] = PAT[p];
  settings_save(&store, sizeof store);
}

/* a starting beat in pattern 1, so PLAY makes a groove at once */
static void demo_pattern(void) {
  pattern_t *p = &PAT[0];
  static const uint8_t KICK[] = {0, 7, 10, 16, 23, 26}, SNARE[] = {4, 12, 20, 28};
  for (unsigned i = 0; i < sizeof KICK; i++) p->drum[KICK[i]] |= 1u << 0;
  for (unsigned i = 0; i < sizeof SNARE; i++) p->drum[SNARE[i]] |= 1u << 1;
  for (int s = 0; s < STEPS; s += 2) p->drum[s] |= 1u << 2;
  p->drum[30] |= 1u << 3;
  static const uint8_t BL[STEPS] = {36, TIE, TIE, 0, 0, 0, 0, 36, 0, 0, 39, TIE, 0, 0, 41, 0,
                                    36, TIE, TIE, 0, 0, 0, 0, 36, 0, 0, 43, TIE, 41, 0, 39, 0};
  for (int s = 0; s < STEPS; s++) p->bass[s] = BL[s];
}

/* ---------------------------------------------------------------- the panel's state */

static uint8_t mode_bass, kpage, held_note[KEY_COUNT];
static uint32_t seq_down_t, saved_t;
static uint8_t seq_held_cleared;
static uint32_t flash_seen[PADS];

/* what is on the screen */
static int8_t drawn;
static uint32_t sh_head = ~0u, sh_info = ~0u, sh_pads = ~0u, sh_grid = ~0u, sh_stat = ~0u, sh_knobs = ~0u;
static uint32_t led_k_now = ~0u, led_b_now = ~0u;

/* ---------------------------------------------------------------- drawing */

#define INFO_Y 24
#define PLATE_Y 64
#define PLATE_H 66
#define GRID_Y 136
#define STAT_Y 176
#define KNOB_Y 202

static void draw_head(void) {
  rect(0, 0, SCREEN_W, 22, C_HEAD);
  text(6, 3, "BEATPEN", C_DRUM, FONT_SMALL);
  char b[16];
  put(put_int(b, S.bpm), " BPM");
  text(SCREEN_W - 6 - text_width(b, FONT_SMALL), 3, b, C_TEXT, FONT_SMALL);
}

static void draw_info(void) {
  rect(0, INFO_Y, SCREEN_W, PLATE_Y - INFO_Y - 2, C_BG);
  const char *m = mode_bass ? "BASS" : "DRUMS";
  uint16_t c = mode_bass ? C_BASS : C_DRUM;
  rect(6, INFO_Y + 2, 92, 34, c);
  center_text(52, INFO_Y + 3, m, C_BG, FONT_BIG);
  text(108, INFO_Y + 2, "KIT", C_DIM, FONT_SMALL);
  text(148, INFO_Y + 2, PACK_NAMES[clampi(S.kit, 0, 3)], mode_bass ? C_TEXT : C_DRUM, FONT_SMALL);
  text(108, INFO_Y + 20, "BASS", C_DIM, FONT_SMALL);
  char b[16], *d = put(b, BASS_NAMES[clampi(S.bass, 0, 3)]);
  if (mode_bass) {
    d = put(d, " ");
    if (oct >= 0) d = put(d, "+");
    put_int(d, oct);
  }
  text(148, INFO_Y + 20, b, mode_bass ? C_BASS : C_TEXT, FONT_SMALL);
}

/* the metal plate and its 12 pads */
static void draw_pads(uint16_t lit, int bass_pad) {
  static const char *const NOTE[12] = {"F", "F#", "G", "G#", "A", "A#", "B", "C", "C#", "D", "D#", "E"};
  rect(2, PLATE_Y, SCREEN_W - 4, PLATE_H, C_PLATE);
  frame(2, PLATE_Y, SCREEN_W - 4, PLATE_H, C_PLATE_D);
  line(3, PLATE_Y + 1, SCREEN_W - 4, PLATE_Y + 1, C_PAD_HI);
  for (int i = 0; i < PADS; i++) {
    int x = 7 + i * 19, on = mode_bass ? (i == bass_pad) : (lit >> i & 1u);
    uint16_t c = on ? (mode_bass ? C_BASS : C_DRUM) : C_PAD;
    rect(x, PLATE_Y + 6, 16, 38, c);
    line(x, PLATE_Y + 6, x + 15, PLATE_Y + 6, on ? C_PAD_HI : C_PAD_HI);
    line(x + 15, PLATE_Y + 6, x + 15, PLATE_Y + 43, C_PLATE_D);
    const char *l = mode_bass ? NOTE[i] : snd_pad_short(S.kit, i);
    center_text(x + 8, PLATE_Y + 47, l, RGB(30, 30, 34), FONT_SMALL);
  }
}

/* the loop: drums above, bass below, the playhead */
static void draw_grid(void) {
  rect(0, GRID_Y - 2, SCREEN_W, STAT_Y - GRID_Y, C_BG);
  const pattern_t *p = &PAT[seq_cur];
  for (int s = 0; s < STEPS; s++) {
    int x = 8 + s * 7;
    uint16_t bg = (s % 4 == 0) ? RGB(58, 58, 66) : C_FAINT;
    rect(x, GRID_Y, 6, 14, p->drum[s] ? C_DRUM : bg);
    uint8_t b = p->bass[s];
    rect(x, GRID_Y + 17, 6, 14, b == TIE ? RGB(40, 110, 150) : b ? C_BASS : bg);
    if (b && b != TIE && (s == STEPS - 1 || p->bass[s + 1] == TIE)) rect(x + 6, GRID_Y + 22, 1, 4, RGB(40, 110, 150));
  }
  if (seq_playing) {
    int x = 8 + seq_step * 7;
    frame(x - 1, GRID_Y - 1, 8, 33, seq_recording ? C_REC : C_TEXT);
  }
  pixel(8 + 16 * 7 - 1, GRID_Y + 32, C_DIM); /* the bar line */
  rect(8 + 16 * 7 - 1, GRID_Y + 32, 1, 3, C_DIM);
}

static void draw_stat(uint32_t now) {
  rect(0, STAT_Y, SCREEN_W, KNOB_Y - STAT_Y - 2, C_BG);
  for (int i = 0; i < PATTERNS; i++) {
    int x = 6 + i * 26;
    char b[2] = {(char)('1' + i), 0};
    if (i == seq_cur) {
      rect(x, STAT_Y + 2, 22, 20, C_GOLD);
      center_text(x + 11, STAT_Y + 4, b, C_BG, FONT_SMALL);
    } else {
      frame(x, STAT_Y + 2, 22, 20, i == seq_next && ((now / 250) & 1) ? C_GOLD : C_FAINT);
      center_text(x + 11, STAT_Y + 4, b, C_DIM, FONT_SMALL);
    }
  }
  const char *st = seq_recording ? "REC" : seq_playing ? "PLAY" : "STOP";
  uint16_t sc = seq_recording ? C_REC : seq_playing ? C_PLAY : C_DIM;
  if (now - saved_t < 1500 && saved_t) st = "SAVED", sc = C_GOLD;
  text(116, STAT_Y + 4, st, sc, FONT_SMALL);
  if (S.click) text(166, STAT_Y + 4, "CLK", C_TEXT, FONT_SMALL);
  if (S.mute) text(200, STAT_Y + 4, S.mute == 1 ? "M:D" : S.mute == 2 ? "M:B" : "M:DB", C_REC, FONT_SMALL);
}

static void draw_knobs(void) {
  rect(0, KNOB_Y, SCREEN_W, SCREEN_H - KNOB_Y, C_BG);
  line(0, KNOB_Y, SCREEN_W - 1, KNOB_Y, C_FAINT);
  for (int i = 0; i < 4; i++) {
    const knob_t *k = &KNOBS[kpage][i];
    int cx = i * 60 + 30;
    char b[12];
    center_text(cx, KNOB_Y + 3, k->label, kpage ? C_GOLD : C_DIM, FONT_SMALL);
    knob_text(b, kpage, i);
    center_text(cx, KNOB_Y + 19, b, C_TEXT, FONT_SMALL);
    /* a small bar, from the centre for the bipolar ones */
    int32_t v = knob_get(k), w = 44;
    rect(cx - w / 2, KNOB_Y + 35, w, 3, C_FAINT);
    if (k->lo < 0) {
      int32_t len = (v * (w / 2)) / k->hi;
      if (len > 0) rect(cx, KNOB_Y + 35, len, 3, C_GOLD);
      else if (len < 0) rect(cx + len, KNOB_Y + 35, -len, 3, C_GOLD);
    } else {
      rect(cx - w / 2, KNOB_Y + 35, ((v - k->lo) * w) / (k->hi - k->lo), 3, C_GOLD);
    }
  }
}

/* ---------------------------------------------------------------- setup and loop */

void setup(void) {
  if (!settings_load(&store, sizeof store) && store.magic == STORE_MAGIC) {
    S.bpm = (int16_t)clampi(store.bpm, 40, 240), S.filter = (int16_t)clampi(store.filter, -100, 100);
    S.kit = (int8_t)clampi(store.kit, 0, 3), S.bass = (int8_t)clampi(store.bass, 0, 3);
    S.transpose = (int8_t)clampi(store.transpose, -12, 12), S.quant = (int8_t)clampi(store.quant, 0, 2);
    S.glide = (int8_t)clampi(store.glide, 0, 100), S.drum_lvl = (int8_t)clampi(store.drum_lvl, 0, 100);
    S.bass_lvl = (int8_t)clampi(store.bass_lvl, 0, 100), S.lofi = (int8_t)clampi(store.lofi, 0, 100);
    S.click_lvl = (int8_t)clampi(store.click_lvl, 0, 100), S.click = store.click != 0;
    oct = (int8_t)clampi(store.oct, -2, 2);
    for (int p = 0; p < PATTERNS; p++) PAT[p] = store.pat[p];
    seq_cur = seq_next = (uint8_t)clampi(store.cur, 0, PATTERNS - 1);
  } else {
    demo_pattern();
    oct = -1;
  }
  snd_init();
  led_backlight(1);
  clear(C_BG);
}

static void release_bass_keys(void) {
  for (int k = 0; k < KEY_COUNT; k++)
    if (held_note[k]) seq_bass(held_note[k], 0), held_note[k] = 0;
}

void loop(void) {
  uint32_t now = millis();

  /* ---- buttons */
  static const uint8_t PAT_BTN[PATTERNS] = {BTN_FX, BTN_SEL, BTN_ENV, BTN_LFO};
  for (int i = 0; i < PATTERNS; i++)
    if (button_pressed(PAT_BTN[i])) seq_select(i);
  if (button_pressed(BTN_PLAY)) seq_play(!seq_playing);
  if (button_pressed(BTN_REC)) seq_rec(!seq_recording);
  if (button_pressed(BTN_EDIT)) release_bass_keys(), mode_bass ^= 1;
  if (button_pressed(BTN_ARP)) S.click = !S.click;
  if (button_pressed(BTN_GLO)) S.mute = (int8_t)((S.mute + 1) & 3);
  if (button_pressed(BTN_HOME)) kpage ^= 1;
  if (button_pressed(BTN_SAVE)) store_save(), saved_t = now ? now : 1;
  if (button_pressed(BTN_SEQ)) seq_down_t = now, seq_held_cleared = 0;
  if (button_held(BTN_SEQ) && !seq_held_cleared && now - seq_down_t > 800) seq_clear(), seq_held_cleared = 1;
  if (button_released(BTN_SEQ) && !seq_held_cleared) seq_undo();
  if (button_pressed(BTN_OCT_DOWN) || button_pressed(BTN_OCT_UP)) {
    if (button_held(BTN_OCT_DOWN) && button_held(BTN_OCT_UP)) oct = 0;
    else oct = (int8_t)clampi(oct + (button_pressed(BTN_OCT_UP) ? 1 : -1), -2, 2);
  }

  /* ---- encoders */
  int32_t e;
  if ((e = encoder(ENC_SELECT))) S.bpm = (int16_t)clampi(S.bpm + e * (e > 1 || e < -1 ? 4 : 1), 40, 240);
  if ((e = encoder(ENC_PRESETS))) S.kit = (int8_t)((S.kit + 4 + (e > 0 ? 1 : -1)) % 4);
  if ((e = encoder(ENC_ALGORITHM))) S.bass = (int8_t)((S.bass + 4 + (e > 0 ? 1 : -1)) % 4);
  for (int i = 0; i < 4; i++)
    if ((e = encoder(ENC_KNOB1 + i))) knob_set(&KNOBS[kpage][i], knob_get(&KNOBS[kpage][i]) + e * KNOBS[kpage][i].step);

  /* ---- keys: the pads, or the bass */
  for (int k = 0; k < KEY_COUNT; k++) {
    if (key_pressed(k)) {
      if (mode_bass) {
        int n = clampi(KEY_FIRST_NOTE - 12 + k + oct * 12, 12, 108); /* F2 at octave 0 */
        held_note[k] = (uint8_t)n;
        seq_bass(n, 1);
      } else {
        seq_pad(k % PADS);
      }
    }
    if (key_released(k) && held_note[k]) seq_bass(held_note[k], 0), held_note[k] = 0;
  }

  /* ---- MIDI: channel 10 plays the pads, the others follow the mode */
  uint8_t st, d1, d2;
  while (midi_read(&st, &d1, &d2)) {
    int on = (st & 0xF0) == 0x90 && d2, off = (st & 0xF0) == 0x80 || ((st & 0xF0) == 0x90 && !d2);
    int drums = (st & 0x0F) == 9 || !mode_bass;
    if (on && drums) seq_pad(((d1 + 120 - 36) % 12));
    else if (on) seq_bass(d1, 1);
    else if (off && !drums) seq_bass(d1, 0);
  }

  /* ---- the pads' light: a hit stays lit ~90 ms */
  uint16_t fl = seq_pad_flash;
  if (fl) {
    seq_pad_flash &= (uint16_t)~fl;
    for (int i = 0; i < PADS; i++)
      if (fl >> i & 1u) flash_seen[i] = now ? now : 1;
  }
  uint16_t lit = 0;
  for (int i = 0; i < PADS; i++)
    if (flash_seen[i] && now - flash_seen[i] < 90) lit |= (uint16_t)(1u << i);
  int bass_pad = seq_bass_note ? (seq_bass_note + 12 * 10 - KEY_FIRST_NOTE) % 12 : -1;

  /* ---- LEDs */
  uint32_t lk = 0, lb = 0;
  for (int k = 0; k < KEY_COUNT; k++) {
    if (!mode_bass && (lit >> (k % PADS) & 1u)) lk |= 1u << k;
    if (mode_bass && seq_bass_note && KEY_FIRST_NOTE - 12 + k + oct * 12 == seq_bass_note) lk |= 1u << k;
  }
  lb |= 1u << PAT_BTN[seq_cur];
  if (seq_next != seq_cur && ((now / 250) & 1)) lb |= 1u << PAT_BTN[seq_next];
  if (seq_playing && (seq_step % 4) < 2) lb |= 1u << BTN_PLAY;
  if (seq_recording) lb |= 1u << BTN_REC;
  if (S.click) lb |= 1u << BTN_ARP;
  if (mode_bass) lb |= 1u << BTN_EDIT;
  if (S.mute) lb |= 1u << BTN_GLO;
  if (kpage) lb |= 1u << BTN_HOME;
  if (lk != led_k_now) {
    for (int i = 0; i < KEY_COUNT; i++)
      if ((lk ^ led_k_now) >> i & 1u) led_key(i, lk >> i & 1u);
    led_k_now = lk;
  }
  if (lb != led_b_now) {
    for (int i = 0; i < BTN_COUNT; i++)
      if ((lb ^ led_b_now) >> i & 1u) led_button(i, lb >> i & 1u);
    led_b_now = lb;
  }

  /* ---- the screen, only what changed */
  if (!drawn) clear(C_BG), drawn = 1;
  uint32_t h = (uint32_t)S.bpm;
  if (h != sh_head) sh_head = h, draw_head();
  h = (uint32_t)S.kit | (uint32_t)S.bass << 4 | (uint32_t)mode_bass << 8 | (uint32_t)(oct + 4) << 9;
  if (h != sh_info) sh_info = h, draw_info(), sh_pads = ~0u;
  h = (uint32_t)lit | (uint32_t)(bass_pad + 1) << 12 | (uint32_t)S.kit << 17 | (uint32_t)mode_bass << 20;
  if (h != sh_pads) sh_pads = h, draw_pads(lit, bass_pad);
  h = (uint32_t)seq_dirty | (uint32_t)seq_step << 8 | (uint32_t)seq_cur << 14 | (uint32_t)seq_playing << 16 |
      (uint32_t)seq_recording << 17;
  if (h != sh_grid) sh_grid = h, draw_grid();
  h = (uint32_t)seq_cur | (uint32_t)seq_next << 2 | (uint32_t)seq_playing << 4 | (uint32_t)seq_recording << 5 |
      (uint32_t)S.click << 6 | (uint32_t)S.mute << 7 | (uint32_t)((now / 250) & 1) << 9 | (uint32_t)(now - saved_t < 1500) << 10;
  if (h != sh_stat) sh_stat = h, draw_stat(now);
  h = (uint32_t)kpage;
  for (int i = 0; i < 4; i++) h = h * 211u + (uint32_t)(knob_get(&KNOBS[kpage][i]) + 128);
  if (h != sh_knobs) sh_knobs = h, draw_knobs();
}

/* ---------------------------------------------------------------- the sound */

void audio(int32_t *out, uint32_t frames) { seq_render(out, frames); }
