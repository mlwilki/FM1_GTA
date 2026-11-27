/* SPDX-License-Identifier: GPL-3.0-only */
/* car.c: 10 vehicle models, integer arcade driving physics, traffic/cop/flee AI, explosions */
#include "oop.h"

Car cars[MAX_CARS];
Skid skids[MAX_SKIDS];
static int skid_head = 0;

const CarSpec car_specs[CAR_MODELS] = {
    /* name,       hw,  hl, max_spd, accel, turn, hp,  cyl, export */
    {"SEDAN",     240, 480, 1450, 48, 86, 1000, 6, 400},
    {"COMPACT",   215, 410, 1320, 52, 98,  850, 4, 300},
    {"HATCH",     220, 420, 1480, 56, 96,  880, 4, 330},
    {"MINIVAN",   255, 510, 1220, 40, 76, 1150, 6, 450},
    {"PICKUP",    255, 530, 1360, 44, 78, 1250, 8, 550},
    {"SUV",       265, 520, 1420, 46, 80, 1300, 8, 700},
    {"WAGON",     245, 500, 1340, 45, 82, 1050, 4, 420},
    {"OFFROAD",   265, 490, 1520, 54, 88, 1400, 8, 800},
    {"COP CAR",   245, 495, 1620, 62, 92, 1350, 8,   0},
    {"TAXI",      240, 480, 1440, 50, 88, 1050, 4,   0},
};

static const uint16_t CAR_PAINTS[8] = {
    RGB(205, 48, 52),   /* crimson */
    RGB(46, 98, 192),   /* cobalt */
    RGB(215, 215, 222), /* silver */
    RGB(42, 46, 54),    /* obsidian */
    RGB(56, 142, 86),   /* emerald */
    RGB(224, 158, 38),  /* amber */
    RGB(152, 68, 178),  /* violet */
    RGB(192, 182, 158), /* sand */
};

uint16_t car_random_paint(uint8_t model) {
  if (model == CAR_COP) return RGB(32, 36, 48);
  if (model == CAR_TAXI) return RGB(242, 192, 24);
  return CAR_PAINTS[oop_rnd() & 7u];
}

void car_add_skid(int32_t x, int32_t y) {
  skids[skid_head].used = 1;
  skids[skid_head].x = x;
  skids[skid_head].y = y;
  skids[skid_head].life = 140;
  skid_head = (skid_head + 1) % MAX_SKIDS;
}

void cars_init(void) {
  int i;
  for (i = 0; i < MAX_CARS; i++) cars[i].used = 0;
  for (i = 0; i < MAX_SKIDS; i++) skids[i].used = 0;
  skid_head = 0;

  /* Seed a few starter vehicles near Walt's lot & Market Row */
  car_spawn(CAR_SEDAN, pos_walt_x + 2400, pos_walt_y + 1200, 128, AI_PARKED, RGB(195, 55, 55));
  {
    int ti = car_spawn(CAR_TAXI, pos_walt_x + 4200, pos_walt_y - 2600, 128, AI_TRAFFIC, car_random_paint(CAR_TAXI));
    if (ti >= 0) {
      int di = ped_spawn(PED_CIV, cars[ti].x, cars[ti].y, 128);
      if (di >= 0) {
        peds[di].car = (int8_t)ti;
        peds[di].state = PS_IN_CAR;
        cars[ti].driver = (int8_t)di;
      }
    }
  }
  car_spawn(CAR_HATCH, pos_shop_x - 2400, pos_shop_y + 1800, 0, AI_PARKED, RGB(48, 130, 210));
}

int car_spawn(uint8_t model, int32_t x, int32_t y, uint8_t ang, uint8_t ai, uint16_t paint) {
  int i;
  if (model >= CAR_MODELS) model = CAR_SEDAN;
  for (i = 0; i < MAX_CARS; i++) {
    if (!cars[i].used) {
      Car *c = &cars[i];
      c->used = 1;
      c->model = model;
      c->ai = ai;
      c->flags = (model == CAR_COP && ai == AI_COP) ? CF_SIREN : 0;
      c->x = x;
      c->y = y;
      c->vx = 0;
      c->vy = 0;
      c->ang16 = (int16_t)((int32_t)ang << 4);
      c->speed = 0;
      c->health = car_specs[model].max_hp;
      c->paint = paint ? paint : car_random_paint(model);
      c->driver = -1;
      c->target = -1;
      c->fire_t = 0;
      c->ai_t = 0;
      c->skid = 0;
      c->rpm = 0;
      return i;
    }
  }
  return -1;
}

