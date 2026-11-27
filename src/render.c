/* SPDX-License-Identifier: GPL-3.0-only */
/* render.c: 240x240 RGB565 2.5D top-down renderer, rotated vehicles, HUD, map, title, shop */
#include "oop.h"

int32_t cam_x = 0, cam_y = 0;
int16_t cam_zoom = 256; /* 256 = 32 px/tile, 192 = 24 px/tile */
int16_t cam_shake = 0;

/* Darken an RGB565 color by factor (0..256) */
static uint16_t shade565(uint16_t c, int factor) {
  int r = ((c >> 11) & 31) * factor >> 8;
  int g = ((c >> 5) & 63) * factor >> 8;
  int b = (c & 31) * factor >> 8;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

static int w2sx(int32_t wx) { return 120 + (int)(((wx - cam_x) * cam_zoom) >> 14); }
static int w2sy(int32_t wy) { return 120 + (int)(((wy - cam_y) * cam_zoom) >> 14); }

static void draw_filled_quad(int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, uint16_t col) {
  int xs[4] = {x0, x1, x2, x3};
  int ys[4] = {y0, y1, y2, y3};
  int min_y = ys[0], max_y = ys[0], i, y;
  for (i = 1; i < 4; i++) {
    if (ys[i] < min_y) min_y = ys[i];
    if (ys[i] > max_y) max_y = ys[i];
  }
  if (min_y < 22) min_y = 22;
  if (max_y > 215) max_y = 215;
  for (y = min_y; y <= max_y; y++) {
    int lx = 999, rx = -999;
    for (i = 0; i < 4; i++) {
      int j = (i + 1) & 3;
      int ya = ys[i], yb = ys[j];
      int xa = xs[i], xb = xs[j];
      if ((ya <= y && yb > y) || (yb <= y && ya > y)) {
        int x = xa + ((y - ya) * (xb - xa)) / (yb - ya);
        if (x < lx) lx = x;
        if (x > rx) rx = x;
      }
    }
    if (lx <= rx) {
      if (lx < 0) lx = 0;
      if (rx >= SCREEN_W) rx = SCREEN_W - 1;
      if (lx <= rx) rect(lx, y, rx - lx + 1, 1, col);
    }
  }
}

static const uint16_t BLDG_ROOFS[8] = {
    RGB(96, 84, 78),  RGB(76, 86, 98),  RGB(112, 82, 72), RGB(84, 92, 84),
    RGB(104, 94, 80), RGB(70, 74, 86),  RGB(116, 98, 86), RGB(82, 78, 94),
};

static void draw_ground(int night_f) {
  int tile_px = (TILE_FP * cam_zoom) >> 14;
  if (tile_px < 12) tile_px = 12;
  int half_t = (120 / tile_px) + 2;
  int ctx = cam_x >> 11;
  int cty = cam_y >> 11;
  int tx, ty;

  uint16_t c_water = shade565(RGB(26, 78, 132), night_f);
  uint16_t c_wave = shade565(RGB(46, 112, 168), night_f);
  uint16_t c_sand = shade565(RGB(194, 174, 126), night_f);
  uint16_t c_grass = shade565(RGB(54, 108, 56), night_f);
  uint16_t c_side = shade565(RGB(132, 132, 136), night_f);
  uint16_t c_conc = shade565(RGB(96, 98, 104), night_f);
  uint16_t c_road = shade565(RGB(48, 50, 56), night_f);
  uint16_t c_line = shade565(RGB(218, 184, 56), night_f);
  uint16_t c_zebra = shade565(RGB(190, 190, 196), night_f);

  int l_ns = world_light_ns();
  int l_ew = world_light_ew();

  for (ty = cty - half_t; ty <= cty + half_t; ty++) {
    for (tx = ctx - half_t; tx <= ctx + half_t; tx++) {
      int sx = w2sx(tx << 11);
      int sy = w2sy(ty << 11);
      if (sx + tile_px < 0 || sx >= SCREEN_W || sy + tile_px < 22 || sy >= 216) continue;
      uint8_t t = (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) ? T_WATER : tile_map[ty][tx];

      uint16_t col = c_conc;
      if (t == T_WATER) col = c_water;
      else if (t == T_SAND) col = c_sand;
      else if (t == T_GRASS) col = c_grass;
      else if (t == T_SIDEWALK) col = c_side;
      else if (t == T_ROAD_H || t == T_ROAD_V || t == T_ROAD_X) col = c_road;
      else if (t >= T_BLDG_1) col = shade565(RGB(40, 42, 48), night_f);

      rect(sx, sy, tile_px + 1, tile_px + 1, col);

      if (t == T_WATER) {
        int wx = sx + (((tx * 7 + (int)(game_ticks >> 2)) & 15) * tile_px >> 4);
        rect(wx, sy + (tile_px >> 1), 4, 1, c_wave);
      } else if (t == T_ROAD_H) {
        rect(sx + (tile_px >> 2), sy + (tile_px >> 1), tile_px >> 1, 2, c_line);
      } else if (t == T_ROAD_V) {
        rect(sx + (tile_px >> 1), sy + (tile_px >> 2), 2, tile_px >> 1, c_line);
      } else if (t == T_ROAD_X) {
        /* Crosswalk bars + traffic light indicator dots */
        rect(sx + 2, sy + 2, 4, 2, c_zebra);
        rect(sx + tile_px - 6, sy + tile_px - 4, 4, 2, c_zebra);
        uint16_t ns_c = (l_ns == 0) ? RGB(40, 230, 80) : (l_ns == 1 ? RGB(240, 200, 30) : RGB(240, 40, 40));
        uint16_t ew_c = (l_ew == 0) ? RGB(40, 230, 80) : (l_ew == 1 ? RGB(240, 200, 30) : RGB(240, 40, 40));
        rect(sx + 1, sy + 1, 3, 3, ns_c);
        rect(sx + tile_px - 4, sy + 1, 3, 3, ew_c);
      }
    }
  }
}

static void draw_buildings_25d(int night_f, int night_lvl) {
  int tile_px = (TILE_FP * cam_zoom) >> 14;
  if (tile_px < 12) tile_px = 12;
  int half_t = (120 / tile_px) + 2;
  int ctx = cam_x >> 11;
  int cty = cam_y >> 11;
  int tx, ty;

  for (ty = cty - half_t; ty <= cty + half_t; ty++) {
    for (tx = ctx - half_t; tx <= ctx + half_t; tx++) {
      uint8_t t;
      if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) continue;
      t = tile_map[ty][tx];
      if (t < T_BLDG_1) continue;

      int sx = w2sx(tx << 11);
      int sy = w2sy(ty << 11);
      if (sx + tile_px < -16 || sx >= SCREEN_W + 16 || sy + tile_px < 10 || sy >= 230) continue;

      int h_lvl = (t == T_BLDG_3) ? 3 : (t == T_BLDG_2 ? 2 : 1);
      int cx = sx + (tile_px >> 1);
      int cy = sy + (tile_px >> 1);
      /* Subtle 2.5D roof shift away from screen center */
      int ox = ((cx - 120) * h_lvl) >> 4;
      int oy = ((cy - 120) * h_lvl) >> 4;
      if (ox > 10) ox = 10;
      if (ox < -10) ox = -10;
      if (oy > 10) oy = 10;
      if (oy < -10) oy = -10;

      uint16_t roof = BLDG_ROOFS[bldg_var[ty][tx] & 7u];
      if (t == T_SHOP) roof = RGB(168, 92, 46);
      else if (t == T_GARAGE) roof = RGB(56, 128, 152);
      else if (t == T_HOSP) roof = RGB(205, 208, 214);
      else if (t == T_POLICE) roof = RGB(48, 72, 138);

      uint16_t wall = shade565(roof, (night_f * 150) >> 8);
      uint16_t roof_s = shade565(roof, night_f);
      uint16_t rim = shade565(roof, (night_f * 210) >> 8);

      /* Side wall base */
      rect(sx + (ox > 0 ? 0 : ox), sy + (oy > 0 ? 0 : oy), tile_px + (ox < 0 ? -ox : ox),
           tile_px + (oy < 0 ? -oy : oy), wall);

      /* Roof top */
      int rx = sx + ox, ry = sy + oy;
      rect(rx, ry, tile_px, tile_px, roof_s);
      frame(rx, ry, tile_px, tile_px, rim);

      /* Special landmark roof markings */
      if (t == T_HOSP && tx == 18 && ty == 10) {
        rect(rx + (tile_px >> 1) - 2, ry + 4, 4, tile_px - 8, RGB(220, 40, 40));
        rect(rx + 4, ry + (tile_px >> 1) - 2, tile_px - 8, 4, RGB(220, 40, 40));
      } else if (t == T_POLICE && tx == 42 && ty == 10) {
        rect(rx + 4, ry + 4, tile_px - 8, 4, RGB(40, 120, 255));
      } else if (t == T_GARAGE && tx == 41 && ty == 18) {
        rect(rx + 3, ry + 3, tile_px - 6, 4, RGB(60, 220, 240));
      } else if (night_lvl > 90 && (bldg_var[ty][tx] & 1u)) {
        /* Night-lit windows along wall edge */
        rect(rx + 4, ry + 4, 4, 4, RGB(245, 215, 110));
        if (tile_px > 20) rect(rx + tile_px - 8, ry + tile_px - 8, 4, 4, RGB(245, 215, 110));
      }
    }
  }
}

