/* SPDX-License-Identifier: GPL-3.0-only */
/* jobs.c: Pawn Kings gun shop, Respray garage, Docks Car Export, Taxi fares, 16 hidden stashes */
#include "oop.h"

uint8_t shop_open = 0;
int8_t shop_sel = 0;
uint16_t stashes_mask = 0;

/* ---- Pawn Kings gun shop */
static const struct {
  const char *name;
  uint8_t item;
  int16_t price;
} SHOP_STOCK[5] = {
    {"PISTOL", PK_PISTOL, 350},
    {"SMG", PK_SMG, 1200},
    {"SHOTGUN", PK_SHOTGUN, 900},
    {"AMMO PACK", PK_AMMO, 120},
    {"BODY ARMOR", PK_ARMOR, 400},
};

int shop_item_count(void) { return 5; }
const char *shop_item_name(int i) { return (i >= 0 && i < 5) ? SHOP_STOCK[i].name : ""; }
int shop_item_price(int i) { return (i >= 0 && i < 5) ? SHOP_STOCK[i].price : 0; }

/* ---- Respray garage */
static int16_t spray_cd = 0;

/* ---- Docks car export */
static uint8_t export_want[3] = {CAR_SEDAN, CAR_PICKUP, CAR_SUV};
static int16_t export_cd = 0;

int export_wanted_model(int idx) {
  if (idx < 0 || idx >= 3) return CAR_SEDAN;
  return export_want[idx];
}

static void export_new_list(void) {
  int i;
  for (i = 0; i < 3; i++) {
    export_want[i] = (uint8_t)(oop_rnd() % 8u); /* non-cop, non-taxi */
  }
}

/* ---- Taxi fares */
enum { F_NONE = 0, F_WAIT, F_RIDE };
static uint8_t fare_state = F_NONE;
static int32_t fare_x = 0, fare_y = 0;
static int32_t fare_from_x = 0, fare_from_y = 0;
static int16_t fare_t = 0;
static int16_t fare_cd = 90;

/* ---- 16 Hidden Stashes (tile coords x, y) */
static const uint8_t STASH_TILES[NSTASH][2] = {
    {8, 8},   {26, 18}, {36, 8},  {52, 8},  {8, 20},  {18, 28}, {34, 24}, {52, 20},
    {8, 36},  {20, 42}, {36, 36}, {52, 36}, {10, 48}, {28, 52}, {42, 52}, {50, 48},
};

static int driving_car(void) {
  int ci = peds[PLAYER].car;
  if (ci >= 0 && ci < MAX_CARS && cars[ci].used && cars[ci].driver == PLAYER) return ci;
  return -1;
}

void jobs_init(void) {
  shop_open = 0;
  shop_sel = 0;
  spray_cd = 0;
  export_cd = 0;
  fare_state = F_NONE;
  fare_cd = 90;
  export_new_list();

  marker_set(SL_SHOP, pos_shop_x, pos_shop_y, MK_SHOP);
  marker_set(SL_RESPRAY, pos_respray_x, pos_respray_y, MK_RESPRAY);
  marker_set(SL_EXPORT, pos_export_x, pos_export_y, MK_EXPORT);
}

static void shop_tick(const GameInput *in) {
  Ped *pl = &peds[PLAYER];
  int here = (game_mode == MODE_PLAY && pl->car < 0 && dist_fp(pl->x, pl->y, pos_shop_x, pos_shop_y) < 1200);
  if (here && !shop_open) {
    shop_sel = 0;
    hud_msg("PAWN KINGS: GUNS IN THE BACK", 60);
  }
  shop_open = (uint8_t)here;
  if (!here) return;

  if (in->hit & IN_NEXT) {
    shop_sel = (int8_t)((shop_sel + 1) % 5);
    audio_sfx(SFX_UI);
  }
  if (in->hit & IN_PREV) {
    shop_sel = (int8_t)((shop_sel + 4) % 5);
    audio_sfx(SFX_UI);
  }
  if (in->hit & IN_ENTER) {
    int price = SHOP_STOCK[shop_sel].price;
    if (cash < price) {
      hud_msg("NOT ENOUGH CASH", 45);
      audio_sfx(SFX_EMPTY);
    } else if (give_item(SHOP_STOCK[shop_sel].item)) {
      char m[48];
      cash -= price;
      str_copy(m, "BOUGHT ", sizeof(m));
      str_cat(m, SHOP_STOCK[shop_sel].name, sizeof(m));
      hud_msg(m, 45);
      audio_sfx(SFX_CASH);
    } else {
      hud_msg("ALREADY FULL", 40);
      audio_sfx(SFX_EMPTY);
    }
  }
}