void car_remove(int ci) {
  if (ci < 0 || ci >= MAX_CARS || !cars[ci].used) return;
  if (cars[ci].driver >= 0 && cars[ci].driver < MAX_PEDS) {
    int d = cars[ci].driver;
    if (d != PLAYER) ped_remove(d);
    else peds[PLAYER].car = -1;
  }
  cars[ci].used = 0;
}

int car_near(int32_t x, int32_t y, int32_t rad) {
  int i, best = -1;
  int32_t best_d = rad;
  for (i = 0; i < MAX_CARS; i++) {
    int32_t d;
    if (!cars[i].used || cars[i].ai == AI_WRECK) continue;
    d = dist_fp(x, y, cars[i].x, cars[i].y);
    if (d < best_d) {
      best_d = d;
      best = i;
    }
  }
  return best;
}

void car_explode(int ci, int attacker) {
  Car *c = &cars[ci];
  int i;
  if (!c->used || c->ai == AI_WRECK) return;
  c->ai = AI_WRECK;
  c->health = 0;
  c->speed = 0;
  c->vx = 0;
  c->vy = 0;
  c->flags &= ~(CF_SIREN | CF_HORN | CF_LIGHTS);
  c->paint = RGB(44, 42, 42);
  fx_burst(FX_EXPLOSION, c->x, c->y, 14);
  audio_sfx(SFX_EXPLOSION);
  cam_shake = 18;
  ped_alarm(c->x, c->y, 18000);

  if (attacker == PLAYER) {
    crime(CR_DESTROY_CAR, c->x, c->y);
  }

  /* Eject/kill driver */
  if (c->driver >= 0 && c->driver < MAX_PEDS) {
    int d = c->driver;
    c->driver = -1;
    peds[d].car = -1;
    peds[d].state = PS_WALK;
    ped_damage(d, 150, 0, 0, attacker);
  }

  /* Blast nearby peds and cars */
  for (i = 0; i < MAX_PEDS; i++) {
    int32_t d;
    if (!peds[i].used || peds[i].state == PS_DEAD || peds[i].car >= 0) continue;
    d = dist_fp(c->x, c->y, peds[i].x, peds[i].y);
    if (d < 3600) {
      ped_damage(i, (int16_t)((3600 - d) / 28), 0, 0, attacker);
    }
  }
  for (i = 0; i < MAX_CARS; i++) {
    int32_t d;
    if (i == ci || !cars[i].used || cars[i].ai == AI_WRECK) continue;
    d = dist_fp(c->x, c->y, cars[i].x, cars[i].y);
    if (d < 3200) {
      car_damage(i, 450, attacker);
    }
  }
}

void car_damage(int ci, int16_t dmg, int attacker) {
  Car *c;
  if (ci < 0 || ci >= MAX_CARS || !cars[ci].used || cars[ci].ai == AI_WRECK) return;
  c = &cars[ci];
  c->health -= dmg;
  if (c->health <= 0) {
    car_explode(ci, attacker);
  }
}

static void ai_steer_to(Car *c, int32_t tx, int32_t ty, int16_t target_spd, int16_t *out_throt, int16_t *out_steer) {
  int32_t dx = tx - c->x;
  int32_t dy = ty - c->y;
  uint8_t want = atan2_256(dx, dy);
  int32_t ang = (c->ang16 >> 4) & 255;
  int32_t diff = ang_diff(want, ang);

  if (diff > 10) *out_steer = 256;
  else if (diff < -10) *out_steer = -256;
  else *out_steer = (int16_t)(diff * 24);

  if (diff > 72 || diff < -72) {
    *out_throt = -128; /* reverse/turn around */
  } else if (c->speed < target_spd) {
    *out_throt = 256;
  } else {
    *out_throt = 0;
  }
}