static void draw_cars(int night_f) {
  int i;
  for (i = 0; i < MAX_CARS; i++) {
    Car *c = &cars[i];
    if (!c->used) continue;
    int sx = w2sx(c->x);
    int sy = w2sy(c->y);
    if (sx < -24 || sx > SCREEN_W + 24 || sy < 0 || sy > 240) continue;

    const CarSpec *sp = &car_specs[c->model];
    int ang = (c->ang16 >> 4) & 255;
    int32_t fx = sin256(ang), fy = -cos256(ang);
    int32_t rx = cos256(ang), ry = sin256(ang);

    int hw = (int)((sp->half_w * cam_zoom) >> 14);
    int hl = (int)((sp->half_l * cam_zoom) >> 14);
    if (hw < 4) hw = 4;
    if (hl < 7) hl = 7;

    int flx = sx + ((fx * hl - rx * hw) >> 8), fly = sy + ((fy * hl - ry * hw) >> 8);
    int frx = sx + ((fx * hl + rx * hw) >> 8), fry = sy + ((fy * hl + ry * hw) >> 8);
    int brx = sx + ((-fx * hl + rx * hw) >> 8), bry = sy + ((-fy * hl + ry * hw) >> 8);
    int blx = sx + ((-fx * hl - rx * hw) >> 8), bly = sy + ((-fy * hl - ry * hw) >> 8);

    /* Shadow */
    draw_filled_quad(flx + 2, fly + 2, frx + 2, fry + 2, brx + 2, bry + 2, blx + 2, bly + 2, RGB(20, 20, 24));
    /* Body */
    uint16_t body = shade565(c->paint, night_f);
    draw_filled_quad(flx, fly, frx, fry, brx, bry, blx, bly, body);

    /* Windshield / Cabin */
    int ch = (hl * 3) >> 3;
    int cw = (hw * 3) >> 2;
    int cflx = sx + ((fx * ch - rx * cw) >> 8), cfly = sy + ((fy * ch - ry * cw) >> 8);
    int cfrx = sx + ((fx * ch + rx * cw) >> 8), cfry = sy + ((fy * ch + ry * cw) >> 8);
    int cbrx = sx + ((-fx * ch + rx * cw) >> 8), cbry = sy + ((-fy * ch + ry * cw) >> 8);
    int cblx = sx + ((-fx * ch - rx * cw) >> 8), cbly = sy + ((-fy * ch - ry * cw) >> 8);
    draw_filled_quad(cflx, cfly, cfrx, cfry, cbrx, cbry, cblx, cbly,
                     c->model == CAR_COP ? RGB(230, 234, 242) : RGB(44, 62, 82));

    /* Headlights at night */
    if (c->flags & CF_LIGHTS) {
      int lx = sx + ((fx * (hl + 10) - rx * (hw >> 1)) >> 8);
      int ly = sy + ((fy * (hl + 10) - ry * (hw >> 1)) >> 8);
      int rx2 = sx + ((fx * (hl + 10) + rx * (hw >> 1)) >> 8);
      int ry2 = sy + ((fy * (hl + 10) + ry * (hw >> 1)) >> 8);
      line(flx, fly, lx, ly, RGB(250, 245, 170));
      line(frx, fry, rx2, ry2, RGB(250, 245, 170));
    }

    /* Cop flashing red/blue lightbar */
    if (c->model == CAR_COP) {
      uint16_t lc = ((game_ticks >> 2) & 1u) ? RGB(255, 36, 36) : RGB(36, 128, 255);
      uint16_t rc = ((game_ticks >> 2) & 1u) ? RGB(36, 128, 255) : RGB(255, 36, 36);
      if (c->flags & CF_SIREN) {
        rect(sx - 3, sy - 1, 3, 3, lc);
        rect(sx + 1, sy - 1, 3, 3, rc);
      } else {
        rect(sx - 2, sy - 1, 4, 2, RGB(80, 120, 220));
      }
    } else if (c->model == CAR_TAXI) {
      rect(sx - 2, sy - 1, 4, 2, RGB(255, 240, 120));
    }
  }
}

