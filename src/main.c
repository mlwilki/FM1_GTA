/* SPDX-License-Identifier: GPL-3.0-only */
/* main.c: Out on Parole — M-VAVE FM-1 entry point, hardware controls, LEDs, flash save */
#include "oop.h"

uint8_t game_mode = MODE_TITLE;
uint32_t game_ticks = 0;
int32_t cash = 200;
uint8_t radio_station = RAD_VICE;
uint8_t zoom_level = 1; /* 0=CLOSE, 1=NORMAL, 2=WIDE, 3=SATELLITE */
uint8_t cut_kind = CUT_NONE;
int16_t cut_t = 0;
uint16_t cut_car_paint = RGB(200, 48, 48);
uint16_t cut_ped_shirt = RGB(64, 130, 210);
uint8_t cut_car_model = CAR_SEDAN;

static const char *const ZOOM_NAMES[4] = {"ZOOM: CLOSE", "ZOOM: NORMAL", "ZOOM: WIDE", "ZOOM: SATELLITE"};

static uint32_t last_tick_ms = 0;
static uint32_t tick_accum = 0;
static int16_t save_flash_t = 0;
static uint16_t pending_hit = 0;
static int32_t pending_enc_sel = 0;

static uint16_t calc_checksum(const SaveData *s) {
  const uint8_t *p = (const uint8_t *)s;
  uint16_t sum = 0x5A3Cu;
  uint32_t i;
  for (i = 0; i < (uint32_t)(sizeof(SaveData) - sizeof(uint16_t)); i++) {
    sum = (uint16_t)((sum * 33u) ^ p[i]);
  }
  return sum;
}

void save_game(void) {
  SaveData s;
  Ped *pl = &peds[PLAYER];
  int i;
  s.magic = SAVE_MAGIC;
  s.cash = cash;
  s.mission_cur = mission_cur;
  s.stashes_mask = stashes_mask;
  s.owned_w = pl->owned_w;
  s.radio_station = radio_station;
  s.zoom_level = zoom_level;
  s.pad0 = 0;
  for (i = 0; i < W_COUNT; i++) s.ammo[i] = pl->ammo[i];
  s.armor = pl->armor;
  s.checksum = calc_checksum(&s);
  settings_save(&s, (uint32_t)sizeof(s));
  save_flash_t = 45;
  hud_msg("GAME SAVED TO FLASH", 55);
  audio_sfx(SFX_UI);
}

void load_game(void) {
  SaveData s;
  int i;
  if (settings_load(&s, (uint32_t)sizeof(s)) != 0) return;
  if (s.magic != SAVE_MAGIC || s.checksum != calc_checksum(&s)) return;
  cash = s.cash;
  if (s.mission_cur >= 0 && s.mission_cur <= NMISSION) mission_cur = s.mission_cur;
  stashes_mask = s.stashes_mask;
  peds[PLAYER].owned_w = s.owned_w | (1u << W_FIST);
  if (s.radio_station < RAD_COUNT) radio_station = s.radio_station;
  if (s.zoom_level < 4u) zoom_level = s.zoom_level;
  for (i = 0; i < W_COUNT; i++) peds[PLAYER].ammo[i] = s.ammo[i];
  peds[PLAYER].armor = s.armor;
}

