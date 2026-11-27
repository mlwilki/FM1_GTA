/* SPDX-License-Identifier: GPL-3.0-only */
/* audio.c: 44.1 kHz pure integer synthesizer — engines, sirens, SFX, and 4 GTA-style FM radio stations */
#include "oop.h"

/* ---- shared audio control state (written from loop, read from audio ISR) */
static volatile uint8_t a_eng_on = 0;
static volatile uint8_t a_eng_cyls = 6;
static volatile uint8_t a_eng_rpm = 0;
static volatile uint8_t a_skid = 0;
static volatile uint8_t a_horn = 0;
static volatile uint8_t a_siren = 0;
static volatile uint8_t a_station = RAD_VICE;
static volatile uint8_t a_muted = 0;

/* ---- SFX ring queue (lock-free single producer / single consumer) */
static volatile uint8_t sfx_q[16];
static volatile uint8_t sfx_w = 0, sfx_r = 0;

void audio_sfx(uint8_t id) {
  uint8_t nw = (uint8_t)((sfx_w + 1u) & 15u);
  if (nw != sfx_r) {
    sfx_q[sfx_w] = id;
    sfx_w = nw;
  }
}

void audio_update_state(int engine_on, uint8_t cyls, uint8_t rpm, uint8_t skid, int horn, int siren, uint8_t station, int muted) {
  a_eng_on = (uint8_t)(engine_on ? 1 : 0);
  a_eng_cyls = cyls;
  a_eng_rpm = rpm;
  a_skid = skid;
  a_horn = (uint8_t)(horn ? 1 : 0);
  a_siren = (uint8_t)(siren ? 1 : 0);
  a_station = station;
  a_muted = (uint8_t)(muted ? 1 : 0);
}

const char *radio_name(uint8_t station) {
  switch (station) {
  case RAD_VICE: return "VICE WAVE FM";
  case RAD_WEST: return "WEST COAST FM";
  case RAD_LIBERTY: return "LIBERTY NOIR";
  case RAD_ELECTRO: return "PAROLE ELECTRO";
  case RAD_SCANNER: return "PD SCANNER";
  default: return "RADIO OFF";
  }
}

/* ---- one-shot SFX voices */
#define MAX_SFX_V 6
typedef struct {
  uint8_t active;
  uint8_t id;
  uint32_t phase;
  uint32_t inc;
  int32_t env;
  int32_t decay;
  int32_t lp;
} SfxVoice;

static SfxVoice sv[MAX_SFX_V];
static uint32_t noise_s = 0x13579BDFu;

static inline int32_t next_noise(void) {
  noise_s ^= noise_s << 13;
  noise_s ^= noise_s >> 17;
  noise_s ^= noise_s << 5;
  return (int32_t)(noise_s >> 16) - 32768;
}

static void trigger_sfx(uint8_t id) {
  int i, slot = 0;
  int32_t min_env = 0x7FFFFFFF;
  for (i = 0; i < MAX_SFX_V; i++) {
    if (!sv[i].active) {
      slot = i;
      break;
    }
    if (sv[i].env < min_env) {
      min_env = sv[i].env;
      slot = i;
    }
  }
  SfxVoice *v = &sv[slot];
  v->active = 1;
  v->id = id;
  v->phase = 0;
  v->lp = 0;
  v->env = 32767;
  switch (id) {
  case SFX_PISTOL: v->inc = 48000000u; v->decay = 7; break;
  case SFX_SMG: v->inc = 64000000u; v->decay = 11; break;
  case SFX_SHOTGUN: v->inc = 26000000u; v->decay = 4; break;
  case SFX_EXPLOSION: v->inc = 9000000u; v->decay = 1; break;
  case SFX_CRASH: v->inc = 16000000u; v->decay = 3; break;
  case SFX_PUNCH: v->inc = 14000000u; v->decay = 8; break;
  case SFX_HIT: v->inc = 22000000u; v->decay = 10; break;
  case SFX_DOOR: v->inc = 18000000u; v->decay = 12; break;
  case SFX_RELOAD: v->inc = 95000000u; v->decay = 9; break;
  case SFX_EMPTY: v->inc = 140000000u; v->decay = 16; break;
  case SFX_CASH: v->inc = note_inc(84); v->decay = 3; break;
  case SFX_PICKUP: v->inc = note_inc(76); v->decay = 4; break;
  case SFX_UI: v->inc = note_inc(79); v->decay = 14; break;
  case SFX_SIREN_BLIP: v->inc = note_inc(81); v->decay = 3; break;
  default: v->inc = note_inc(69); v->decay = 10; break;
  }
}