static void draw_peds(int night_f) {
  int i;
  for (i = MAX_PEDS - 1; i >= 0; i--) {
    Ped *p = &peds[i];
    if (!p->used || p->car >= 0) continue;
    int sx = w2sx(p->x);
    int sy = w2sy(p->y);
    if (sx < -8 || sx > SCREEN_W + 8 || sy < 18 || sy > 220) continue;

    if (p->state == PS_DEAD) {
      rect(sx - 3, sy - 2, 7, 4, RGB(140, 24, 24));
      rect(sx - 2, sy - 1, 4, 2, shade565(p->shirt, night_f));
      continue;
    }

    /* Shoulders & head */
    rect(sx - 3, sy - 2, 7, 5, shade565(p->shirt, night_f));
    rect(sx - 2, sy - 2, 4, 4, shade565(p->skin, night_f));
    if (p->kind == PED_COP) {
      rect(sx - 2, sy - 3, 4, 2, RGB(26, 48, 120));
    } else if (i == PLAYER) {
      /* Subtle white highlight ring on player */
      pixel(sx, sy - 4, RGB(255, 230, 80));
    }

    /* Aim direction / weapon barrel */
    int bx = sx + ((sin256(p->ang) * 6) >> 8);
    int by = sy - ((cos256(p->ang) * 6) >> 8);
    if (p->weapon != W_FIST) {
      line(sx, sy, bx, by, RGB(220, 224, 230));
    } else if (i == PLAYER) {
      pixel(bx, by, RGB(255, 220, 120));
    }
  }
}

static void draw_markers_and_fx(void) {
  int i;
  /* Skids */
  for (i = 0; i < MAX_SKIDS; i++) {
    if (!skids[i].used) continue;
    int sx = w2sx(skids[i].x), sy = w2sy(skids[i].y);
    if (sx >= 0 && sx < SCREEN_W && sy >= 22 && sy < 216) rect(sx - 1, sy - 1, 2, 2, RGB(28, 28, 32));
  }

  /* Pickups */
  for (i = 0; i < MAX_PICKUPS; i++) {
    if (!pickups[i].used) continue;
    int sx = w2sx(pickups[i].x), sy = w2sy(pickups[i].y);
    if (sx < 0 || sx >= SCREEN_W || sy < 22 || sy >= 216) continue;
    uint16_t c = RGB(60, 235, 90);
    if (pickups[i].type == PK_HEALTH) c = RGB(245, 70, 70);
    else if (pickups[i].type == PK_ARMOR) c = RGB(70, 170, 250);
    else if (pickups[i].type == PK_AMMO) c = RGB(245, 200, 60);
    int bob = ((game_ticks >> 3) & 1u) ? -1 : 0;
    rect(sx - 2, sy - 2 + bob, 5, 5, c);
    frame(sx - 3, sy - 3 + bob, 7, 7, WHITE);
  }

  /* Bullets */
  for (i = 0; i < MAX_BULLETS; i++) {
    if (!bullets[i].used) continue;
    int sx = w2sx(bullets[i].x), sy = w2sy(bullets[i].y);
    int px = w2sx(bullets[i].x - bullets[i].vx), py = w2sy(bullets[i].y - bullets[i].vy);
    if (sx >= 0 && sx < SCREEN_W && sy >= 22 && sy < 216) {
      line(px, py, sx, sy, RGB(255, 240, 120));
    }
  }

  /* Particles */
  for (i = 0; i < MAX_FX; i++) {
    Particle *f = &fx_list[i];
    if (!f->used) continue;
    int sx = w2sx(f->x), sy = w2sy(f->y);
    if (sx < 0 || sx >= SCREEN_W || sy < 22 || sy >= 216) continue;
    uint16_t c = RGB(255, 220, 80);
    int sz = 2;
    if (f->kind == FX_BLOOD) c = RGB(200, 24, 24);
    else if (f->kind == FX_SMOKE) { c = RGB(120, 124, 130); sz = 3; }
    else if (f->kind == FX_FIRE) { c = RGB(255, 110, 24); sz = 3; }
    else if (f->kind == FX_EXPLOSION) { c = (f->life > 10) ? RGB(255, 230, 90) : RGB(240, 80, 20); sz = 5; }
    rect(sx - (sz >> 1), sy - (sz >> 1), sz, sz, c);
  }

  /* World beacons & edge arrows for markers */
  for (i = 0; i < MAX_MARKERS; i++) {
    if (markers[i].kind == MK_NONE) continue;
    uint16_t mc = RGB(255, 210, 40);
    if (markers[i].kind == MK_MISSION) mc = RGB(255, 160, 30);
    else if (markers[i].kind == MK_TARGET) mc = RGB(255, 56, 86);
    else if (markers[i].kind == MK_SHOP) mc = RGB(70, 230, 110);
    else if (markers[i].kind == MK_RESPRAY) mc = RGB(56, 205, 250);
    else if (markers[i].kind == MK_EXPORT) mc = RGB(190, 110, 255);
    else if (markers[i].kind == MK_STASH) mc = RGB(240, 235, 100);

    int sx = w2sx(markers[i].x);
    int sy = w2sy(markers[i].y);
    int pulse = (int)((game_ticks >> 2) & 3u);
    if (sx >= 6 && sx <= 234 && sy >= 26 && sy <= 210) {
      frame(sx - 6 - pulse, sy - 6 - pulse, 12 + pulse * 2, 12 + pulse * 2, mc);
    } else if (markers[i].kind == MK_MISSION || markers[i].kind == MK_TARGET) {
      /* Clamp edge arrow */
      int ax = sx < 8 ? 8 : (sx > 231 ? 231 : sx);
      int ay = sy < 28 ? 28 : (sy > 209 ? 209 : sy);
      rect(ax - 3, ay - 3, 7, 7, mc);
      frame(ax - 4, ay - 4, 9, 9, WHITE);
    }
  }

  /* Police Helicopter */
  if (heli_active) {
    int hx = w2sx(heli_x), hy = w2sy(heli_y);
    if (hx >= -30 && hx <= 270 && hy >= 0 && hy <= 240) {
      /* Spotlight cone */
      int px = w2sx(peds[PLAYER].x), py = w2sy(peds[PLAYER].y);
      frame(px - 14, py - 14, 28, 28, RGB(180, 230, 255));
      /* Body & spinning rotor */
      rect(hx - 6, hy - 4, 12, 8, RGB(34, 42, 58));
      int ra = (int)((game_ticks * 27u) & 255u);
      int rx = (sin256(ra) * 16) >> 8, ry = (-cos256(ra) * 16) >> 8;
      line(hx - rx, hy - ry, hx + rx, hy + ry, RGB(210, 220, 235));
      line(hx - ry, hy + rx, hx + ry, hy - rx, RGB(140, 155, 175));
    }
  }
}

