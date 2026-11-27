/* SPDX-License-Identifier: GPL-3.0-only */
/* ped.c: player & NPC pedestrians, car entry/exit, panic AI, hospital/police respawn */
#include "oop.h"

Ped peds[MAX_PEDS];

static const uint16_t SHIRT_COLS[8] = {
    RGB(210, 64, 64),  RGB(64, 130, 210), RGB(68, 175, 92), RGB(220, 175, 48),
    RGB(168, 86, 198), RGB(215, 215, 220), RGB(220, 118, 56), RGB(70, 175, 175),
};
static const uint16_t SKIN_COLS[4] = {
    RGB(235, 194, 160), RGB(208, 158, 118), RGB(166, 112, 76), RGB(118, 76, 52),
};

void peds_init(void) {
  int i;
  for (i = 0; i < MAX_PEDS; i++) peds[i].used = 0;

  /* Player at Walt's lot */
  Ped *pl = &peds[PLAYER];
  pl->used = 1;
  pl->kind = PED_PLAYER;
  pl->state = PS_WALK;
  pl->ang = 64;
  pl->x = pos_walt_x;
  pl->y = pos_walt_y;
  pl->vx = 0;
  pl->vy = 0;
  pl->health = 100;
  pl->armor = 0;
  pl->car = -1;
  pl->weapon = W_FIST;
  pl->owned_w = (1u << W_FIST) | (1u << W_PISTOL);
  for (i = 0; i < W_COUNT; i++) {
    pl->ammo[i] = 0;
    pl->clip_ammo[i] = 0;
  }
  pl->ammo[W_PISTOL] = 36;
  pl->clip_ammo[W_PISTOL] = 12;
  pl->fire_cd = 0;
  pl->reload_t = 0;
  pl->shirt = RGB(240, 148, 38); /* Parole orange/amber jacket */
  pl->skin = SKIN_COLS[0];
  pl->target = -1;
}

int ped_spawn(uint8_t kind, int32_t x, int32_t y, uint8_t ang) {
  int i;
  for (i = 1; i < MAX_PEDS; i++) {
    if (!peds[i].used) {
      Ped *p = &peds[i];
      p->used = 1;
      p->kind = kind;
      p->state = PS_WALK;
      p->ang = ang;
      p->x = x;
      p->y = y;
      p->vx = 0;
      p->vy = 0;
      p->health = (kind == PED_COP) ? 110 : 60;
      p->armor = 0;
      p->car = -1;
      p->weapon = (kind == PED_COP) ? W_PISTOL : W_FIST;
      p->owned_w = (kind == PED_COP) ? (1u << W_PISTOL) : (1u << W_FIST);
      p->ammo[W_PISTOL] = 999;
      p->clip_ammo[W_PISTOL] = 12;
      p->fire_cd = 0;
      p->reload_t = 0;
      p->timer = (int16_t)(60 + (oop_rnd() & 127u));
      p->walk_anim = 0;
      p->shirt = (kind == PED_COP) ? RGB(42, 76, 168) : SHIRT_COLS[oop_rnd() & 7u];
      p->skin = SKIN_COLS[oop_rnd() & 3u];
      p->target = -1;
      return i;
    }
  }
  return -1;
}

void ped_remove(int pi) {
  if (pi <= 0 || pi >= MAX_PEDS || !peds[pi].used) return;
  if (peds[pi].car >= 0 && peds[pi].car < MAX_CARS) {
    if (cars[peds[pi].car].driver == pi) cars[peds[pi].car].driver = -1;
  }
  peds[pi].used = 0;
}

void ped_alarm(int32_t x, int32_t y, int32_t rad) {
  int i;
  for (i = 1; i < MAX_PEDS; i++) {
    Ped *p = &peds[i];
    if (!p->used || p->kind != PED_CIV || p->state == PS_DEAD || p->car >= 0) continue;
    if (dist_fp(x, y, p->x, p->y) < rad) {
      p->state = PS_FLEE;
      p->timer = 180;
    }
  }
}

void player_respawn(int32_t rx, int32_t ry, const char *banner) {
  Ped *pl = &peds[PLAYER];
  if (pl->car >= 0 && pl->car < MAX_CARS) {
    if (cars[pl->car].driver == PLAYER) cars[pl->car].driver = -1;
    pl->car = -1;
  }
  pl->x = rx;
  pl->y = ry;
  pl->vx = 0;
  pl->vy = 0;
  pl->health = 100;
  pl->state = PS_WALK;
  pl->fire_cd = 0;
  pl->reload_t = 0;
  if (cash >= 100) cash -= 100;
  else cash = 0;
  cops_reset();
  rob_active = 0;
  cut_kind = CUT_NONE;
  cut_t = 0;
  game_mode = MODE_PLAY;
  hud_big(banner, 75);
}

