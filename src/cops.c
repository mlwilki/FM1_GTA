/* SPDX-License-Identifier: GPL-3.0-only */
/* cops.c: 5-star wanted system, police cruisers, on-foot cops, police helicopter */
#include "oop.h"

int8_t heat = 0;
int16_t heat_search = 0;
int32_t last_seen_x = 0, last_seen_y = 0;
uint8_t heli_active = 0;
int32_t heli_x = 0, heli_y = 0;
uint8_t heli_ang = 0;
static int16_t heli_cd = 0;
static int16_t cop_spawn_cd = 0;

void cops_init(void) { cops_reset(); }

void cops_reset(void) {
  int i;
  heat = 0;
  heat_search = 0;
  heli_active = 0;
  cop_spawn_cd = 0;
  for (i = 1; i < MAX_PEDS; i++) {
    if (peds[i].used && peds[i].kind == PED_COP) ped_remove(i);
  }
  for (i = 0; i < MAX_CARS; i++) {
    if (cars[i].used && cars[i].ai == AI_COP && cars[i].driver != PLAYER) {
      car_remove(i);
    }
  }
}

void heat_clear(void) {
  heat = 0;
  heat_search = 0;
  heli_active = 0;
  int i;
  for (i = 0; i < MAX_CARS; i++) {
    if (cars[i].used && cars[i].ai == AI_COP && cars[i].driver != PLAYER) {
      cars[i].ai = AI_TRAFFIC;
      cars[i].flags &= ~CF_SIREN;
    }
  }
}

void crime(uint8_t kind, int32_t x, int32_t y) {
  int8_t min_heat = 1;
  switch (kind) {
  case CR_SHOTS:
  case CR_ASSAULT:
  case CR_CARJACK:
  case CR_HIT_PED:
    min_heat = 1;
    break;
  case CR_MURDER:
  case CR_ROBBERY:
  case CR_DESTROY_CAR:
    min_heat = 2;
    break;
  case CR_COP_ASSAULT:
    min_heat = (heat < 2) ? 2 : (int8_t)(heat + 1);
    break;
  case CR_COP_MURDER:
    min_heat = (heat < 3) ? 3 : (int8_t)(heat + 1);
    break;
  default:
    min_heat = 1;
    break;
  }
  if (min_heat > 5) min_heat = 5;
  if (min_heat > heat) {
    heat = min_heat;
    audio_sfx(SFX_SIREN_BLIP);
    if (heat == 3) hud_msg("DISPATCH: CHOPPER INBOUND!", 60);
  }
  heat_search = (int16_t)(360 + heat * 120);
  last_seen_x = x;
  last_seen_y = y;
}

static void spawn_cop_unit(void) {
  int32_t px = peds[PLAYER].x, py = peds[PLAYER].y;
  int ptx = px >> 11, pty = py >> 11;
  int k;
  for (k = 0; k < 12; k++) {
    int tx, ty;
    if (k < 4) {
      /* Prefer spawning down the same road/avenue for direct pursuit */
      int off = ((k & 1) ? 7 : -7) + (k >= 2 ? ((k & 1) ? 2 : -2) : 0);
      if (world_is_road(ptx, pty)) {
        tx = (tile_map[pty][ptx] == T_ROAD_H) ? (ptx + off) : ptx;
        ty = (tile_map[pty][ptx] == T_ROAD_V) ? (pty + off) : pty;
      } else {
        tx = ptx + off;
        ty = pty;
      }
    } else {
      tx = ptx + (int)(oop_rnd() % 23u) - 11;
      ty = pty + (int)(oop_rnd() % 23u) - 11;
    }
    if (tx < 6 || tx > 54 || ty < 6 || ty > 46) continue;
    if (!world_is_road(tx, ty)) continue;
    int32_t sx = (tx << 11) + (TILE_FP >> 1);
    int32_t sy = (ty << 11) + (TILE_FP >> 1);
    int32_t d = dist_fp(px, py, sx, sy);
    if (d < 7000 || d > 20000) continue;
    if (car_near(sx, sy, 3000) >= 0) continue;

    int ci = car_spawn(CAR_COP, sx, sy, atan2_256(px - sx, py - sy), AI_COP, car_random_paint(CAR_COP));
    if (ci >= 0) {
      int di = ped_spawn(PED_COP, sx, sy, cars[ci].ang16 >> 4);
      if (di >= 0) {
        peds[di].car = (int8_t)ci;
        peds[di].state = PS_IN_CAR;
        cars[ci].driver = (int8_t)di;
      }
      return;
    }
  }
}