static void draw_hud(void) {
  Ped *pl = &peds[PLAYER];
  int i;
  char buf[32];

  /* Top HUD bar */
  rect(0, 0, SCREEN_W, 22, RGB(14, 17, 24));
  line(0, 22, SCREEN_W - 1, 22, RGB(52, 62, 78));

  /* Cash */
  str_copy(buf, "$", sizeof(buf));
  str_cat_int(buf, cash, sizeof(buf));
  text(4, 3, buf, RGB(90, 238, 120), FONT_SMALL);

  /* Health & Armor bars */
  rect(74, 4, 46, 6, RGB(48, 22, 24));
  rect(74, 4, (pl->health * 46) / 100, 6, RGB(235, 58, 66));
  rect(74, 12, 46, 5, RGB(22, 34, 52));
  if (pl->armor > 0) rect(74, 12, (pl->armor * 46) / 100, 5, RGB(56, 184, 248));

  /* Wanted stars (top-right) */
  for (i = 0; i < 5; i++) {
    int sx = 182 + i * 11;
    uint16_t sc = RGB(44, 48, 58);
    if (i < heat) {
      sc = ((game_ticks >> 2) & 1u) ? RGB(255, 210, 40) : RGB(255, 70, 70);
    }
    rect(sx, 6, 8, 8, sc);
  }

  /* Clock (HH:MM) */
  {
    int hh = day_min / 60;
    int mm = day_min % 60;
    buf[0] = (char)('0' + (hh / 10));
    buf[1] = (char)('0' + (hh % 10));
    buf[2] = ':';
    buf[3] = (char)('0' + (mm / 10));
    buf[4] = (char)('0' + (mm % 10));
    buf[5] = 0;
    text(130, 3, buf, RGB(150, 165, 188), FONT_SMALL);
  }

  /* Robbery progress bar */
  if (rob_active) {
    rect(40, 30, 160, 14, RGB(20, 24, 32));
    frame(40, 30, 160, 14, RGB(255, 200, 40));
    rect(42, 32, (rob_prog * 156) / 100, 10, RGB(240, 180, 36));
    text(84, 29, "ROBBERY", BLACK, FONT_SMALL);
  }

  /* Pawn Kings Shop Menu */
  if (shop_open) {
    rect(92, 42, 142, 114, RGB(16, 20, 30));
    frame(92, 42, 142, 114, RGB(70, 230, 110));
    text(100, 46, "PAWN KINGS", RGB(70, 230, 110), FONT_SMALL);
    for (i = 0; i < shop_item_count(); i++) {
      int y = 64 + i * 16;
      if (i == shop_sel) rect(95, y, 136, 15, RGB(36, 62, 52));
      text(98, y, shop_item_name(i), i == shop_sel ? WHITE : RGB(180, 190, 204), FONT_SMALL);
      str_copy(buf, "$", sizeof(buf));
      str_cat_int(buf, shop_item_price(i), sizeof(buf));
      text(192, y, buf, RGB(110, 235, 130), FONT_SMALL);
    }
    text(96, 142, "FX/SEL  REC:BUY", RGB(130, 145, 165), FONT_SMALL);
  }

  /* Message ticker above bottom bar */
  if (hud_msg_t > 0 && hud_msg_buf[0]) {
    rect(0, 198, SCREEN_W, 18, RGB(18, 22, 32));
    text(4, 199, hud_msg_buf, RGB(250, 225, 120), FONT_SMALL);
  }

  /* Big center banner (JOB DONE, WASTED, BUSTED, mission title) */
  if (hud_big_t > 0 && hud_big_buf[0]) {
    int tw = text_width(hud_big_buf, FONT_SMALL);
    int bx = (SCREEN_W - tw) >> 1;
    rect(bx - 12, 96, tw + 24, 26, RGB(12, 14, 20));
    frame(bx - 12, 96, tw + 24, 26, RGB(56, 189, 248));
    text(bx, 101, hud_big_buf, WHITE, FONT_SMALL);
  }

  /* Bottom bar: Weapon/Ammo or Car + Radio + Objective */
  rect(0, 216, SCREEN_W, 24, RGB(14, 17, 24));
  line(0, 216, SCREEN_W - 1, 216, RGB(52, 62, 78));

  if (pl->car >= 0) {
    Car *c = &cars[pl->car];
    str_copy(buf, car_specs[c->model].name, sizeof(buf));
    text(4, 220, buf, RGB(56, 189, 248), FONT_SMALL);
    text(84, 220, radio_name(radio_station), RGB(210, 140, 255), FONT_SMALL);
  } else {
    str_copy(buf, weapon_specs[pl->weapon].name, sizeof(buf));
    if (pl->weapon != W_FIST) {
      str_cat(buf, " ", sizeof(buf));
      str_cat_int(buf, pl->clip_ammo[pl->weapon], sizeof(buf));
      str_cat(buf, "/", sizeof(buf));
      str_cat_int(buf, pl->ammo[pl->weapon], sizeof(buf));
    }
    text(4, 220, pl->reload_t > 0 ? "RELOADING..." : buf, RGB(230, 236, 245), FONT_SMALL);
  }
  if (hud_obj_buf[0] && pl->car < 0 && !shop_open) {
    /* Short objective hint on right side when space allows, else show in map/pause */
    int ox = 132;
    if (pl->weapon == W_FIST) ox = 56;
    char short_o[24];
    str_copy(short_o, hud_obj_buf, sizeof(short_o));
    text(ox, 220, short_o, RGB(248, 198, 66), FONT_SMALL);
  }
}

