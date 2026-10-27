/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for BEATPEN's sounds and looper:
 *   - each kit: its 12 pads one after the other, to build/render-out/kit-NAME.wav
 *   - each bass: a short line with a slide, to build/render-out/bass-NAME.wav
 *   - the looper: a beat recorded live (pads hit off the grid, quantised) then played back, to loop.wav
 *   - a fuzz: random settings, pads and notes, checking the output stays in range
 *   tests/run.sh builds and runs it */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "beat.h"

int32_t sine(uint32_t phase) {
  int32_t x = (int32_t)phase >> 16, ax = x < 0 ? -x : x, y, ay;
  y = (x * (32768 - ax)) >> 13;
  ay = y < 0 ? -y : y;
  y += ((((y * ay) >> 15) - y) * 7373) >> 15;
  return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}
static const uint32_t INC_C9[12] = {815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
                                    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};
uint32_t note_inc(int note) {
  if (note < 0) note = 0;
  if (note > 127) note = 127;
  return INC_C9[note % 12] >> (10 - note / 12);
}

#define MAXF (44100 * 20)
static int16_t buf[MAXF * 2];
static uint32_t pos;
static int fail;

static void render(uint32_t frames, int seq) {
  static int32_t blk[512];
  while (frames) {
    uint32_t n = frames > 256 ? 256 : frames;
    memset(blk, 0, sizeof blk);
    if (seq) seq_render(blk, n);
    else snd_render(blk, n);
    for (uint32_t i = 0; i < 2 * n; i++) {
      if (blk[i] > 32767 || blk[i] < -32767) fail = 1;
      if (pos * 2 + i < MAXF * 2) buf[pos * 2 + i] = (int16_t)blk[i];
    }
    pos += n, frames -= n;
  }
}