/* ---- GTA-Style Multi-Track FM Radio Sequencer (32-step patterns, MIDI notes, 0 = rest) */
typedef struct {
  uint16_t step_samples; /* samples per 16th-note step */
  uint8_t bass[32];
  uint8_t lead[32];
  uint8_t drum[32]; /* bit0: kick, bit1: snare, bit2: hat */
} RadioTrack;

static const RadioTrack RADIO_TRACKS[4] = {
    /* RAD_VICE: 118 BPM 80s Synthwave (A minor: A-F-C-G driving octaves + neon lead) */
    {
        5605,
        {45, 57, 45, 57, 45, 57, 45, 57, 41, 53, 41, 53, 41, 53, 41, 53,
         48, 60, 48, 60, 48, 60, 48, 60, 43, 55, 43, 55, 43, 55, 45, 55},
        {69,  0, 72,  0, 76,  0, 72, 76, 77,  0, 76,  0, 72,  0, 69,  0,
         72,  0, 76,  0, 79,  0, 76, 72, 74,  0, 71,  0, 67,  0, 71, 74},
        {5, 4, 4, 4, 6, 4, 4, 4, 5, 4, 5, 4, 6, 4, 4, 4,
         5, 4, 4, 4, 6, 4, 4, 4, 5, 4, 5, 4, 6, 4, 6, 4},
    },
    /* RAD_WEST: 94 BPM West Coast G-Funk (San Andreas-inspired bounce + high whine lead) */
    {
        7037,
        {40,  0,  0, 40,  0,  0, 43,  0, 45,  0, 43,  0, 40,  0, 38,  0,
         40,  0,  0, 40,  0,  0, 43, 45, 47,  0, 45,  0, 43,  0, 38,  0},
        {88,  0, 86,  0, 83,  0, 86, 88,  0,  0, 91,  0, 88,  0, 86,  0,
         83,  0,  0, 86, 88,  0,  0,  0, 91,  0, 88, 86, 83,  0, 81,  0},
        {5, 0, 4, 1, 6, 0, 4, 0, 5, 0, 5, 0, 6, 0, 4, 4,
         5, 0, 4, 1, 6, 0, 4, 0, 5, 1, 4, 0, 6, 0, 4, 2},
    },
    /* RAD_LIBERTY: 88 BPM Gritty East Coast Noir (minor piano/bell + deep boom-bap) */
    {
        7517,
        {36,  0,  0,  0,  0,  0, 36,  0, 39,  0,  0,  0, 38,  0, 34,  0,
         36,  0,  0,  0,  0,  0, 36,  0, 43,  0,  0,  0, 41,  0, 39,  0},
        {72,  0,  0, 75,  0,  0, 79,  0, 75,  0, 74,  0, 72,  0,  0,  0,
         72,  0,  0, 75,  0,  0, 80,  0, 79,  0, 77,  0, 74,  0,  0,  0},
        {1, 0, 4, 0, 2, 0, 4, 1, 1, 0, 4, 0, 2, 0, 4, 0,
         1, 0, 4, 0, 2, 0, 4, 1, 1, 0, 5, 0, 2, 0, 4, 2},
    },
    /* RAD_ELECTRO: 128 BPM Parole Arcade Electro-Funk */
    {
        5168,
        {38,  0, 50, 38,  0, 50, 38,  0, 41,  0, 53, 41, 43,  0, 55, 43,
         38,  0, 50, 38,  0, 50, 45,  0, 43,  0, 41,  0, 40,  0, 38,  0},
        {74, 77,  0, 81,  0, 77, 74,  0, 77, 81,  0, 84,  0, 81, 79,  0,
         74,  0, 77, 81, 86,  0, 81, 77, 79,  0, 76,  0, 74,  0,  0,  0},
        {5, 4, 1, 4, 6, 4, 5, 4, 5, 4, 1, 4, 6, 4, 4, 6,
         5, 4, 1, 4, 6, 4, 5, 4, 5, 4, 5, 4, 6, 4, 6, 4},
    },
};