static void poll_input(GameInput *in) {
  in->mx = 0;
  in->my = 0;
  in->held = 0;
  in->hit = 0;

  /* Left-hand inverted-T piano keys:
   * Key 0 (F3) = Left, Key 3 (G#3) = Up/Gas, Key 2 (G3) = Down/Brake, Key 4 (A3) = Right
   * Also support Key 1 (F#3) as Up alt, and OCT-/OCT+ as Left/Right alt */
  if (key_held(0) || button_held(BTN_OCT_DOWN)) in->mx -= 256;
  if (key_held(4) || button_held(BTN_OCT_UP)) in->mx += 256;
  if (key_held(3) || key_held(1)) in->my += 256;
  if (key_held(2)) in->my -= 256;

  /* Encoder steering/aim assist via ENC_SELECT */
  pending_enc_sel += encoder(ENC_SELECT);
  if (pending_enc_sel > 0) in->mx = 256;
  else if (pending_enc_sel < 0) in->mx = -256;

  /* Buttons & right-hand keys */
  if (button_held(BTN_PLAY) || key_held(26)) in->held |= IN_FIRE;
  if (button_pressed(BTN_PLAY) || key_pressed(26)) in->hit |= IN_FIRE;

  if (button_held(BTN_REC) || key_held(24)) in->held |= IN_ENTER;
  if (button_pressed(BTN_REC) || key_pressed(24)) in->hit |= IN_ENTER;

  if (button_held(BTN_ARP) || key_held(22)) in->held |= IN_SPRINT;
  if (button_pressed(BTN_ARP) || key_pressed(22)) in->hit |= IN_SPRINT;

  if (button_held(BTN_SEQ) || key_held(20)) in->held |= IN_RELOAD;
  if (button_pressed(BTN_SEQ) || key_pressed(20)) in->hit |= IN_RELOAD;

  if (button_pressed(BTN_FX)) in->hit |= IN_PREV;
  if (button_pressed(BTN_SEL)) in->hit |= IN_NEXT;
  {
    int32_t k1 = encoder(ENC_KNOB1);
    if (k1 > 0) in->hit |= IN_NEXT;
    else if (k1 < 0) in->hit |= IN_PREV;
  }

  if (button_pressed(BTN_HOME)) in->hit |= IN_MAP;
  if (button_pressed(BTN_EDIT)) in->hit |= IN_PAUSE;
  if (button_pressed(BTN_GLO)) in->hit |= IN_RADIO;
  {
    int32_t ep = encoder(ENC_PRESETS);
    if (ep != 0) in->hit |= IN_RADIO;
  }
  if (button_pressed(BTN_SAVE)) in->hit |= IN_SAVE;
  if (button_pressed(BTN_LFO)) in->hit |= IN_ZOOM_IN;
  if (button_pressed(BTN_ENV)) in->hit |= IN_ZOOM_OUT;
  {
    int32_t k2 = encoder(ENC_KNOB2);
    if (k2 > 0) in->hit |= IN_ZOOM_IN;
    else if (k2 < 0) in->hit |= IN_ZOOM_OUT;
  }
}

static void update_leds(void) {
  int k;
  Ped *pl = &peds[PLAYER];

  if (game_mode == MODE_TITLE) {
    int scan = (int)((game_ticks >> 1) % 27u);
    for (k = 0; k < KEY_COUNT; k++) led_key(k, (k == scan || k == (26 - scan)) ? 1 : 0);
    led_button(BTN_PLAY, (game_ticks >> 4) & 1u);
    return;
  }

  if (heat > 0) {
    /* Police lightbar strobe across the 27 keys! Faster at higher wanted levels */
    int shift = heat >= 4 ? 1 : (heat >= 2 ? 2 : 3);
    int phase = (int)((game_ticks >> shift) & 1u);
    for (k = 0; k < KEY_COUNT; k++) {
      int left_half = k < 13;
      led_key(k, phase ? left_half : !left_half);
    }
  } else if (pl->car >= 0) {
    /* Speedometer bar on keys */
    int spd = cars[pl->car].speed < 0 ? -cars[pl->car].speed : cars[pl->car].speed;
    int lit = (spd * KEY_COUNT) / 1600;
    if (lit > KEY_COUNT) lit = KEY_COUNT;
    for (k = 0; k < KEY_COUNT; k++) led_key(k, k < lit ? 1 : 0);
  } else {
    /* Health bar on keys */
    int lit = (pl->health * KEY_COUNT) / 100;
    for (k = 0; k < KEY_COUNT; k++) led_key(k, k < lit ? 1 : 0);
  }

  led_button(BTN_PLAY, pl->weapon != W_FIST ? 1 : 0);
  led_button(BTN_REC, (pl->car >= 0 || shop_open || car_near(pl->x, pl->y, 1400) >= 0) ? 1 : 0);
  led_button(BTN_HOME, game_mode == MODE_MAP ? 1 : 0);
  led_button(BTN_EDIT, game_mode == MODE_PAUSE ? 1 : 0);
  led_button(BTN_GLO, radio_station != RAD_OFF ? 1 : 0);
  led_button(BTN_SAVE, save_flash_t > 0 ? ((game_ticks >> 2) & 1u) : 0);
}

void setup(void) {
  led_backlight(1);
  world_init();
  peds_init();
  cars_init();
  combat_init();
  cops_init();
  missions_init();
  jobs_init();
  audio_init();
  load_game();

  cam_x = peds[PLAYER].x;
  cam_y = peds[PLAYER].y;
  game_mode = MODE_TITLE;
  last_tick_ms = millis();
  tick_accum = 0;
}