void ped_damage(int pi, int16_t dmg, int32_t kx, int32_t ky, int attacker) {
  Ped *p;
  if (pi < 0 || pi >= MAX_PEDS || !peds[pi].used || peds[pi].state == PS_DEAD) return;
  p = &peds[pi];

  /* Armor absorbs first */
  if (p->armor > 0) {
    if (p->armor >= dmg) {
      p->armor -= dmg;
      dmg = 0;
    } else {
      dmg -= p->armor;
      p->armor = 0;
    }
  }
  p->health -= dmg;
  p->vx += kx >> 2;
  p->vy += ky >> 2;
  fx_burst(FX_BLOOD, p->x, p->y, 4);
  audio_sfx(SFX_HIT);

  if (pi == PLAYER) {
    cam_shake = 5;
    if (p->health <= 0) {
      p->health = 0;
      p->state = PS_DEAD;
      p->timer = 75;
      game_mode = MODE_DEAD;
      hud_big("WASTED", 75);
    }
    return;
  }

  if (attacker == PLAYER) {
    if (p->health <= 0) {
      crime(p->kind == PED_COP ? CR_COP_MURDER : CR_MURDER, p->x, p->y);
    } else {
      crime(p->kind == PED_COP ? CR_COP_ASSAULT : CR_ASSAULT, p->x, p->y);
    }
  }

  if (p->health <= 0) {
    p->health = 0;
    p->state = PS_DEAD;
    p->timer = 180;
    /* Drop cash or ammo */
    if (p->kind == PED_COP) {
      pickup_drop(p->x, p->y, PK_AMMO, 18);
    } else if ((oop_rnd() & 1u) == 0) {
      pickup_drop(p->x, p->y, PK_CASH, (int16_t)(15 + (oop_rnd() % 45u)));
    }
  } else if (p->kind == PED_CIV) {
    p->state = PS_FLEE;
    p->timer = 210;
  } else if (p->kind == PED_COP) {
    p->state = PS_CHASE;
    p->target = PLAYER;
  }
}

static void spawn_ambient_peds(void) {
  int i, civs = 0;
  int32_t px = peds[PLAYER].x, py = peds[PLAYER].y;
  for (i = 1; i < MAX_PEDS; i++) {
    if (!peds[i].used) continue;
    if (peds[i].car < 0 && dist_fp(px, py, peds[i].x, peds[i].y) > 22000) {
      ped_remove(i);
      continue;
    }
    if (peds[i].kind == PED_CIV && peds[i].car < 0) civs++;
  }
  if (civs >= 12) return;

  for (i = 0; i < 6; i++) {
    int tx = (px >> 11) + (int)(oop_rnd() % 21u) - 10;
    int ty = (py >> 11) + (int)(oop_rnd() % 21u) - 10;
    if (tx < 5 || tx > 55 || ty < 5 || ty > 49) continue;
    if (tile_map[ty][tx] != T_SIDEWALK) continue;
    int32_t sx = (tx << 11) + (TILE_FP >> 1);
    int32_t sy = (ty << 11) + (TILE_FP >> 1);
    if (dist_fp(px, py, sx, sy) < 6000) continue;
    ped_spawn(PED_CIV, sx, sy, (uint8_t)(oop_rnd() & 255u));
    break;
  }
}

static void handle_enter_exit(Ped *pl) {
  if (pl->car >= 0) {
    /* Exit car */
    Car *c = &cars[pl->car];
    int ang = ((c->ang16 >> 4) + 192) & 255; /* driver left side */
    int32_t ex = c->x + (sin256(ang) * 2);
    int32_t ey = c->y - (cos256(ang) * 2);
    c->driver = -1;
    c->flags &= ~CF_HORN;
    pl->car = -1;
    pl->state = PS_WALK;
    pl->x = ex;
    pl->y = ey;
    world_push(&pl->x, &pl->y, 140);
    audio_sfx(SFX_DOOR);
    return;
  }

  /* Don't hijack while shopping */
  if (shop_open) return;

  int ci = car_near(pl->x, pl->y, 1400);
  if (ci >= 0) {
    Car *c = &cars[ci];
    if (c->driver >= 0 && c->driver < MAX_PEDS) {
      int d = c->driver;
      peds[d].car = -1;
      peds[d].state = (peds[d].kind == PED_COP) ? PS_CHASE : PS_FLEE;
      peds[d].timer = 240;
      peds[d].x = c->x + 420;
      peds[d].y = c->y;
      world_push(&peds[d].x, &peds[d].y, 140);
      crime(CR_CARJACK, c->x, c->y);
      hud_msg("HEY! MY CAR!", 45);
      /* Trigger 3D-style carjack cutscene! */
      cut_kind = CUT_CARJACK;
      cut_t = 0;
      cut_car_paint = c->paint;
      cut_car_model = c->model;
      cut_ped_shirt = peds[d].shirt;
    }
    c->driver = PLAYER;
    c->ai = AI_NONE;
    pl->car = (int8_t)ci;
    pl->state = PS_IN_CAR;
    pl->x = c->x;
    pl->y = c->y;
    audio_sfx(SFX_DOOR);
    hud_msg(car_specs[c->model].name, 45);
  }
}