static void respray_tick(void) {
  int ci, i;
  if (spray_cd > 0) spray_cd--;
  ci = driving_car();
  if (ci < 0 || spray_cd > 0) return;
  if (dist_fp(cars[ci].x, cars[ci].y, pos_respray_x, pos_respray_y) > 1500) return;

  spray_cd = 90;
  if (heat == 0) {
    hud_msg("PAINT IS FINE. COME BACK WANTED.", 55);
    return;
  }
  /* Check if cops are right behind player */
  for (i = 0; i < MAX_CARS; i++) {
    if (cars[i].used && cars[i].ai == AI_COP && cars[i].driver != PLAYER) {
      if (dist_fp(cars[i].x, cars[i].y, cars[ci].x, cars[ci].y) < 5500) {
        hud_msg("COPS SAW YOU! SHAKE THEM FIRST.", 55);
        return;
      }
    }
  }
  int fee = 100 + 100 * heat;
  if (cash < fee) {
    char m[48];
    str_copy(m, "RESPRAY COSTS $", sizeof(m));
    str_cat_int(m, fee, sizeof(m));
    hud_msg(m, 55);
    return;
  }
  cash -= fee;
  cars[ci].paint = car_random_paint(CAR_SEDAN);
  cars[ci].health = car_specs[cars[ci].model].max_hp;
  heat_clear();
  audio_sfx(SFX_CASH);
  hud_msg("NEW PAINT! NOBODY SAW ANYTHING.", 65);
}

static void export_tick(void) {
  int ci, i, wanted = 0;
  if (export_cd > 0) export_cd--;
  ci = driving_car();
  if (ci < 0 || export_cd > 0) return;
  if (dist_fp(cars[ci].x, cars[ci].y, pos_export_x, pos_export_y) > 1800) return;

  for (i = 0; i < 3; i++) {
    if (cars[ci].model == export_want[i]) wanted = 1;
  }
  if (!wanted || (cars[ci].flags & CF_MISSION)) {
    char m[60];
    str_copy(m, "DOCKS WANT: ", sizeof(m));
    str_cat(m, car_specs[export_want[0]].name, sizeof(m));
    str_cat(m, ", ", sizeof(m));
    str_cat(m, car_specs[export_want[1]].name, sizeof(m));
    hud_msg(m, 65);
    export_cd = 90;
    return;
  }
  if (cars[ci].speed > 260 || cars[ci].speed < -260) return;

  int base = car_specs[cars[ci].model].export_pay;
  int hp_pct = (cars[ci].health * 100) / (car_specs[cars[ci].model].max_hp ? car_specs[cars[ci].model].max_hp : 1);
  if (hp_pct < 35) hp_pct = 35;
  int pay = (base * hp_pct) / 100;
  cash += pay;
  {
    char m[48];
    str_copy(m, "EXPORTED! +$", sizeof(m));
    str_cat_int(m, pay, sizeof(m));
    hud_msg(m, 65);
  }
  audio_sfx(SFX_CASH);
  /* Eject player & remove exported car */
  peds[PLAYER].car = -1;
  peds[PLAYER].state = PS_WALK;
  peds[PLAYER].x = cars[ci].x - 500;
  cars[ci].driver = -1;
  car_remove(ci);
  export_new_list();
  export_cd = 90;
}