static void spawn_ambient_traffic(void) {
  int i, count = 0;
  int32_t px = peds[PLAYER].x, py = peds[PLAYER].y;
  for (i = 0; i < MAX_CARS; i++) {
    if (!cars[i].used) continue;
    /* Despawn far non-mission, non-player cars */
    if (cars[i].driver != PLAYER && !(cars[i].flags & CF_MISSION)) {
      int32_t d = dist_fp(px, py, cars[i].x, cars[i].y);
      if (d > 26000) {
        car_remove(i);
        continue;
      }
    }
    count++;
  }

  if (count >= 14) return;

  /* Try to spawn a traffic car on a road tile ~10..18 tiles away from player */
  for (i = 0; i < 6; i++) {
    int ptx = px >> 11;
    int pty = py >> 11;
    int ox = (int)(oop_rnd() % 29u) - 14;
    int oy = (int)(oop_rnd() % 29u) - 14;
    int tx = ptx + ox;
    int ty = pty + oy;
    uint8_t t;
    if (ox > -6 && ox < 6 && oy > -6 && oy < 6) continue;
    if (tx < 6 || tx > 54 || ty < 6 || ty > 46) continue;
    t = tile_map[ty][tx];
    if (t != T_ROAD_H && t != T_ROAD_V) continue;

    int32_t sx = (tx << 11) + (TILE_FP >> 1);
    int32_t sy = (ty << 11) + (TILE_FP >> 1);
    if (car_near(sx, sy, 4200) >= 0) continue;

    uint8_t ang = (t == T_ROAD_H) ? ((oop_rnd() & 1u) ? 64 : 192) : ((oop_rnd() & 1u) ? 0 : 128);
    uint8_t model = (uint8_t)(oop_rnd() % CAR_MODELS);
    if (model == CAR_COP) model = CAR_TAXI;
    int ci = car_spawn(model, sx, sy, ang, AI_TRAFFIC, 0);
    if (ci >= 0) {
      int di = ped_spawn(PED_CIV, sx, sy, ang);
      if (di >= 0) {
        peds[di].car = (int8_t)ci;
        peds[di].state = PS_IN_CAR;
        cars[ci].driver = (int8_t)di;
      }
    }
    break;
  }
}