void cops_tick(void) {
  int i;
  int32_t px = peds[PLAYER].x, py = peds[PLAYER].y;
  int cop_cars = 0, cop_peds = 0;
  int seen = 0;

  if (heat <= 0) {
    heli_active = 0;
    return;
  }

  for (i = 0; i < MAX_CARS; i++) {
    if (!cars[i].used || cars[i].ai != AI_COP || cars[i].driver == PLAYER) continue;
    cop_cars++;
    int32_t d = dist_fp(px, py, cars[i].x, cars[i].y);
    if (d < 9500) seen = 1;
    /* If cop cruiser gets close to on-foot player, cop hops out to chase/shoot */
    if (d < 2600 && peds[PLAYER].car < 0 && cars[i].driver > 0) {
      int di = cars[i].driver;
      cars[i].driver = -1;
      cars[i].speed = 0;
      peds[di].car = -1;
      peds[di].state = PS_CHASE;
      peds[di].x = cars[i].x + 320;
      peds[di].y = cars[i].y;
    }
  }

  for (i = 1; i < MAX_PEDS; i++) {
    if (!peds[i].used || peds[i].kind != PED_COP || peds[i].state == PS_DEAD) continue;
    cop_peds++;
    if (dist_fp(px, py, peds[i].x, peds[i].y) < 7500) seen = 1;
  }

  /* Helicopter at 3+ stars */
  if (heat >= 3) {
    if (!heli_active) {
      heli_active = 1;
      heli_x = px - 14000;
      heli_y = py - 14000;
      heli_cd = 60;
    }
    int32_t dx = px - heli_x;
    int32_t dy = py - heli_y;
    int32_t d = dist_fp(heli_x, heli_y, px, py);
    heli_ang = atan2_256(dx, dy);
    if (d > 2200) {
      heli_x += (sin256(heli_ang) * 165) >> 8;
      heli_y -= (cos256(heli_ang) * 165) >> 8;
    } else {
      /* Orbit slowly */
      uint8_t orb = (uint8_t)(heli_ang + 64);
      heli_x += (sin256(orb) * 90) >> 8;
      heli_y -= (cos256(orb) * 90) >> 8;
    }
    if (d < 6500) seen = 1;
    if (heat >= 4 && d < 5500) {
      if (heli_cd > 0) heli_cd--;
      else {
        heli_cd = (heat == 5) ? 16 : 28;
        /* Fire downward burst */
        for (i = 0; i < MAX_BULLETS; i++) {
          if (!bullets[i].used) {
            bullets[i].used = 1;
            bullets[i].owner = -1;
            bullets[i].x = heli_x;
            bullets[i].y = heli_y;
            bullets[i].vx = (sin256(heli_ang) * 380) >> 8;
            bullets[i].vy = (-cos256(heli_ang) * 380) >> 8;
            bullets[i].dmg = 14;
            bullets[i].life = 16;
            audio_sfx(SFX_SMG);
            break;
          }
        }
      }
    }
  } else {
    heli_active = 0;
  }

  /* Spawn cops up to heat cap */
  if (cop_spawn_cd > 0) cop_spawn_cd--;
  else if (cop_cars < (heat > 4 ? 4 : heat) && cop_peds < 8) {
    spawn_cop_unit();
    cop_spawn_cd = 75;
  }

  /* Evasion / search cooldown */
  if (seen) {
    heat_search = (int16_t)(300 + heat * 90);
    last_seen_x = px;
    last_seen_y = py;
  } else {
    if (heat_search > 0) heat_search--;
    else {
      heat--;
      if (heat > 0) {
        heat_search = 240;
      } else {
        heat_clear();
        hud_msg("EVADED THE COPS!", 55);
      }
    }
  }
}
