/* SPDX-License-Identifier: GPL-3.0-only */
/* world.c: fixed-point math, deterministic 64x64 coastal city, traffic lights, collision */
#include "oop.h"

/* ---- quarter-wave sine LUT (0..64 -> 0..256) */
static const uint16_t SIN_Q[65] = {
    0,   6,   13,  19,  25,  31,  38,  44,  50,  56,  62,  68,  74,  80,  86,  92,  98,
    104, 109, 115, 121, 126, 132, 137, 142, 147, 152, 157, 162, 167, 172, 177, 181, 185,
    190, 194, 198, 202, 206, 209, 213, 216, 220, 223, 226, 229, 231, 234, 237, 239, 241,
    243, 245, 247, 248, 250, 251, 252, 253, 254, 255, 255, 256, 256, 256,
};

int32_t sin256(int32_t a) {
  a &= 255;
  if (a < 64) return (int32_t)SIN_Q[a];
  if (a < 128) return (int32_t)SIN_Q[128 - a];
  if (a < 192) return -(int32_t)SIN_Q[a - 128];
  return -(int32_t)SIN_Q[256 - a];
}

int32_t cos256(int32_t a) { return sin256(a + 64); }

int32_t isqrt32(uint32_t v) {
  uint32_t r = 0, b = 1u << 30;
  while (b > v) b >>= 2;
  while (b) {
    if (v >= r + b) {
      v -= r + b;
      r = (r >> 1) + b;
    } else {
      r >>= 1;
    }
    b >>= 2;
  }
  return (int32_t)r;
}

/* Returns angle 0..255 where 0 = North (0,-1), 64 = East (1,0), 128 = South (0,1), 192 = West (-1,0) */
uint8_t atan2_256(int32_t dx, int32_t dy) {
  /* Convert (dx, -dy) to standard octant atan2 */
  int32_t x = -dy; /* North is +x in angle-0 frame */
  int32_t y = dx;  /* East is +y */
  int32_t ax = x < 0 ? -x : x;
  int32_t ay = y < 0 ? -y : y;
  int32_t a;
  if (ax == 0 && ay == 0) return 0;
  if (ax >= ay) {
    a = (ay * 32) / (ax ? ax : 1);
  } else {
    a = 64 - (ax * 32) / (ay ? ay : 1);
  }
  if (x < 0 && y >= 0) a = 128 - a;
  else if (x < 0 && y < 0) a = 128 + a;
  else if (x >= 0 && y < 0) a = 256 - a;
  return (uint8_t)(a & 255);
}

int32_t dist_fp(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
  int32_t dx = (x1 - x0) >> 4;
  int32_t dy = (y1 - y0) >> 4;
  if (dx < 0) dx = -dx;
  if (dy < 0) dy = -dy;
  if (dx > 30000 || dy > 30000) return 0x7FFFFFFF;
  return isqrt32((uint32_t)(dx * dx + dy * dy)) << 4;
}

int32_t ang_diff(int32_t a, int32_t b) {
  int32_t d = (a - b) & 255;
  if (d >= 128) d -= 256;
  return d;
}

static uint32_t rng_s = 0x51A7C0DEu;
uint32_t oop_rnd(void) {
  rng_s ^= rng_s << 13;
  rng_s ^= rng_s >> 17;
  rng_s ^= rng_s << 5;
  return rng_s;
}

void str_copy(char *dst, const char *src, int max) {
  int i = 0;
  if (max <= 0) return;
  while (src[i] && i < max - 1) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = 0;
}

void str_cat(char *dst, const char *src, int max) {
  int i = 0, j = 0;
  while (i < max - 1 && dst[i]) i++;
  while (i < max - 1 && src[j]) dst[i++] = src[j++];
  dst[i] = 0;
}

void str_cat_int(char *dst, int32_t v, int max) {
  char b[16];
  format_int(b, v);
  str_cat(dst, b, max);
}

/* ---- world state */
uint8_t tile_map[MAP_H][MAP_W];
uint8_t bldg_var[MAP_H][MAP_W];
uint16_t day_min = 540; /* 09:00 */
static uint32_t light_ticks = 0;

int32_t pos_walt_x, pos_walt_y;
int32_t pos_shop_x, pos_shop_y;
int32_t pos_respray_x, pos_respray_y;
int32_t pos_export_x, pos_export_y;
int32_t pos_hosp_x, pos_hosp_y;
int32_t pos_police_x, pos_police_y;
int32_t pos_laundry_x, pos_laundry_y;
int32_t pos_harbor_x, pos_harbor_y;
int32_t pos_dale_x, pos_dale_y;
int32_t pos_suv_x, pos_suv_y;

static int32_t tc(int t) { return t * TILE_FP + (TILE_FP >> 1); }

