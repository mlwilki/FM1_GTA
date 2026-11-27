/* SPDX-License-Identifier: GPL-3.0-only */
/* combat.c: 4 weapons, bullets, pickups, particles, store robbery */
#include "oop.h"

Bullet bullets[MAX_BULLETS];
Pickup pickups[MAX_PICKUPS];
Particle fx_list[MAX_FX];

uint8_t rob_active = 0;
int16_t rob_prog = 0;
int16_t rob_cash = 0;
static int16_t rob_cd = 0;

const WeaponSpec weapon_specs[W_COUNT] = {
    /* name,     dmg, cd, range, clip, reload, pellets, spread */
    {"FIST",     24, 12,  520,  0,  0, 1,  0},
    {"PISTOL",   34,  9, 6400, 12, 36, 1,  4},
    {"SMG",      20,  4, 5800, 30, 44, 1,  9},
    {"SHOTGUN",  22, 22, 4200,  6, 52, 5, 16},
};

void combat_init(void) {
  int i;
  for (i = 0; i < MAX_BULLETS; i++) bullets[i].used = 0;
  for (i = 0; i < MAX_PICKUPS; i++) pickups[i].used = 0;
  for (i = 0; i < MAX_FX; i++) fx_list[i].used = 0;
  rob_active = 0;
  rob_prog = 0;
  rob_cash = 0;
  rob_cd = 0;

  /* Place a few starter pickups around the city */
  pickup_drop(pos_hosp_x + 800, pos_hosp_y, PK_HEALTH, 100);
  pickup_drop(pos_police_x - 1000, pos_police_y + 1200, PK_ARMOR, 100);
}

void fx_burst(uint8_t kind, int32_t x, int32_t y, int n) {
  int k, i;
  for (k = 0; k < n; k++) {
    for (i = 0; i < MAX_FX; i++) {
      if (!fx_list[i].used) {
        Particle *f = &fx_list[i];
        uint8_t a = (uint8_t)(oop_rnd() & 255u);
        int32_t spd = (kind == FX_EXPLOSION) ? (70 + (int32_t)(oop_rnd() & 127u)) : (25 + (int32_t)(oop_rnd() & 63u));
        f->used = 1;
        f->kind = kind;
        f->x = x;
        f->y = y;
        f->vx = (int16_t)((sin256(a) * spd) >> 8);
        f->vy = (int16_t)((-cos256(a) * spd) >> 8);
        f->max_life = (kind == FX_EXPLOSION) ? 20 : (kind == FX_SMOKE ? 24 : 14);
        f->life = f->max_life;
        break;
      }
    }
  }
}

void fx_tick(void) {
  int i;
  for (i = 0; i < MAX_FX; i++) {
    if (!fx_list[i].used) continue;
    fx_list[i].x += fx_list[i].vx;
    fx_list[i].y += fx_list[i].vy;
    fx_list[i].vx = (int16_t)((fx_list[i].vx * 220) >> 8);
    fx_list[i].vy = (int16_t)((fx_list[i].vy * 220) >> 8);
    if (fx_list[i].life > 1) fx_list[i].life--;
    else fx_list[i].used = 0;
  }
}

void pickup_drop(int32_t x, int32_t y, uint8_t type, int16_t amount) {
  int i;
  for (i = 0; i < MAX_PICKUPS; i++) {
    if (!pickups[i].used) {
      pickups[i].used = 1;
      pickups[i].type = type;
      pickups[i].x = x;
      pickups[i].y = y;
      pickups[i].amount = amount;
      return;
    }
  }
}

int give_item(uint8_t pk_type) {
  Ped *pl = &peds[PLAYER];
  switch (pk_type) {
  case PK_PISTOL:
    pl->owned_w |= (1u << W_PISTOL);
    pl->ammo[W_PISTOL] += 36;
    if (pl->clip_ammo[W_PISTOL] == 0) pl->clip_ammo[W_PISTOL] = weapon_specs[W_PISTOL].clip;
    pl->weapon = W_PISTOL;
    return 1;
  case PK_SMG:
    pl->owned_w |= (1u << W_SMG);
    pl->ammo[W_SMG] += 90;
    if (pl->clip_ammo[W_SMG] == 0) pl->clip_ammo[W_SMG] = weapon_specs[W_SMG].clip;
    pl->weapon = W_SMG;
    return 1;
  case PK_SHOTGUN:
    pl->owned_w |= (1u << W_SHOTGUN);
    pl->ammo[W_SHOTGUN] += 24;
    if (pl->clip_ammo[W_SHOTGUN] == 0) pl->clip_ammo[W_SHOTGUN] = weapon_specs[W_SHOTGUN].clip;
    pl->weapon = W_SHOTGUN;
    return 1;
  case PK_AMMO: {
    int got = 0;
    if (pl->owned_w & (1u << W_PISTOL)) { pl->ammo[W_PISTOL] += 24; got = 1; }
    if (pl->owned_w & (1u << W_SMG)) { pl->ammo[W_SMG] += 60; got = 1; }
    if (pl->owned_w & (1u << W_SHOTGUN)) { pl->ammo[W_SHOTGUN] += 12; got = 1; }
    return got;
  }
  case PK_HEALTH:
    if (pl->health >= 100) return 0;
    pl->health = 100;
    return 1;
  case PK_ARMOR:
    if (pl->armor >= 100) return 0;
    pl->armor = 100;
    return 1;
  default:
    return 0;
  }
}