void cars_tick(const GameInput *in) {
  int i, j;
  int night = world_night_level() > 100;

  /* Decay skid marks */
  for (i = 0; i < MAX_SKIDS; i++) {
    if (skids[i].used) {
      if (skids[i].life > 1) skids[i].life--;
      else skids[i].used = 0;
    }
  }

  if ((game_ticks % 18u) == 0) spawn_ambient_traffic();

  for (i = 0; i < MAX_CARS; i++) {
    Car *c = &cars[i];
    const CarSpec *sp;
    int16_t throt = 0, steer = 0;
    int handbrake = 0;
    if (!c->used) continue;
    sp = &car_specs[c->model];

    if (c->ai == AI_WRECK) {
      if ((game_ticks & 7u) == 0 && c->fire_t < 180) {
        c->fire_t++;
        fx_burst(FX_SMOKE, c->x, c->y, 1);
      }
      continue;
    }

    /* Smoke / Fire damage */
    if (c->health < 400 && (game_ticks & 7u) == 0) {
      fx_burst(FX_SMOKE, c->x, c->y, 1);
    }
    if (c->health < 150) {
      if ((game_ticks & 3u) == 0) fx_burst(FX_FIRE, c->x, c->y, 1);
      c->health -= 2;
      if (c->health <= 0) {
        car_explode(i, -1);
        continue;
      }
    }

    /* Headlights at night when driven */
    if (c->driver >= 0 && night) c->flags |= CF_LIGHTS;
    else c->flags &= ~CF_LIGHTS;

    c->flags &= ~CF_HORN;

    if (c->driver == PLAYER) {
      throt = in->my;
      steer = in->mx;
      handbrake = (in->held & IN_SPRINT) ? 1 : 0;
      if (in->held & IN_RELOAD) c->flags |= CF_HORN;
      if (c->model == CAR_COP && (in->hit & IN_RELOAD)) {
        c->flags ^= CF_SIREN;
      }
    } else if (c->driver >= 0) {
      if (c->ai == AI_COP) {
        c->flags |= CF_SIREN;
        ai_steer_to(c, peds[PLAYER].x, peds[PLAYER].y, (int16_t)(sp->max_spd - 120), &throt, &steer);
      } else if (c->ai == AI_FLEE) {
        /* Flee away from player along roads */
        int32_t dx = c->x - peds[PLAYER].x;
        int32_t dy = c->y - peds[PLAYER].y;
        int32_t tx = c->x + (dx > 0 ? 4096 : -4096);
        int32_t ty = c->y + (dy > 0 ? 4096 : -4096);
        if (tx < 8 * TILE_FP) tx = 16 * TILE_FP;
        if (tx > 52 * TILE_FP) tx = 44 * TILE_FP;
        if (ty < 8 * TILE_FP) ty = 16 * TILE_FP;
        if (ty > 44 * TILE_FP) ty = 36 * TILE_FP;
        ai_steer_to(c, tx, ty, (int16_t)(sp->max_spd - 140), &throt, &steer);
      } else if (c->ai == AI_TRAFFIC) {
        /* Follow road cardinal direction, stop at red lights & obstacles */
        int ang = ((c->ang16 >> 4) + 32) & 192; /* snap to 0, 64, 128, 192 */
        int32_t fx = sin256(ang), fy = -cos256(ang);
        int32_t ahead_x = c->x + (fx * 10);
        int32_t ahead_y = c->y + (fy * 10);
        uint8_t cur_t = world_tile(c->x, c->y);
        uint8_t nxt_t = world_tile(ahead_x, ahead_y);
        int blocked = 0;

        /* Red light check */
        if (nxt_t == T_ROAD_X && cur_t != T_ROAD_X) {
          int red = (ang == 0 || ang == 128) ? (world_light_ns() == 2) : (world_light_ew() == 2);
          if (red) blocked = 1;
        }
        /* Turn at intersection occasionally or if road ends */
        if (cur_t == T_ROAD_X && c->ai_t <= 0) {
          if ((oop_rnd() & 3u) == 0) {
            ang = (ang + ((oop_rnd() & 1u) ? 64 : 192)) & 255;
            c->ai_t = 45;
          } else {
            c->ai_t = 30;
          }
        }
        if (c->ai_t > 0) c->ai_t--;

        if (world_solid(ahead_x, ahead_y) || nxt_t == T_SIDEWALK || nxt_t == T_GRASS) {
          ang = (ang + 64) & 255;
        }

        /* Yield to cars/player right in front */
        if (dist_fp(ahead_x, ahead_y, peds[PLAYER].x, peds[PLAYER].y) < 1200) blocked = 1;
        for (j = 0; j < MAX_CARS; j++) {
          if (j != i && cars[j].used && dist_fp(ahead_x, ahead_y, cars[j].x, cars[j].y) < 1400) {
            blocked = 1;
            break;
          }
        }

        int32_t diff = ang_diff(ang, (c->ang16 >> 4) & 255);
        steer = (int16_t)(diff > 8 ? 192 : (diff < -8 ? -192 : diff * 20));
        throt = blocked ? -180 : (c->speed < 620 ? 180 : 0);
      }
    } else {
      /* Parked / driverless friction */
      throt = 0;
      steer = 0;
    }

    /* Acceleration / Braking */
    if (throt > 16) {
      int32_t a = (sp->accel * throt) >> 8;
      c->speed += (int16_t)a;
      if (c->speed > sp->max_spd) c->speed = sp->max_spd;
    } else if (throt < -16) {
      if (c->speed > 40) {
        c->speed -= (int16_t)((sp->accel * 2) - 10);
        if (c->speed < 0) c->speed = 0;
      } else {
        int32_t a = (sp->accel * (-throt)) >> 9;
        c->speed -= (int16_t)a;
        int16_t rev_max = -(sp->max_spd >> 2);
        if (c->speed < rev_max) c->speed = rev_max;
      }
    } else {
      /* Rolling drag */
      c->speed = (int16_t)((c->speed * 246) >> 8);
      if (c->speed > -12 && c->speed < 12) c->speed = 0;
    }

    if (handbrake) {
      c->speed = (int16_t)((c->speed * 232) >> 8);
    }

    /* Steering (proportional to speed, reversed in reverse) */
    {
      int32_t spd_abs = c->speed < 0 ? -c->speed : c->speed;
      if (spd_abs > 20 && steer != 0) {
        int32_t factor = spd_abs > 500 ? 256 : (spd_abs * 256) / 500;
        int32_t rate = handbrake ? (sp->turn_rate * 3) >> 1 : sp->turn_rate;
        int32_t d_ang = (steer * rate * factor) >> 16;
        if (c->speed < 0) d_ang = -d_ang;
        c->ang16 = (int16_t)((c->ang16 + d_ang) & 4095);
      }
    }

    /* Velocity with mild drift inertia */
    {
      int ang = (c->ang16 >> 4) & 255;
      int32_t tvx = (sin256(ang) * c->speed) >> 8;
      int32_t tvy = (-cos256(ang) * c->speed) >> 8;
      int32_t grip = handbrake ? 40 : 150; /* out of 256 */
      c->vx += ((tvx - c->vx) * grip) >> 8;
      c->vy += ((tvy - c->vy) * grip) >> 8;

      /* Skid calculation */
      int32_t slip = (tvx - c->vx) < 0 ? -(tvx - c->vx) : (tvx - c->vx);
      slip += (tvy - c->vy) < 0 ? -(tvy - c->vy) : (tvy - c->vy);
      if ((handbrake && (c->speed > 350 || c->speed < -250)) || slip > 260) {
        c->skid = 200;
        if ((game_ticks & 1u) == 0) car_add_skid(c->x, c->y);
      } else {
        c->skid = 0;
      }

      /* Engine RPM 0..255 */
      int32_t spd_abs = c->speed < 0 ? -c->speed : c->speed;
      int32_t r = 35 + (spd_abs * 220) / (sp->max_spd ? sp->max_spd : 1);
      if (r > 255) r = 255;
      c->rpm = (uint8_t)r;
    }

    /* Integrate position (vx/vy are Q8 m/s, scaled for 30Hz -> /8 feels snappy) */
    c->x += c->vx >> 3;
    c->y += c->vy >> 3;

    /* World collision */
    if (world_push(&c->x, &c->y, sp->half_w + 60)) {
      int32_t imp = c->speed < 0 ? -c->speed : c->speed;
      if (imp > 480) {
        int16_t dmg = (int16_t)((imp - 400) >> 2);
        car_damage(i, dmg, c->driver);
        fx_burst(FX_SPARK, c->x, c->y, 4);
        if (c->driver == PLAYER || dist_fp(c->x, c->y, peds[PLAYER].x, peds[PLAYER].y) < 6000) {
          audio_sfx(SFX_CRASH);
          if (c->driver == PLAYER) cam_shake = 6;
        }
      }
      c->speed = (int16_t)(-(c->speed >> 2));
      c->vx = -(c->vx >> 2);
      c->vy = -(c->vy >> 2);
    }

    /* Car-vs-car collision */
    for (j = i + 1; j < MAX_CARS; j++) {
      Car *o = &cars[j];
      int32_t dx, dy, d, min_d;
      if (!o->used) continue;
      dx = o->x - c->x;
      dy = o->y - c->y;
      min_d = sp->half_w + car_specs[o->model].half_w + 120;
      if (dx > min_d || dx < -min_d || dy > min_d || dy < -min_d) continue;
      d = dist_fp(c->x, c->y, o->x, o->y);
      if (d < min_d && d > 1) {
        int32_t push = (min_d - d) >> 1;
        int32_t nx = (dx * push) / d;
        int32_t ny = (dy * push) / d;
        c->x -= nx;
        c->y -= ny;
        o->x += nx;
        o->y += ny;

        int32_t rel = (c->speed - o->speed);
        if (rel < 0) rel = -rel;
        if (rel > 400) {
          int16_t dmg = (int16_t)(rel >> 3);
          car_damage(i, dmg, o->driver);
          car_damage(j, dmg, c->driver);
          fx_burst(FX_SPARK, (c->x + o->x) >> 1, (c->y + o->y) >> 1, 4);
          if (c->driver == PLAYER || o->driver == PLAYER) {
            audio_sfx(SFX_CRASH);
            cam_shake = 5;
          }
          if (o->ai == AI_COP && c->driver == PLAYER) {
            crime(CR_COP_ASSAULT, c->x, c->y);
          }
        }
        c->speed = (int16_t)(c->speed >> 1);
        o->speed = (int16_t)(o->speed >> 1);
      }
    }

    /* Sync driver position & run over pedestrians */
    if (c->driver >= 0 && c->driver < MAX_PEDS) {
      peds[c->driver].x = c->x;
      peds[c->driver].y = c->y;
      peds[c->driver].ang = (uint8_t)((c->ang16 >> 4) & 255);
    }

    int32_t spd_abs = c->speed < 0 ? -c->speed : c->speed;
    if (spd_abs > 320) {
      for (j = 0; j < MAX_PEDS; j++) {
        Ped *p = &peds[j];
        if (!p->used || p->state == PS_DEAD || p->car >= 0) continue;
        if (dist_fp(c->x, c->y, p->x, p->y) < sp->half_w + 140) {
          int16_t dmg = (int16_t)(spd_abs >> 3);
          ped_damage(j, dmg, c->vx, c->vy, c->driver);
          if (c->driver == PLAYER) {
            crime(p->kind == PED_COP ? CR_COP_ASSAULT : CR_HIT_PED, p->x, p->y);
          }
        }
      }
    }
  }
}