static uint32_t hash2(int x, int y) {
  uint32_t h = (uint32_t)(x * 374761393 + y * 668265263);
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

static int is_road_col(int x) {
  return x == 6 || x == 14 || x == 22 || x == 30 || x == 38 || x == 46 || x == 54;
}

static int is_road_row(int y) {
  return y == 6 || y == 14 || y == 22 || y == 30 || y == 38 || y == 46;
}

void world_init(void) {
  int x, y;
  day_min = 540;
  light_ticks = 0;

  for (y = 0; y < MAP_H; y++) {
    for (x = 0; x < MAP_W; x++) {
      uint8_t t = T_GRASS;
      uint32_t h = hash2(x, y);
      bldg_var[y][x] = (uint8_t)(h & 7u);

      /* Outer water border & southern Harbor coastline */
      if (x <= 2 || x >= MAP_W - 3 || y <= 2 || y >= 55) {
        t = T_WATER;
      } else if (y >= 52 && x < 24) {
        t = T_WATER;
      } else if (y >= 50 && y <= 54 && x >= 24 && x <= 44) {
        /* Docks concrete pier */
        t = T_CONCRETE;
      } else if (y == 53 || y == 54 || (y == 51 && x < 24) || (y >= 52 && x > 44)) {
        t = T_SAND;
      } else {
        int rc = is_road_col(x) && y >= 6 && y <= 50;
        int rr = is_road_row(y) && x >= 6 && x <= 54;
        if (rc && rr) {
          t = T_ROAD_X;
        } else if (rr) {
          t = T_ROAD_H;
        } else if (rc) {
          t = T_ROAD_V;
        } else if (x >= 5 && x <= 55 && y >= 5 && y <= 49) {
          /* Inside city blocks: sidewalks adjacent to roads, buildings inside */
          int near_r = is_road_col(x - 1) || is_road_col(x + 1) || is_road_row(y - 1) || is_road_row(y + 1);
          if (near_r) {
            t = T_SIDEWALK;
          } else {
            /* Central park block at (24..28, 16..20) */
            if (x >= 24 && x <= 28 && y >= 16 && y <= 20) {
              t = T_GRASS;
            } else if ((h & 15u) == 0) {
              /* Alleyway / parking lot gap */
              t = T_CONCRETE;
            } else {
              /* Downtown taller buildings in the middle */
              if (x >= 24 && x <= 44 && y >= 24 && y <= 36) {
                t = (h & 3u) == 0 ? T_BLDG_3 : T_BLDG_2;
              } else {
                t = (h & 3u) == 0 ? T_BLDG_2 : T_BLDG_1;
              }
            }
          }
        } else {
          t = T_GRASS;
        }
      }
      tile_map[y][x] = t;
    }
  }

  /* Clear Walt's Lot (west commercial yard) at x=9..12, y=24..28 */
  for (y = 24; y <= 28; y++) {
    for (x = 9; x <= 12; x++) tile_map[y][x] = T_CONCRETE;
  }
  tile_map[24][9] = T_BLDG_1;
  tile_map[24][10] = T_BLDG_1;

  /* Hospital at (17..20, 9..11) */
  for (y = 9; y <= 11; y++)
    for (x = 17; x <= 20; x++) tile_map[y][x] = T_HOSP;
  tile_map[12][18] = T_CONCRETE;
  tile_map[12][19] = T_CONCRETE;

  /* Police Station at (41..44, 9..11) */
  for (y = 9; y <= 11; y++)
    for (x = 41; x <= 44; x++) tile_map[y][x] = T_POLICE;
  tile_map[12][42] = T_CONCRETE;
  tile_map[12][43] = T_CONCRETE;

  /* Respray Garage at (40..43, 17..19) with open bay at y=20..21 */
  for (y = 17; y <= 19; y++)
    for (x = 40; x <= 43; x++) tile_map[y][x] = T_GARAGE;
  tile_map[20][41] = T_CONCRETE;
  tile_map[20][42] = T_CONCRETE;
  tile_map[21][41] = T_CONCRETE;
  tile_map[21][42] = T_CONCRETE;

  /* Pawn Kings Gun Shop at (48..50, 25..27) facing x=46 road */
  for (y = 25; y <= 27; y++)
    for (x = 48; x <= 50; x++) tile_map[y][x] = T_SHOP;
  tile_map[26][47] = T_SIDEWALK;

  /* Market Row Laundromat at (25..28, 25..27) facing y=30 road */
  for (y = 25; y <= 27; y++)
    for (x = 25; x <= 28; x++) tile_map[y][x] = T_SHOP;
  tile_map[28][26] = T_CONCRETE;
  tile_map[28][27] = T_CONCRETE;

  /* Harbor Row loading yard at (15..20, 47..49) */
  for (y = 47; y <= 49; y++)
    for (x = 15; x <= 20; x++) tile_map[y][x] = T_CONCRETE;

  /* Landmark coordinates */
  pos_walt_x = tc(12);
  pos_walt_y = tc(26);
  pos_shop_x = tc(47);
  pos_shop_y = tc(26);
  pos_respray_x = tc(41);
  pos_respray_y = tc(21);
  pos_export_x = tc(34);
  pos_export_y = tc(51);
  pos_hosp_x = tc(18);
  pos_hosp_y = tc(13);
  pos_police_x = tc(42);
  pos_police_y = tc(13);
  pos_laundry_x = tc(27);
  pos_laundry_y = tc(29);
  pos_harbor_x = tc(18);
  pos_harbor_y = tc(48);
  pos_dale_x = tc(23);
  pos_dale_y = tc(34);
  pos_suv_x = tc(47);
  pos_suv_y = tc(41);
}

void world_tick(void) {
  light_ticks++;
  /* 1 game minute every 15 ticks (0.5s) -> full 24h cycle in 12 minutes */
  if ((game_ticks & 15u) == 0) {
    day_min = (uint16_t)((day_min + 1u) % 1440u);
  }
}

/* 18-second cycle: 0..240 green, 240..270 yellow, 270..540 red */
int world_light_ns(void) {
  uint32_t c = light_ticks % 540u;
  if (c < 240u) return 0;
  if (c < 270u) return 1;
  return 2;
}

int world_light_ew(void) {
  uint32_t c = (light_ticks + 270u) % 540u;
  if (c < 240u) return 0;
  if (c < 270u) return 1;
  return 2;
}

int world_night_level(void) {
  /* Day: 7:00 (420) .. 18:00 (1080) -> 0
   * Dusk: 18:00..21:00 (1080..1260) -> 0..255
   * Night: 21:00..05:00 -> 255
   * Dawn: 05:00..07:00 (300..420) -> 255..0 */
  if (day_min >= 420 && day_min < 1080) return 0;
  if (day_min >= 1080 && day_min < 1260) return ((day_min - 1080) * 255) / 180;
  if (day_min >= 300 && day_min < 420) return ((420 - day_min) * 255) / 120;
  return 255;
}

uint8_t world_tile(int32_t wx, int32_t wy) {
  int tx = wx >> 11;
  int ty = wy >> 11;
  if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return T_WATER;
  return tile_map[ty][tx];
}

int world_is_road(int tx, int ty) {
  uint8_t t;
  if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return 0;
  t = tile_map[ty][tx];
  return t == T_ROAD_H || t == T_ROAD_V || t == T_ROAD_X;
}

int world_is_water(int32_t wx, int32_t wy) { return world_tile(wx, wy) == T_WATER; }

int world_solid(int32_t wx, int32_t wy) {
  uint8_t t = world_tile(wx, wy);
  return t >= T_BLDG_1 || t == T_WATER;
}

int world_push(int32_t *x, int32_t *y, int32_t rad) {
  int hit = 0;
  int tx0 = (*x - rad) >> 11;
  int ty0 = (*y - rad) >> 11;
  int tx1 = (*x + rad) >> 11;
  int ty1 = (*y + rad) >> 11;
  int tx, ty;
  for (ty = ty0; ty <= ty1; ty++) {
    for (tx = tx0; tx <= tx1; tx++) {
      uint8_t t = (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) ? T_WATER : tile_map[ty][tx];
      if (t < T_BLDG_1 && t != T_WATER) continue;
      int32_t bx0 = tx << 11, by0 = ty << 11;
      int32_t bx1 = bx0 + TILE_FP, by1 = by0 + TILE_FP;
      int32_t cx = *x < bx0 ? bx0 : (*x > bx1 ? bx1 : *x);
      int32_t cy = *y < by0 ? by0 : (*y > by1 ? by1 : *y);
      int32_t dx = *x - cx;
      int32_t dy = *y - cy;
      int32_t d = isqrt32((uint32_t)((dx >> 2) * (dx >> 2) + (dy >> 2) * (dy >> 2))) << 2;
      if (d < rad) {
        hit = 1;
        if (d > 4) {
          int32_t push = rad - d;
          *x += (dx * push) / d;
          *y += (dy * push) / d;
        } else {
          /* Deep inside tile: push out along nearest edge */
          int32_t dl = *x - bx0, dr = bx1 - *x, dt = *y - by0, db = by1 - *y;
          if (dl <= dr && dl <= dt && dl <= db) *x = bx0 - rad;
          else if (dr <= dt && dr <= db) *x = bx1 + rad;
          else if (dt <= db) *y = by0 - rad;
          else *y = by1 + rad;
        }
      }
    }
  }
  return hit;
}

int world_ray(int32_t x0, int32_t y0, int32_t dx, int32_t dy, int32_t max_steps, int32_t *hx, int32_t *hy) {
  int32_t x = x0, y = y0;
  int i;
  for (i = 0; i < max_steps; i++) {
    x += dx;
    y += dy;
    if (world_solid(x, y)) {
      if (hx) *hx = x;
      if (hy) *hy = y;
      return 1;
    }
  }
  if (hx) *hx = x;
  if (hy) *hy = y;
  return 0;
}