void fire_weapon(int pi) {
  Ped *p = &peds[pi];
  const WeaponSpec *w = &weapon_specs[p->weapon];
  int k, i;

  if (p->weapon == W_FIST) {
    /* Melee punch */
    int32_t px = p->x + (sin256(p->ang) * 2);
    int32_t py = p->y - (cos256(p->ang) * 2);
    p->fire_cd = w->fire_cd;
    audio_sfx(SFX_PUNCH);
    for (i = 0; i < MAX_PEDS; i++) {
      if (i == pi || !peds[i].used || peds[i].state == PS_DEAD || peds[i].car >= 0) continue;
      if (dist_fp(px, py, peds[i].x, peds[i].y) < w->range_fp) {
        ped_damage(i, w->dmg, sin256(p->ang), -cos256(p->ang), pi);
        break;
      }
    }
    return;
  }

  /* Firearms */
  if (p->reload_t > 0) return;
  if (p->clip_ammo[p->weapon] <= 0) {
    if (p->ammo[p->weapon] > 0) {
      p->reload_t = w->reload_t;
      audio_sfx(SFX_RELOAD);
    } else if (pi == PLAYER) {
      audio_sfx(SFX_EMPTY);
      p->fire_cd = 12;
    }
    return;
  }

  p->clip_ammo[p->weapon]--;
  p->fire_cd = w->fire_cd;

  if (p->weapon == W_PISTOL) audio_sfx(SFX_PISTOL);
  else if (p->weapon == W_SMG) audio_sfx(SFX_SMG);
  else audio_sfx(SFX_SHOTGUN);

  fx_burst(FX_MUZZLE, p->x + sin256(p->ang), p->y - cos256(p->ang), 2);
  ped_alarm(p->x, p->y, 12000);
  if (pi == PLAYER) {
    crime(CR_SHOTS, p->x, p->y);
    if (p->weapon == W_SHOTGUN) cam_shake = 4;
  }

  for (k = 0; k < w->pellets; k++) {
    for (i = 0; i < MAX_BULLETS; i++) {
      if (!bullets[i].used) {
        int jitter = w->spread ? ((int)(oop_rnd() % (uint32_t)(w->spread * 2 + 1)) - w->spread) : 0;
        uint8_t a = (uint8_t)((p->ang + jitter) & 255);
        bullets[i].used = 1;
        bullets[i].owner = (int8_t)pi;
        bullets[i].x = p->x + sin256(a);
        bullets[i].y = p->y - cos256(a);
        bullets[i].vx = (sin256(a) * 460) >> 8;
        bullets[i].vy = (-cos256(a) * 460) >> 8;
        bullets[i].dmg = w->dmg;
        bullets[i].life = (int16_t)(w->range_fp / 420);
        if (bullets[i].life < 4) bullets[i].life = 4;
        break;
      }
    }
  }
}

static void robbery_tick(const GameInput *in) {
  Ped *pl = &peds[PLAYER];
  if (rob_cd > 0) rob_cd--;
  if (pl->car >= 0 || game_mode != MODE_PLAY) {
    rob_active = 0;
    return;
  }
  int near_l = dist_fp(pl->x, pl->y, pos_laundry_x, pos_laundry_y) < 1500;
  if (!near_l) {
    rob_active = 0;
    return;
  }
  if (!rob_active && rob_cd <= 0 && (in->held & IN_FIRE)) {
    rob_active = 1;
    rob_prog = 0;
    rob_cash = 0;
    hud_msg("HOLD-UP! KEEP AIMING!", 50);
    crime(CR_ROBBERY, pl->x, pl->y);
  }
  if (rob_active) {
    rob_prog += 2;
    if ((rob_prog % 20) == 0) {
      rob_cash += 80;
      cash += 80;
      audio_sfx(SFX_CASH);
    }
    if (rob_prog >= 100) {
      rob_active = 0;
      rob_cd = 600; /* 20s cooldown */
      hud_msg("REGISTER EMPTY! RUN!", 60);
    }
  }
}