static void save(const char *dir, const char *name) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s.wav", dir, name);
  FILE *f = fopen(path, "wb");
  uint32_t frames = pos < MAXF ? pos : MAXF, data = frames * 4, riff = 36 + data, rate = 44100, br = rate * 4, fmt = 16;
  uint16_t pcm = 1, ch = 2, al = 4, bits = 16;
  fwrite("RIFF", 1, 4, f), fwrite(&riff, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f), fwrite(&fmt, 4, 1, f);
  fwrite(&pcm, 2, 1, f), fwrite(&ch, 2, 1, f), fwrite(&rate, 4, 1, f), fwrite(&br, 4, 1, f);
  fwrite(&al, 2, 1, f), fwrite(&bits, 2, 1, f), fwrite("data", 1, 4, f), fwrite(&data, 4, 1, f);
  fwrite(buf, 2, frames * 2, f);
  fclose(f);
  double sq = 0;
  int peak = 0;
  for (uint32_t i = 0; i < frames * 2; i++) sq += (double)buf[i] * buf[i], peak = abs(buf[i]) > peak ? abs(buf[i]) : peak;
  printf("%-22s peak %6d  rms %6.0f\n", name, peak, sqrt(sq / (frames * 2)));
  pos = 0;
}

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : ".";
  snd_init();
  S.lofi = 0, S.filter = 0;
  for (int k = 0; k < PACKS; k++) {
    S.kit = (int8_t)k;
    for (int p = 0; p < PADS; p++) snd_pad(p), render(22050, 0);
    render(22050, 0);
    char n[32];
    snprintf(n, sizeof n, "kit-%s", PACK_NAMES[k]);
    save(dir, n);
  }
  static const int LINE[8] = {36, 36, 43, 41, 36, 48, 46, 39};
  for (int b = 0; b < PACKS; b++) {
    S.bass = (int8_t)b, S.glide = 30;
    for (int i = 0; i < 8; i++) {
      snd_bass_on(LINE[i], i == 5);
      render(i == 4 ? 5000 : 9000, 0);
      if (i != 4) snd_bass_off();
      render(2000, 0);
    }
    render(30000, 0);
    char n[32];
    snprintf(n, sizeof n, "bass-%s", BASS_NAMES[b]);
    save(dir, n);
  }

  /* the looper: record a beat live, a little off the grid, then hear it back with a bass line */
  S.kit = PACK_HIPHOP, S.bass = PACK_HIPHOP, S.bpm = 92, S.quant = 2, S.lofi = 22, S.click = 1;
  uint32_t step = 44100u * 60u / 92u / 4u; /* a sixteenth, in frames */
  seq_rec(1);
  render(64, 1);
  for (int s = 0; s < STEPS; s++) {
    if (s % 8 == 0 || s == 11) seq_pad(0);
    if (s % 8 == 4) seq_pad(1);
    if (s % 2 == 0) seq_pad(2);
    if (s == 30) seq_pad(3);
    if (s == 0 || s == 16) seq_bass(36, 1);
    if (s == 6 || s == 22) seq_bass(36, 0);
    if (s == 8) seq_bass(39, 1);
    if (s == 12) seq_bass(39, 0);
    if (s == 24) seq_bass(43, 1);
    if (s == 28) seq_bass(43, 0);
    render(step + (s % 3 == 0 ? 300 : -200) * (s > 0), 1); /* humanised: early and late hits */
  }
  seq_rec(0);
  S.click = 0;
  render(step * STEPS * 2, 1); /* two passes of playback */
  save(dir, "loop");
  int hits = 0, notes = 0;
  for (int s = 0; s < STEPS; s++) {
    for (int p = 0; p < PADS; p++) hits += PAT[0].drum[s] >> p & 1;
    notes += PAT[0].bass[s] && PAT[0].bass[s] != TIE;
  }
  int placed = 1; /* every hit on the step it was aimed at */
  for (int s = 0; s < STEPS; s++) {
    uint16_t want = (uint16_t)(((s % 8 == 0 || s == 11) ? 1 : 0) | (s % 8 == 4 ? 2 : 0) | (s % 2 == 0 ? 4 : 0) | (s == 30 ? 8 : 0));
    if (PAT[0].drum[s] != want) placed = 0, printf("  step %d: %03x, wanted %03x\n", s, PAT[0].drum[s], want);
  }
  printf("looper: %d drum hits, %d bass notes recorded (expected 26 and 4), quantised %s\n", hits, notes, placed ? "ok" : "WRONG");
  if (hits != 26 || notes != 4 || !placed) fail = 1;
  seq_undo();
  render(512, 1);
  int after = 0;
  for (int s = 0; s < STEPS; s++) after += PAT[0].drum[s] != 0;
  printf("undo: %s\n", after == 0 ? "ok" : "FAILED");
  if (after) fail = 1;
  seq_play(0);

  /* fuzz */
  srand(3);
  for (int b = 0; b < 30000; b++) {
    switch (rand() % 12) {
      case 0: S.kit = (int8_t)(rand() % PACKS); break;
      case 1: S.bass = (int8_t)(rand() % PACKS); break;
      case 2: S.filter = (int16_t)(rand() % 201 - 100); break;
      case 3: S.lofi = (int8_t)(rand() % 101); break;
      case 4: S.glide = (int8_t)(rand() % 101); break;
      case 5: S.bpm = (int16_t)(40 + rand() % 200); break;
      case 6: seq_bass(12 + rand() % 90, rand() & 1); break;
      case 7: seq_rec(rand() & 1); break;
      case 8: seq_play(rand() & 1); break;
      case 9: seq_select(rand() % PATTERNS); break;
      case 10: S.transpose = (int8_t)(rand() % 25 - 12), S.quant = (int8_t)(rand() % 3); break;
      default: seq_pad(rand() % PADS); break;
    }
    if (b % 997 == 0) seq_undo();
    if (b % 4999 == 0) seq_clear();
    render(256, 1);
    pos = 0;
  }
  printf("fuzz: 30000 blocks, %s\n", fail ? "FAILED" : "ok");
  return fail;
}