static void step_simulation(const GameInput *in) {
  game_ticks++;
  if (save_flash_t > 0) save_flash_t--;

  if (game_mode == MODE_TITLE) {
    if (game_ticks > 6u && (in->hit || in->held || keys() || buttons())) {
      game_mode = MODE_PLAY;
      audio_sfx(SFX_UI);
      hud_msg("WELCOME BACK. WALT WANTS TO SEE YOU.", 90);
    }
    audio_update_state(0, 6, 0, 0, 0, 0, RAD_VICE, 0);
    return;
  }

  if (in->hit & IN_RADIO) {
    radio_station = (uint8_t)((radio_station + 1u) % RAD_COUNT);
    hud_msg(radio_name(radio_station), 45);
    audio_sfx(SFX_UI);
  }

  if (in->hit & IN_ZOOM_IN) {
    if (zoom_level > 0) zoom_level--;
    hud_msg(ZOOM_NAMES[zoom_level], 40);
    audio_sfx(SFX_UI);
  }
  if (in->hit & IN_ZOOM_OUT) {
    if (zoom_level < 3) zoom_level++;
    hud_msg(ZOOM_NAMES[zoom_level], 40);
    audio_sfx(SFX_UI);
  }

  /* 3D-style cutscenes: carjacking (36 ticks, skippable) & busted (90 ticks) */
  if (cut_kind == CUT_CARJACK) {
    cut_t++;
    if (cut_t == 8) audio_sfx(SFX_DOOR);
    if (cut_t == 16) audio_sfx(SFX_PUNCH);
    if (cut_t == 30) audio_sfx(SFX_DOOR);
    if (cut_t >= 38 || (cut_t > 8 && (in->hit & (IN_FIRE | IN_ENTER)))) {
      cut_kind = CUT_NONE;
      cut_t = 0;
    }
    return;
  }
  if (cut_kind == CUT_BUSTED) {
    cut_t++;
    if (cut_t == 42) audio_sfx(SFX_RELOAD); /* handcuffs click! */
  }

  if (in->hit & IN_SAVE) {
    save_game();
  }

  if (in->hit & IN_MAP) {
    if (game_mode == MODE_PLAY) game_mode = MODE_MAP;
    else if (game_mode == MODE_MAP) game_mode = MODE_PLAY;
    audio_sfx(SFX_UI);
  }

  if (in->hit & IN_PAUSE) {
    if (game_mode == MODE_PLAY) game_mode = MODE_PAUSE;
    else if (game_mode == MODE_PAUSE || game_mode == MODE_MAP) game_mode = MODE_PLAY;
    audio_sfx(SFX_UI);
  }

  if (game_mode == MODE_PLAY || game_mode == MODE_DEAD || game_mode == MODE_BUSTED) {
    world_tick();
    cars_tick(in);
    peds_tick(in);
    combat_tick(in);
    cops_tick();
    jobs_tick(in);
    missions_tick();
  }

  /* Update synthesizer state */
  {
    Ped *pl = &peds[PLAYER];
    int eng_on = (pl->car >= 0 && game_mode == MODE_PLAY);
    uint8_t cyls = eng_on ? car_specs[cars[pl->car].model].cyls : 6;
    uint8_t rpm = eng_on ? cars[pl->car].rpm : 0;
    uint8_t skid = eng_on ? cars[pl->car].skid : 0;
    int horn = eng_on && (cars[pl->car].flags & CF_HORN);
    int siren = (heat > 0) || (eng_on && (cars[pl->car].flags & CF_SIREN));
    /* Radio plays in cars, or softly on foot if tuned */
    uint8_t st = (game_mode == MODE_PAUSE) ? RAD_OFF : radio_station;
    audio_update_state(eng_on, cyls, rpm, skid, horn, siren, st, 0);
  }
}

void loop(void) {
  uint32_t now = millis();
  uint32_t dt = now - last_tick_ms;
  last_tick_ms = now;
  if (dt > 100u) dt = 100u; /* clamp after flash save or pause */
  tick_accum += dt;

  GameInput in;
  poll_input(&in);
  pending_hit |= in.hit;

  /* Run fixed 30Hz ticks (33ms per tick) */
  if (tick_accum >= 33u) {
    int steps = 0;
    in.hit = pending_hit;
    pending_hit = 0;
    pending_enc_sel = 0;
    while (tick_accum >= 33u && steps < 3) {
      step_simulation(&in);
      in.hit = 0; /* edge triggers only fire on first substep */
      tick_accum -= 33u;
      steps++;
    }
    update_leds();
    render_frame();
  }
}