static void draw_map_screen(void) {
  int tx, ty, i;
  clear(RGB(12, 16, 24));
  text(64, 4, "PAROLE CITY MAP", RGB(56, 189, 248), FONT_SMALL);

  for (ty = 0; ty < MAP_H; ty++) {
    for (tx = 0; tx < MAP_W; tx++) {
      uint8_t t = tile_map[ty][tx];
      uint16_t c = RGB(44, 86, 48);
      if (t == T_WATER) c = RGB(22, 64, 118);
      else if (t == T_SAND) c = RGB(176, 158, 110);
      else if (t == T_ROAD_H || t == T_ROAD_V || t == T_ROAD_X) c = RGB(74, 78, 86);
      else if (t == T_SIDEWALK || t == T_CONCRETE) c = RGB(104, 108, 116);
      else if (t == T_HOSP) c = RGB(230, 70, 70);
      else if (t == T_POLICE) c = RGB(60, 120, 240);
      else if (t == T_GARAGE) c = RGB(56, 205, 250);
      else if (t == T_SHOP) c = RGB(70, 230, 110);
      else if (t >= T_BLDG_1) c = RGB(58, 54, 62);
      rect(24 + tx * 3, 24 + ty * 3, 3, 3, c);
    }
  }
  frame(23, 23, MAP_W * 3 + 2, MAP_H * 3 + 2, RGB(70, 82, 100));

  /* Markers */
  for (i = 0; i < MAX_MARKERS; i++) {
    if (markers[i].kind == MK_NONE) continue;
    int mx = 24 + (markers[i].x >> 11) * 3;
    int my = 24 + (markers[i].y >> 11) * 3;
    uint16_t mc = RGB(255, 210, 40);
    if (markers[i].kind == MK_TARGET) mc = RGB(255, 50, 80);
    else if (markers[i].kind == MK_SHOP) mc = RGB(70, 230, 110);
    else if (markers[i].kind == MK_RESPRAY) mc = RGB(56, 205, 250);
    else if (markers[i].kind == MK_EXPORT) mc = RGB(190, 110, 255);
    rect(mx - 1, my - 1, 5, 5, mc);
  }

  /* Player blinking dot */
  if ((game_ticks >> 3) & 1u) {
    int px = 24 + (peds[PLAYER].x >> 11) * 3;
    int py = 24 + (peds[PLAYER].y >> 11) * 3;
    rect(px - 2, py - 2, 5, 5, WHITE);
  }

  text(8, 220, hud_obj_buf[0] ? hud_obj_buf : "HOME: CLOSE MAP", RGB(248, 198, 66), FONT_SMALL);
}

static void draw_pause_screen(void) {
  char buf[40];
  int st = 0, i;
  for (i = 0; i < NSTASH; i++)
    if (stashes_mask & (1u << i)) st++;

  rect(20, 32, 200, 176, RGB(14, 18, 28));
  frame(20, 32, 200, 176, RGB(56, 189, 248));
  text(88, 40, "PAUSED", WHITE, FONT_SMALL);

  str_copy(buf, "MISSIONS: ", sizeof(buf));
  str_cat_int(buf, mission_cur, sizeof(buf));
  str_cat(buf, "/5", sizeof(buf));
  text(34, 66, buf, RGB(248, 198, 66), FONT_SMALL);

  str_copy(buf, "STASHES:  ", sizeof(buf));
  str_cat_int(buf, st, sizeof(buf));
  str_cat(buf, "/16", sizeof(buf));
  text(34, 84, buf, RGB(110, 235, 130), FONT_SMALL);

  str_copy(buf, "RADIO:    ", sizeof(buf));
  str_cat(buf, radio_name(radio_station), sizeof(buf));
  text(34, 102, buf, RGB(210, 140, 255), FONT_SMALL);

  text(34, 126, "PLAY:FIRE  REC:CAR/BUY", RGB(170, 182, 200), FONT_SMALL);
  text(34, 144, "ARP:DRIFT  SEQ:RLD/HRN", RGB(170, 182, 200), FONT_SMALL);
  text(34, 162, "HOME:MAP   GLO:RADIO", RGB(170, 182, 200), FONT_SMALL);
  text(34, 184, "EDIT:RESUME  SAVE:SAVE", RGB(56, 189, 248), FONT_SMALL);
}