/* Synth state */
static uint32_t eng_ph = 0, eng_ph2 = 0;
static int32_t eng_lp = 0;
static uint32_t sir_ph = 0, sir_lfo = 0;
static uint32_t horn_ph1 = 0, horn_ph2 = 0;
static int32_t skid_lp = 0;

static uint32_t rad_step_cnt = 0;
static uint8_t rad_step = 0;
static uint32_t rad_bass_ph = 0, rad_bass_inc = 0;
static uint32_t rad_lead_ph = 0, rad_lead_inc = 0, rad_mod_ph = 0;
static int32_t rad_bass_env = 0, rad_lead_env = 0;
static int32_t rad_kick_env = 0, rad_snare_env = 0, rad_hat_env = 0;
static uint32_t rad_kick_ph = 0;

void audio_init(void) {
  int i;
  for (i = 0; i < MAX_SFX_V; i++) sv[i].active = 0;
  sfx_w = sfx_r = 0;
}

void audio(int32_t *out, uint32_t frames) {
  uint32_t f;
  int i;

  while (sfx_r != sfx_w) {
    trigger_sfx(sfx_q[sfx_r]);
    sfx_r = (uint8_t)((sfx_r + 1u) & 15u);
  }

  if (a_muted) return;

  uint8_t eng_on = a_eng_on;
  uint8_t cyls = a_eng_cyls;
  uint8_t rpm = a_eng_rpm;
  uint8_t skid = a_skid;
  uint8_t horn = a_horn;
  uint8_t siren = a_siren;
  uint8_t st = a_station;

  /* Engine pitch from RPM (4/6/8 cylinders) */
  uint32_t eng_inc = (uint32_t)(3800000u + (uint32_t)rpm * (cyls == 8 ? 52000u : (cyls == 6 ? 60000u : 68000u)));
  uint32_t horn_inc1 = note_inc(61); /* C#4 */
  uint32_t horn_inc2 = note_inc(65); /* F4 */

  for (f = 0; f < frames; f++) {
    int32_t mix = 0;

    /* 1. Car Engine */
    if (eng_on) {
      eng_ph += eng_inc;
      eng_ph2 += eng_inc + (eng_inc >> 1);
      int32_t saw = ((int32_t)(eng_ph >> 16) - 32768) >> 3;
      int32_t sub = ((eng_ph2 & 0x80000000u) ? 2400 : -2400);
      int32_t raw = saw + sub;
      eng_lp += (raw - eng_lp) >> 4;
      mix += eng_lp;
    }

    /* 2. Horn */
    if (horn) {
      horn_ph1 += horn_inc1;
      horn_ph2 += horn_inc2;
      int32_t h1 = (horn_ph1 & 0x80000000u) ? 2600 : -2600;
      int32_t h2 = (horn_ph2 & 0x80000000u) ? 2200 : -2200;
      mix += (h1 + h2) >> 1;
    }

    /* 3. Tire Screech */
    if (skid > 20) {
      int32_t n = next_noise() >> 3;
      skid_lp += (n - skid_lp) >> 2;
      mix += (n - skid_lp) >> 1;
    }

    /* 4. Police Siren (wail sweep) */
    if (siren) {
      sir_lfo += 42000u;
      int32_t lfo = sine(sir_lfo); /* -32767..32767 */
      uint32_t s_inc = (uint32_t)(72000000u + (lfo * 900));
      sir_ph += s_inc;
      mix += sine(sir_ph) >> 4;
    }

    /* 5. GTA-Style FM Synth Radio */
    if (st >= RAD_VICE && st <= RAD_ELECTRO) {
      const RadioTrack *tr = &RADIO_TRACKS[st - RAD_VICE];
      if (rad_step_cnt == 0) {
        uint8_t bn = tr->bass[rad_step];
        uint8_t ln = tr->lead[rad_step];
        uint8_t dr = tr->drum[rad_step];
        if (bn) {
          rad_bass_inc = note_inc(bn);
          rad_bass_env = 16000;
        }
        if (ln) {
          rad_lead_inc = note_inc(ln);
          rad_lead_env = 14000;
        }
        if (dr & 1u) {
          rad_kick_env = 22000;
          rad_kick_ph = 0;
        }
        if (dr & 2u) rad_snare_env = 16000;
        if (dr & 4u) rad_hat_env = 7000;
        rad_step = (uint8_t)((rad_step + 1u) & 31u);
        rad_step_cnt = tr->step_samples;
      } else {
        rad_step_cnt--;
      }

      int32_t rmix = 0;
      if (rad_bass_env > 0) {
        rad_bass_ph += rad_bass_inc;
        int32_t b = (sine(rad_bass_ph) >> 1) + (((int32_t)(rad_bass_ph >> 16) - 32768) >> 2);
        rmix += (b * rad_bass_env) >> 16;
        if ((f & 3u) == 0) rad_bass_env -= 10;
        if (rad_bass_env < 0) rad_bass_env = 0;
      }
      if (rad_lead_env > 0) {
        rad_lead_ph += rad_lead_inc;
        rad_mod_ph += rad_lead_inc << 1;
        /* Simple 2-op FM lead */
        int32_t mod = (sine(rad_mod_ph) * (st == RAD_WEST ? 3 : 10)) << 10;
        int32_t ld = sine(rad_lead_ph + (uint32_t)mod);
        rmix += (ld * rad_lead_env) >> 16;
        if ((f & 3u) == 0) rad_lead_env -= 7;
        if (rad_lead_env < 0) rad_lead_env = 0;
      }
      if (rad_kick_env > 0) {
        rad_kick_ph += (uint32_t)(5500000u + (uint32_t)rad_kick_env * 700u);
        rmix += (sine(rad_kick_ph) * rad_kick_env) >> 16;
        rad_kick_env -= 12;
        if (rad_kick_env < 0) rad_kick_env = 0;
      }
      if (rad_snare_env > 0) {
        rmix += (next_noise() * rad_snare_env) >> 17;
        rad_snare_env -= 9;
        if (rad_snare_env < 0) rad_snare_env = 0;
      }
      if (rad_hat_env > 0) {
        rmix += (next_noise() * rad_hat_env) >> 18;
        rad_hat_env -= 14;
        if (rad_hat_env < 0) rad_hat_env = 0;
      }
      mix += (rmix * 3) >> 2;
    } else if (st == RAD_SCANNER) {
      /* Police scanner static bursts */
      if ((rad_step_cnt++ & 32767u) < 4200u) {
        mix += next_noise() >> 5;
      }
    }

    /* 6. One-shot SFX voices */
    for (i = 0; i < MAX_SFX_V; i++) {
      SfxVoice *v = &sv[i];
      if (!v->active) continue;
      int32_t s = 0;
      if (v->id == SFX_PISTOL || v->id == SFX_SMG || v->id == SFX_SHOTGUN || v->id == SFX_EXPLOSION || v->id == SFX_CRASH) {
        int32_t n = next_noise();
        v->lp += (n - v->lp) >> (v->id == SFX_EXPLOSION ? 4 : (v->id == SFX_SHOTGUN ? 2 : 1));
        s = (v->lp * v->env) >> 16;
      } else if (v->id == SFX_CASH || v->id == SFX_PICKUP || v->id == SFX_UI || v->id == SFX_SIREN_BLIP) {
        v->phase += v->inc;
        if (v->id == SFX_CASH && v->env < 18000) v->phase += v->inc >> 2;
        if (v->id == SFX_PICKUP) v->inc += 1800u;
        s = (sine(v->phase) * v->env) >> 16;
      } else {
        v->phase += v->inc;
        if (v->inc > 2000000u) v->inc -= 2500u;
        s = (sine(v->phase) * v->env) >> 16;
      }
      mix += s >> 1;
      v->env -= v->decay;
      if (v->env <= 0) v->active = 0;
    }

    /* Soft clip to +-32767 */
    if (mix > 30000) mix = 30000;
    else if (mix < -30000) mix = -30000;
    out[f * 2] += mix;
    out[f * 2 + 1] += mix;
  }
}