void peds_tick(const GameInput *in) {
  int i;
  Ped *pl = &peds[PLAYER];

  if (game_mode == MODE_DEAD || game_mode == MODE_BUSTED) {
    if (pl->timer > 0) pl->timer--;
    else {
      if (game_mode == MODE_DEAD) player_respawn(pos_hosp_x, pos_hosp_y, "DISCHARGED -$100");
      else player_respawn(pos_police_x, pos_police_y, "RELEASED -$100");
    }
    return;
  }

  if ((game_ticks % 20u) == 0) spawn_ambient_peds();

  /* Player enter/exit car */
  if (in->hit & IN_ENTER) {
    handle_enter_exit(pl);
  }

  /* Player on-foot movement */
  if (pl->car < 0) {
    int32_t spd = (in->held & IN_SPRINT) ? 135 : 88;
    int32_t vx = (in->mx * spd) >> 8;
    int32_t vy = (-in->my * spd) >> 8; /* my > 0 is North (-Y) */
    if (vx != 0 || vy != 0) {
      pl->ang = atan2_256(vx, vy);
      pl->walk_anim += (in->held & IN_SPRINT) ? 2 : 1;
    }
    pl->x += vx;
    pl->y += vy;
    world_push(&pl->x, &pl->y, 130);

    /* Push out of cars */
    for (i = 0; i < MAX_CARS; i++) {
      if (!cars[i].used) continue;
      int32_t d = dist_fp(pl->x, pl->y, cars[i].x, cars[i].y);
      int32_t min_d = car_specs[cars[i].model].half_w + 110;
      if (d < min_d && d > 1) {
        int32_t push = min_d - d;
        pl->x += ((pl->x - cars[i].x) * push) / d;
        pl->y += ((pl->y - cars[i].y) * push) / d;
      }
    }
  }

  /* Update NPCs */
  for (i = 1; i < MAX_PEDS; i++) {
    Ped *p = &peds[i];
    if (!p->used || p->car >= 0) continue;

    if (p->state == PS_DEAD) {
      if (p->timer > 0) p->timer--;
      else p->used = 0;
      continue;
    }

    if (p->fire_cd > 0) p->fire_cd--;

    if (p->kind == PED_COP) {
      if (heat > 0) {
        int32_t d = dist_fp(p->x, p->y, pl->x, pl->y);
        p->ang = atan2_256(pl->x - p->x, pl->y - p->y);
        int32_t stop_d = (heat == 1) ? 340 : 1400;
        if (d > stop_d) {
          p->x += (sin256(p->ang) * 86) >> 8;
          p->y -= (cos256(p->ang) * 86) >> 8;
          p->walk_anim++;
        }
        /* Bust player if close, on foot (or stopped car), low heat */
        if (d < 540 && (pl->car < 0 || (cars[pl->car].speed > -120 && cars[pl->car].speed < 120))) {
          if (heat == 1) {
            game_mode = MODE_BUSTED;
            pl->timer = 90;
            cut_kind = CUT_BUSTED;
            cut_t = 0;
            hud_big("BUSTED", 90);
            audio_sfx(SFX_SIREN_BLIP);
            return;
          }
        }
        /* Shoot at player (1-star cops arrest first; 2+ stars shoot on sight) */
        if (d < 5600 && p->fire_cd <= 0 && heat >= 2) {
          fire_weapon(i);
          p->fire_cd = 26;
        }
      } else {
        /* Patrol */
        p->x += (sin256(p->ang) * 40) >> 8;
        p->y -= (cos256(p->ang) * 40) >> 8;
        if (world_push(&p->x, &p->y, 130)) p->ang = (uint8_t)(p->ang + 96);
      }
    } else {
      /* Civilian */
      if (p->state == PS_FLEE) {
        p->ang = atan2_256(p->x - pl->x, p->y - pl->y);
        p->x += (sin256(p->ang) * 84) >> 8;
        p->y -= (cos256(p->ang) * 84) >> 8;
        p->walk_anim += 2;
        if (p->timer > 0) p->timer--;
        else p->state = PS_WALK;
      } else {
        p->x += (sin256(p->ang) * 34) >> 8;
        p->y -= (cos256(p->ang) * 34) >> 8;
        p->walk_anim++;
        if (p->timer > 0) p->timer--;
        else {
          p->ang = (uint8_t)((p->ang + ((oop_rnd() & 1u) ? 64 : 192)) & 255);
          p->timer = (int16_t)(70 + (oop_rnd() & 127u));
        }
      }
    }

    if (world_push(&p->x, &p->y, 120)) {
      p->ang = (uint8_t)((p->ang + 96) & 255);
    }
  }
}