static void taxi_tick(void) {
  int ci = driving_car();
  int in_taxi = (ci >= 0 && cars[ci].model == CAR_TAXI && heat == 0 && game_mode == MODE_PLAY);
  if (!in_taxi) {
    if (fare_state != F_NONE) {
      if (ci >= 0 && cars[ci].model == CAR_TAXI && heat > 0) hud_msg("FARE: COPS? I'M WALKING!", 50);
      fare_state = F_NONE;
      marker_set(SL_TAXI, 0, 0, MK_NONE);
    }
    fare_cd = 90;
    return;
  }

  if (fare_state == F_NONE) {
    if (fare_cd > 0) {
      fare_cd--;
      return;
    }
    /* Pick a sidewalk/road point 8..16 tiles away */
    int tx = (cars[ci].x >> 11) + (int)(oop_rnd() % 19u) - 9;
    int ty = (cars[ci].y >> 11) + (int)(oop_rnd() % 19u) - 9;
    if (tx < 6) tx = 14;
    if (tx > 54) tx = 46;
    if (ty < 6) ty = 14;
    if (ty > 46) ty = 38;
    fare_x = (tx << 11) + (TILE_FP >> 1);
    fare_y = (ty << 11) + (TILE_FP >> 1);
    if (!world_solid(fare_x, fare_y) && dist_fp(cars[ci].x, cars[ci].y, fare_x, fare_y) > 5000) {
      fare_state = F_WAIT;
      marker_set(SL_TAXI, fare_x, fare_y, MK_TARGET);
      hud_msg("TAXI: SOMEONE IS WAVING YOU DOWN", 60);
    }
    return;
  }

  if (fare_state == F_WAIT) {
    if (dist_fp(cars[ci].x, cars[ci].y, fare_x, fare_y) < 1600 && cars[ci].speed < 250 && cars[ci].speed > -250) {
      /* Pick destination across town */
      int tx = 6 + (int)(oop_rnd() % 7u) * 8;
      int ty = 6 + (int)(oop_rnd() % 6u) * 8;
      fare_from_x = cars[ci].x;
      fare_from_y = cars[ci].y;
      fare_x = (tx << 11) + (TILE_FP >> 1);
      fare_y = (ty << 11) + (TILE_FP >> 1);
      fare_t = 0;
      fare_state = F_RIDE;
      marker_set(SL_TAXI, fare_x, fare_y, MK_TARGET);
      hud_msg("FARE: DRIVE. I'M LATE.", 55);
    }
    return;
  }

  /* F_RIDE */
  fare_t++;
  if (dist_fp(cars[ci].x, cars[ci].y, fare_x, fare_y) < 1800 && cars[ci].speed < 300 && cars[ci].speed > -300) {
    int32_t d = dist_fp(fare_from_x, fare_from_y, fare_x, fare_y);
    int pay = 45 + (int)(d / 280);
    int tip = (fare_t < 450) ? (int)((450 - fare_t) / 10) : 0;
    cash += pay + tip;
    {
      char m[48];
      str_copy(m, "FARE $", sizeof(m));
      str_cat_int(m, pay, sizeof(m));
      str_cat(m, " + TIP $", sizeof(m));
      str_cat_int(m, tip, sizeof(m));
      hud_msg(m, 65);
    }
    audio_sfx(SFX_CASH);
    fare_state = F_NONE;
    fare_cd = 120;
    marker_set(SL_TAXI, 0, 0, MK_NONE);
  }
}

static void stashes_tick(void) {
  int i;
  int32_t px = peds[PLAYER].x, py = peds[PLAYER].y;
  int nearest = -1;
  int32_t best_d = 9000;

  for (i = 0; i < NSTASH; i++) {
    int32_t sx, sy, d;
    if (stashes_mask & (1u << i)) continue;
    sx = ((int32_t)STASH_TILES[i][0] << 11) + (TILE_FP >> 1);
    sy = ((int32_t)STASH_TILES[i][1] << 11) + (TILE_FP >> 1);
    d = dist_fp(px, py, sx, sy);
    if (d < 700) {
      stashes_mask |= (uint16_t)(1u << i);
      cash += 250;
      audio_sfx(SFX_CASH);
      int cnt = 0, k;
      for (k = 0; k < NSTASH; k++)
        if (stashes_mask & (1u << k)) cnt++;
      char m[48];
      str_copy(m, "STASH ", sizeof(m));
      str_cat_int(m, cnt, sizeof(m));
      str_cat(m, "/16 (+$250)", sizeof(m));
      hud_msg(m, 60);
      continue;
    }
    if (d < best_d) {
      best_d = d;
      nearest = i;
    }
  }

  if (nearest >= 0) {
    marker_set(SL_STASH, ((int32_t)STASH_TILES[nearest][0] << 11) + (TILE_FP >> 1),
               ((int32_t)STASH_TILES[nearest][1] << 11) + (TILE_FP >> 1), MK_STASH);
  } else {
    marker_set(SL_STASH, 0, 0, MK_NONE);
  }
}

void jobs_tick(const GameInput *in) {
  shop_tick(in);
  respray_tick();
  export_tick();
  taxi_tick();
  stashes_tick();
}