static void draw_title_screen(void) {
  int i;
  clear(RGB(12, 14, 24));

  /* Synthwave horizon bands */
  rect(0, 118, SCREEN_W, 24, RGB(46, 20, 62));
  rect(0, 142, SCREEN_W, 22, RGB(88, 28, 72));
  rect(0, 164, SCREEN_W, 20, RGB(154, 48, 68));
  rect(0, 184, SCREEN_W, 56, RGB(16, 22, 34));

  /* Neon sun */
  rect(96, 116, 48, 42, RGB(248, 178, 44));
  rect(96, 134, 48, 3, RGB(88, 28, 72));
  rect(96, 144, 48, 4, RGB(154, 48, 68));
  rect(96, 154, 48, 4, RGB(154, 48, 68));

  /* City skyline silhouette */
  static const uint8_t SKY_H[15] = {28, 46, 34, 62, 42, 74, 52, 38, 68, 48, 58, 34, 50, 40, 30};
  for (i = 0; i < 15; i++) {
    int x = i * 16;
    int h = SKY_H[i];
    rect(x, 184 - h, 15, h, RGB(14, 16, 26));
    if ((i & 1) == 0) rect(x + 4, 184 - h + 8, 3, 3, RGB(248, 220, 110));
    if ((i % 3) == 0) rect(x + 9, 184 - h + 16, 3, 3, RGB(56, 205, 250));
  }

  /* Title text */
  text(32, 24, "OUT ON", RGB(248, 192, 48), FONT_BIG);
  text(32, 58, "PAROLE", RGB(56, 205, 250), FONT_BIG);
  text(46, 94, "M-VAVE FM-1 EDITION", RGB(200, 208, 224), FONT_SMALL);

  if ((game_ticks >> 4) & 1u) {
    text(38, 198, "PRESS PLAY OR SPACE", WHITE, FONT_SMALL);
  }
  text(34, 218, "PORT OF OUTONPAROLE", RGB(118, 130, 150), FONT_SMALL);
}

/* ---- 3D-style perspective cutscene renderer (pure integer projection into cinema band y=36..203) */
static void proj3d(int32_t x, int32_t y, int32_t z, int *sx, int *sy) {
  if (z < 64) z = 64;
  *sx = 120 + (int)((x * 200) / z);
  *sy = 122 - (int)((y * 200) / z);
}

static void draw_quad_any(int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, uint16_t col) {
  int xs[4] = {x0, x1, x2, x3};
  int ys[4] = {y0, y1, y2, y3};
  int min_y = ys[0], max_y = ys[0], i, y;
  for (i = 1; i < 4; i++) {
    if (ys[i] < min_y) min_y = ys[i];
    if (ys[i] > max_y) max_y = ys[i];
  }
  if (min_y < 36) min_y = 36;
  if (max_y > 203) max_y = 203;
  for (y = min_y; y <= max_y; y++) {
    int lx = 999, rx = -999;
    for (i = 0; i < 4; i++) {
      int j = (i + 1) & 3;
      int ya = ys[i], yb = ys[j];
      int xa = xs[i], xb = xs[j];
      if ((ya <= y && yb > y) || (yb <= y && ya > y)) {
        int x = xa + ((y - ya) * (xb - xa)) / (yb - ya);
        if (x < lx) lx = x;
        if (x > rx) rx = x;
      }
    }
    if (lx <= rx) {
      if (lx < 0) lx = 0;
      if (rx >= SCREEN_W) rx = SCREEN_W - 1;
      if (lx <= rx) rect(lx, y, rx - lx + 1, 1, col);
    }
  }
}

/* Draw a 3D perspective street backdrop with vanishing road lines and skyline */
static void draw_street_3d(int strobe_red, int strobe_blue) {
  int i;
  uint16_t sky_top = strobe_red ? RGB(72, 18, 28) : (strobe_blue ? RGB(18, 32, 82) : RGB(24, 30, 52));
  uint16_t sky_bot = strobe_red ? RGB(112, 34, 46) : (strobe_blue ? RGB(34, 58, 128) : RGB(56, 48, 78));
  uint16_t road_c = RGB(42, 44, 52);
  uint16_t side_c = RGB(92, 94, 102);

  /* Cinema letterbox bars */
  rect(0, 0, SCREEN_W, 36, BLACK);
  rect(0, 204, SCREEN_W, 36, BLACK);

  /* Sky + Horizon */
  rect(0, 36, SCREEN_W, 44, sky_top);
  rect(0, 80, SCREEN_W, 36, sky_bot);

  /* Distant skyline silhouettes */
  static const uint8_t BG_H[12] = {22, 36, 26, 42, 30, 20, 38, 46, 28, 34, 24, 32};
  for (i = 0; i < 12; i++) {
    int h = BG_H[i];
    rect(i * 20, 116 - h, 18, h, RGB(20, 24, 36));
    if ((i & 1) == 0) rect(i * 20 + 5, 116 - h + 6, 3, 3, RGB(240, 210, 110));
  }

  /* Sidewalk & Asphalt perspective */
  rect(0, 116, SCREEN_W, 14, side_c);
  rect(0, 130, SCREEN_W, 74, road_c);
  /* Perspective curb & dashed center line */
  line(0, 130, 239, 130, RGB(150, 152, 160));
  for (i = 0; i < 6; i++) {
    int x = i * 44 - ((cut_t * 2) & 31);
    rect(x, 166, 22, 3, RGB(215, 185, 58));
  }
}