void combat_tick(const GameInput *in) {
  int i, j;
  Ped *pl = &peds[PLAYER];

  if (pl->fire_cd > 0) pl->fire_cd--;

  /* Reload completion */
  if (pl->reload_t > 0) {
    pl->reload_t--;
    if (pl->reload_t == 0) {
      const WeaponSpec *w = &weapon_specs[pl->weapon];
      int16_t need = w->clip - pl->clip_ammo[pl->weapon];
      int16_t take = pl->ammo[pl->weapon] < need ? pl->ammo[pl->weapon] : need;
      pl->clip_ammo[pl->weapon] += take;
      pl->ammo[pl->weapon] -= take;
    }
  }

  /* Weapon switching (when not in Pawn Kings menu) */
  if (!shop_open && pl->car < 0) {
    if (in->hit & IN_NEXT) {
      for (i = 1; i <= W_COUNT; i++) {
        uint8_t nw = (uint8_t)((pl->weapon + i) % W_COUNT);
        if (pl->owned_w & (1u << nw)) {
          pl->weapon = nw;
          pl->reload_t = 0;
          audio_sfx(SFX_UI);
          break;
        }
      }
    }
    if (in->hit & IN_PREV) {
      for (i = 1; i <= W_COUNT; i++) {
        uint8_t nw = (uint8_t)((pl->weapon + W_COUNT - i) % W_COUNT);
        if (pl->owned_w & (1u << nw)) {
          pl->weapon = nw;
          pl->reload_t = 0;
          audio_sfx(SFX_UI);
          break;
        }
      }
    }
    if ((in->hit & IN_RELOAD) && pl->weapon != W_FIST && pl->reload_t == 0) {
      const WeaponSpec *w = &weapon_specs[pl->weapon];
      if (pl->clip_ammo[pl->weapon] < w->clip && pl->ammo[pl->weapon] > 0) {
        pl->reload_t = w->reload_t;
        audio_sfx(SFX_RELOAD);
      }
    }
    if ((in->held & IN_FIRE) && pl->fire_cd <= 0) {
      fire_weapon(PLAYER);
    }
  } else if (pl->car >= 0 && (in->held & IN_FIRE) && pl->fire_cd <= 0 && pl->weapon != W_FIST) {
    /* Drive-by shooting! */
    fire_weapon(PLAYER);
  }

  /* Update bullets */
  for (i = 0; i < MAX_BULLETS; i++) {
    Bullet *b = &bullets[i];
    int hit = 0;
    if (!b->used) continue;
    b->x += b->vx;
    b->y += b->vy;
    if (--b->life <= 0 || world_solid(b->x, b->y)) {
      fx_burst(FX_SPARK, b->x, b->y, 2);
      b->used = 0;
      continue;
    }

    /* Hit peds */
    for (j = 0; j < MAX_PEDS; j++) {
      if (j == b->owner || !peds[j].used || peds[j].state == PS_DEAD || peds[j].car >= 0) continue;
      if (dist_fp(b->x, b->y, peds[j].x, peds[j].y) < 260) {
        ped_damage(j, b->dmg, b->vx >> 2, b->vy >> 2, b->owner);
        hit = 1;
        break;
      }
    }
    if (hit) {
      b->used = 0;
      continue;
    }

    /* Hit cars */
    for (j = 0; j < MAX_CARS; j++) {
      if (!cars[j].used) continue;
      if (cars[j].driver == b->owner) continue;
      if (dist_fp(b->x, b->y, cars[j].x, cars[j].y) < car_specs[cars[j].model].half_w + 100) {
        car_damage(j, b->dmg * 2, b->owner);
        fx_burst(FX_SPARK, b->x, b->y, 3);
        b->used = 0;
        break;
      }
    }
  }

  /* Collect pickups */
  for (i = 0; i < MAX_PICKUPS; i++) {
    Pickup *pk = &pickups[i];
    if (!pk->used) continue;
    if (dist_fp(pl->x, pl->y, pk->x, pk->y) < 420) {
      if (pk->type == PK_CASH) {
        cash += pk->amount;
        audio_sfx(SFX_CASH);
        pk->used = 0;
      } else if (give_item(pk->type)) {
        audio_sfx(SFX_PICKUP);
        pk->used = 0;
      }
    }
  }

  robbery_tick(in);
  fx_tick();
}