/* Draw an articulated 3D-projected person at (x, y_ground, z), pose: 0=stand, 1=pull/lunge, 2=tumble, 3=kneel_handsup, 4=cuffed */
static void draw_person_3d(int32_t px, int32_t py, int32_t pz, uint16_t shirt, uint16_t pants, uint16_t skin, int pose, int is_cop) {
  int hx, hy, sx, sy, bx, by, lx, ly, rx, ry;
  int scale = (int)((160 * 120) / (pz > 80 ? pz : 80));
  int body_w = scale >> 3;
  if (body_w < 4) body_w = 4;

  if (pose == 2) {
    /* Tumbling backward on the asphalt! */
    proj3d(px, py + 12, pz, &bx, &by);
    proj3d(px + 24, py + 18, pz, &hx, &hy);
    rect(bx - body_w, by - 4, body_w * 2, 8, pants);
    rect(hx - body_w, hy - 5, body_w * 2, 9, shirt);
    rect(hx + body_w - 2, hy - 7, 7, 7, skin);
    /* Flailing arm */
    line(hx, hy, hx + 8, hy - 14, shirt);
    return;
  }

  int hip_y = (pose == 3 || pose == 4) ? (py + 16) : (py + 34);
  int sh_y = hip_y + 28;
  int hd_y = sh_y + 12;

  proj3d(px, hd_y, pz, &hx, &hy);
  proj3d(px, sh_y, pz, &sx, &sy);
  proj3d(px, hip_y, pz, &bx, &by);
  proj3d(px - 8, py, pz, &lx, &ly);
  proj3d(px + 8, py, pz, &rx, &ry);

  /* Legs */
  if (pose == 3 || pose == 4) {
    /* Kneeling */
    rect(bx - body_w, by, body_w * 2, 9, pants);
  } else {
    line(bx - 3, by, lx, ly, pants);
    line(bx - 2, by, lx + 1, ly, pants);
    line(bx + 3, by, rx, ry, pants);
    line(bx + 2, by, rx - 1, ry, pants);
  }

  /* Torso */
  rect(sx - body_w, sy, body_w * 2 + 1, by - sy + 2, shirt);

  /* Head (+ cop cap) */
  int hr = body_w - 1;
  if (hr < 3) hr = 3;
  rect(hx - hr, hy - hr, hr * 2 + 1, hr * 2 + 1, skin);
  if (is_cop) {
    rect(hx - hr - 1, hy - hr - 3, hr * 2 + 3, 4, RGB(24, 42, 118));
    rect(hx - 1, hy - hr - 2, 3, 2, RGB(248, 210, 50)); /* badge */
  }

  /* Arms */
  if (pose == 1) {
    /* Lunging / pulling door or driver */
    line(sx, sy + 3, sx - 16, sy + 1, shirt);
    line(sx, sy + 4, sx - 16, sy + 2, shirt);
    rect(sx - 18, sy, 4, 4, skin);
  } else if (pose == 3) {
    /* Hands raised high! */
    line(sx - body_w, sy + 3, sx - body_w - 7, sy - 14, shirt);
    line(sx + body_w, sy + 3, sx + body_w + 7, sy - 14, shirt);
    rect(sx - body_w - 9, sy - 17, 4, 4, skin);
    rect(sx + body_w + 6, sy - 17, 4, 4, skin);
  } else if (pose == 4) {
    /* Cuffed behind back */
    line(sx - body_w, sy + 4, sx - body_w - 2, by - 2, shirt);
    line(sx + body_w, sy + 4, sx + body_w + 2, by - 2, shirt);
    rect(sx - 3, by - 4, 7, 3, RGB(205, 215, 230)); /* silver handcuffs */
  } else {
    line(sx - body_w, sy + 3, sx - body_w - 4, by, shirt);
    line(sx + body_w, sy + 3, sx + body_w + 4, by, shirt);
  }
}

static void draw_cutscene_carjack(void) {
  int t = cut_t;
  draw_street_3d(0, 0);

  /* 3D Side-perspective car in center-left */
  uint16_t paint = cut_car_paint;
  uint16_t dark = shade565(paint, 155);
  uint16_t roof_c = shade565(paint, 225);
  int x0, y0, x1, y1, x2, y2, x3, y3;

  /* Car lower body (3D box side) */
  proj3d(-115, 16, 210, &x0, &y0);
  proj3d(95, 16, 210, &x1, &y1);
  proj3d(95, 52, 210, &x2, &y2);
  proj3d(-115, 52, 210, &x3, &y3);
  draw_quad_any(x0, y0, x1, y1, x2, y2, x3, y3, paint);

  /* Car hood/roof top slope (3D depth) */
  int rx0, ry0, rx1, ry1, rx2, ry2, rx3, ry3;
  proj3d(-65, 52, 210, &rx0, &ry0);
  proj3d(48, 52, 210, &rx1, &ry1);
  proj3d(38, 82, 245, &rx2, &ry2);
  proj3d(-52, 82, 245, &rx3, &ry3);
  draw_quad_any(rx0, ry0, rx1, ry1, rx2, ry2, rx3, ry3, RGB(46, 68, 92)); /* side window */
  line(rx3, ry3, rx2, ry2, roof_c);

  /* Wheels */
  int wl, wy, wr;
  proj3d(-72, 16, 205, &wl, &wy);
  proj3d(58, 16, 205, &wr, &wy);
  rect(wl - 11, wy - 8, 22, 20, RGB(24, 24, 28));
  rect(wl - 5, wy - 2, 10, 8, RGB(155, 162, 174));
  rect(wr - 11, wy - 8, 22, 20, RGB(24, 24, 28));
  rect(wr - 5, wy - 2, 10, 8, RGB(155, 162, 174));

  /* Swinging 3D Driver Door! */
  int door_open = 0;
  if (t >= 6 && t < 30) {
    door_open = (t < 12) ? (t - 6) * 8 : (t > 24 ? (30 - t) * 8 : 48);
  }
  int dx0, dy0, dx1, dy1, dx2, dy2, dx3, dy3;
  proj3d(-44, 18, 205, &dx0, &dy0);
  proj3d(-44 + (44 - (door_open >> 1)), 18, 205 - door_open, &dx1, &dy1);
  proj3d(-44 + (44 - (door_open >> 1)), 54, 205 - door_open, &dx2, &dy2);
  proj3d(-44, 54, 205, &dx3, &dy3);
  draw_quad_any(dx0, dy0, dx1, dy1, dx2, dy2, dx3, dy3, dark);
  line(dx0, dy0, dx3, dy3, WHITE);

  /* Victim driver yanked out & tumbling! */
  if (t >= 10) {
    int vt = t - 10;
    int32_t vx = -10 + vt * 5;
    int32_t vz = 225 - vt * 2;
    if (vz < 165) vz = 165;
    draw_person_3d(vx, -28, vz, cut_ped_shirt, RGB(44, 54, 76), RGB(224, 182, 148), vt > 5 ? 2 : 0, 0);
  }

  /* Player lunging, pulling, then hopping in */
  if (t < 27) {
    int32_t px = (t < 8) ? (40 - t * 6) : (t < 20 ? -8 : -8 - (t - 20) * 3);
    int32_t pz = (t < 20) ? 210 : 210 + (t - 20) * 5;
    draw_person_3d(px, -28, pz, RGB(240, 148, 38), RGB(38, 46, 64), RGB(235, 194, 160), (t >= 7 && t < 20) ? 1 : 0, 0);
  }

  /* Cinema caption */
  text(76, 10, "CARJACKING!", RGB(248, 192, 48), FONT_SMALL);
  if (t >= 12 && t < 32) {
    text(20, 214, "\"HEY! GET OUT OF MY CAR!\"", WHITE, FONT_SMALL);
  } else if (t >= 32) {
    text(52, 214, "FLOOR IT!  [PLAY]", RGB(56, 205, 250), FONT_SMALL);
  }
}

static void draw_cutscene_busted(void) {
  int t = cut_t;
  int flash = (t >> 2) & 1;
  draw_street_3d(flash, !flash);

  /* 3D Police Cruiser in background */
  int x0, y0, x1, y1, x2, y2, x3, y3;
  proj3d(-135, -24, 275, &x0, &y0);
  proj3d(35, -24, 275, &x1, &y1);
  proj3d(35, 14, 275, &x2, &y2);
  proj3d(-135, 14, 275, &x3, &y3);
  draw_quad_any(x0, y0, x1, y1, x2, y2, x3, y3, RGB(30, 34, 46));
  /* White police door */
  proj3d(-85, -20, 272, &x0, &y0);
  proj3d(-15, -20, 272, &x1, &y1);
  proj3d(-15, 12, 272, &x2, &y2);
  proj3d(-85, 12, 272, &x3, &y3);
  draw_quad_any(x0, y0, x1, y1, x2, y2, x3, y3, RGB(230, 234, 242));

  /* Flashing 3D Lightbar + Beams */
  int lx, ly;
  proj3d(-50, 36, 275, &lx, &ly);
  uint16_t lc = flash ? RGB(255, 36, 36) : RGB(36, 128, 255);
  uint16_t rc = flash ? RGB(36, 128, 255) : RGB(255, 36, 36);
  rect(lx - 14, ly - 5, 14, 7, lc);
  rect(lx + 1, ly - 5, 14, 7, rc);
  line(lx - 10, ly - 4, lx - 48, 42, lc);
  line(lx + 10, ly - 4, lx + 48, 42, rc);

  /* Player dropping to knees with hands up -> cuffed at t >= 42 */
  int pl_pose = (t < 10) ? 0 : (t < 42 ? 3 : 4);
  draw_person_3d(26, -30, 215, RGB(240, 148, 38), RGB(38, 46, 64), RGB(235, 194, 160), pl_pose, 0);

  /* Officer walking in to arrest player */
  int32_t cop_x = (t < 38) ? (125 - t * 2) : 52;
  draw_person_3d(cop_x, -30, 205, RGB(34, 64, 152), RGB(24, 34, 76), RGB(215, 170, 134), (t >= 38 && t < 50) ? 1 : 0, 1);

  /* Handcuff sparkle at t=42..50 */
  if (t >= 42 && t <= 50) {
    int cx, cy;
    proj3d(26, -6, 205, &cx, &cy);
    line(cx - 8, cy, cx + 8, cy, WHITE);
    line(cx, cy - 8, cx, cy + 8, WHITE);
  }

  /* Dramatic BUSTED slam banner (upper band so cruiser & arrest stay visible!) */
  if (t >= 48) {
    rect(46, 44, 148, 40, RGB(12, 14, 22));
    frame(46, 44, 148, 40, flash ? RGB(255, 48, 56) : RGB(56, 156, 255));
    text(72, 48, "BUSTED", flash ? RGB(255, 64, 64) : RGB(64, 168, 255), FONT_BIG);
  }

  text(56, 10, "PAROLE VIOLATION", flash ? RGB(255, 76, 76) : RGB(76, 168, 255), FONT_SMALL);
  if (t < 38) text(18, 214, "\"FREEZE! HANDS IN THE AIR!\"", WHITE, FONT_SMALL);
  else text(16, 214, "\"BACK TO PRECINCT! -$100\"", RGB(248, 192, 48), FONT_SMALL);
}

void render_frame(void) {
  if (game_mode == MODE_TITLE) {
    draw_title_screen();
    return;
  }
  if (cut_kind == CUT_CARJACK) {
    draw_cutscene_carjack();
    return;
  }
  if (cut_kind == CUT_BUSTED) {
    draw_cutscene_busted();
    return;
  }
  if (game_mode == MODE_MAP) {
    draw_map_screen();
    return;
  }

  /* Smooth camera follow + user zoom preset + speed zoom */
  static const int16_t ZOOM_BASE[4] = {336, 256, 192, 136};
  Ped *pl = &peds[PLAYER];
  int32_t tx = pl->x, ty = pl->y;
  int16_t want_z = ZOOM_BASE[zoom_level & 3u];
  if (pl->car >= 0) {
    Car *c = &cars[pl->car];
    int32_t spd = c->speed < 0 ? -c->speed : c->speed;
    tx += c->vx * 2;
    ty += c->vy * 2;
    want_z = (int16_t)(want_z - (spd >> 5));
    if (want_z < 112) want_z = 112;
  }
  cam_x += (tx - cam_x) >> 2;
  cam_y += (ty - cam_y) >> 2;
  cam_zoom += (want_z - cam_zoom) >> 3;

  if (cam_shake > 0) {
    cam_x += ((int32_t)(oop_rnd() & 127u) - 64) * cam_shake;
    cam_y += ((int32_t)(oop_rnd() & 127u) - 64) * cam_shake;
    cam_shake--;
  }

  int night_lvl = world_night_level();
  int night_f = 256 - ((night_lvl * 118) >> 8); /* 256 day -> 138 midnight */

  draw_ground(night_f);
  draw_markers_and_fx();
  draw_peds(night_f);
  draw_cars(night_f);
  draw_buildings_25d(night_f, night_lvl);
  draw_hud();

  if (game_mode == MODE_PAUSE) {
    draw_pause_screen();
  }
}
