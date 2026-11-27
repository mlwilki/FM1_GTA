/* SPDX-License-Identifier: GPL-3.0-only */
/* missions.c: 5 Walt story missions (Day Three, Dale's Wagon, Spin Cycle, Export Quality, Ray Runs) + HUD helpers */
#include "oop.h"

Marker markers[MAX_MARKERS];
char hud_msg_buf[64];
int16_t hud_msg_t = 0;
char hud_big_buf[32];
int16_t hud_big_t = 0;
char hud_obj_buf[48];

int16_t mission_cur = 0;
static int8_t m_step = 0; /* 0 = waiting at Walt's lot, 1 = active */
static int8_t m_car = -1;
static uint8_t m_went = 0;
static uint8_t m_robbed = 0;

static const char *const M_TITLE[NMISSION] = {
    "DAY THREE",
    "DALE'S WAGON",
    "SPIN CYCLE",
    "EXPORT QUALITY",
    "RAY RUNS",
};
static const int16_t M_PAY[NMISSION] = {300, 450, 600, 800, 1200};

void marker_set(int slot, int32_t x, int32_t y, int8_t kind) {
  if (slot < 0 || slot >= MAX_MARKERS) return;
  markers[slot].kind = kind;
  markers[slot].x = x;
  markers[slot].y = y;
}

void hud_msg(const char *s, int16_t ticks) {
  str_copy(hud_msg_buf, s, sizeof(hud_msg_buf));
  hud_msg_t = ticks;
}

void hud_big(const char *s, int16_t ticks) {
  str_copy(hud_big_buf, s, sizeof(hud_big_buf));
  hud_big_t = ticks;
}

void hud_objective(const char *s) { str_copy(hud_obj_buf, s, sizeof(hud_obj_buf)); }

void missions_init(void) {
  int i;
  for (i = 0; i < MAX_MARKERS; i++) markers[i].kind = MK_NONE;
  m_step = 0;
  m_car = -1;
  m_went = 0;
  m_robbed = 0;
  hud_msg_buf[0] = 0;
  hud_msg_t = 0;
  hud_big_buf[0] = 0;
  hud_big_t = 0;
  hud_obj_buf[0] = 0;
}

static void mission_cleanup(void) {
  if (m_car >= 0 && m_car < MAX_CARS && cars[m_car].used) {
    cars[m_car].flags &= ~CF_MISSION;
  }
  m_car = -1;
  marker_set(SL_TARGET, 0, 0, MK_NONE);
  hud_objective("");
}

static void mission_fail(const char *why) {
  hud_msg(why, 90);
  hud_big("JOB BLOWN", 75);
  mission_cleanup();
  m_step = 0;
}

static void mission_finish(const char *line) {
  char m[48];
  cash += M_PAY[mission_cur];
  str_copy(m, "+$", sizeof(m));
  str_cat_int(m, M_PAY[mission_cur], sizeof(m));
  str_cat(m, " ", sizeof(m));
  str_cat(m, line, sizeof(m));
  hud_msg(m, 95);
  hud_big("JOB DONE", 75);
  audio_sfx(SFX_CASH);
  mission_cur++;
  m_step = 0;
  mission_cleanup();
}

static void mission_begin(void) {
  m_step = 1;
  m_went = 0;
  m_robbed = 0;
  m_car = -1;
  marker_set(SL_MISSION, 0, 0, MK_NONE);
  hud_big(M_TITLE[mission_cur], 65);

  switch (mission_cur) {
  case 0: /* DAY THREE */
    m_car = (int8_t)car_spawn(CAR_PICKUP, pos_walt_x + 1200, pos_walt_y - 800, 128, AI_PARKED, RGB(210, 205, 190));
    if (m_car >= 0) cars[m_car].flags |= CF_MISSION;
    hud_msg("WALT: TAKE MY PICKUP TO HARBOR ROW.", 90);
    break;
  case 1: /* DALE'S WAGON */
    m_car = (int8_t)car_spawn(CAR_WAGON, pos_dale_x, pos_dale_y, 64, AI_PARKED, RGB(52, 128, 70));
    if (m_car >= 0) cars[m_car].flags |= CF_MISSION;
    hud_msg("WALT: STEAL DALE'S GREEN WAGON.", 90);
    break;
  case 2: /* SPIN CYCLE */
    hud_msg("WALT: ROB THE LAUNDROMAT ON MARKET ROW.", 90);
    break;
  case 3: /* EXPORT QUALITY */
    m_car = (int8_t)car_spawn(CAR_SUV, pos_suv_x, pos_suv_y, 0, AI_PARKED, RGB(210, 42, 42));
    if (m_car >= 0) cars[m_car].flags |= CF_MISSION;
    hud_msg("WALT: SHIP NEEDS A RED SUV AT THE DOCKS.", 90);
    break;
  default: /* RAY RUNS */
    m_car = (int8_t)car_spawn(CAR_HATCH, pos_walt_x + 4096, pos_walt_y + 4096, 64, AI_FLEE, RGB(44, 92, 215));
    if (m_car >= 0) {
      cars[m_car].flags |= CF_MISSION;
      int di = ped_spawn(PED_CIV, cars[m_car].x, cars[m_car].y, 64);
      if (di >= 0) {
        peds[di].car = m_car;
        peds[di].state = PS_IN_CAR;
        cars[m_car].driver = (int8_t)di;
      }
    }
    hud_msg("WALT: STOP RAY'S BLUE HATCHBACK!", 90);
    break;
  }
}

static int car_delivery_step(int32_t dest_x, int32_t dest_y, const char *get_in, const char *go) {
  if (m_car < 0 || !cars[m_car].used || cars[m_car].ai == AI_WRECK) {
    mission_fail("WRECKED IT. WALT WANTED IT WHOLE.");
    return 0;
  }
  if (peds[PLAYER].car != m_car) {
    hud_objective(m_went ? "GET BACK IN THE CAR" : get_in);
    marker_set(SL_TARGET, cars[m_car].x, cars[m_car].y, MK_TARGET);
    return 0;
  }
  m_went = 1;
  hud_objective(go);
  marker_set(SL_TARGET, dest_x, dest_y, MK_TARGET);
  if (dist_fp(cars[m_car].x, cars[m_car].y, dest_x, dest_y) < 2000 && cars[m_car].speed < 360 && cars[m_car].speed > -360) {
    return 1;
  }
  return 0;
}

void missions_tick(void) {
  Ped *pl = &peds[PLAYER];

  if (hud_msg_t > 0) hud_msg_t--;
  if (hud_big_t > 0) hud_big_t--;

  if (game_mode != MODE_PLAY) return;

  if (mission_cur >= NMISSION) {
    marker_set(SL_MISSION, 0, 0, MK_NONE);
    if (!m_step) hud_objective("FREE ROAM: JOBS, STASHES, CHAOS");
    return;
  }

  if (m_step == 0) {
    marker_set(SL_MISSION, pos_walt_x, pos_walt_y, MK_MISSION);
    hud_objective("VISIT WALT (W) FOR NEXT JOB");
    if (dist_fp(pl->x, pl->y, pos_walt_x, pos_walt_y) < 1200 && heat == 0) {
      mission_begin();
    }
    return;
  }

  /* Active mission */
  switch (mission_cur) {
  case 0:
    if (car_delivery_step(pos_harbor_x, pos_harbor_y, "GET IN WALT'S PICKUP", "DRIVE PICKUP TO HARBOR ROW")) {
      mission_finish("YOUR HALF FROM WALT.");
    }
    break;
  case 1:
    if (car_delivery_step(pos_walt_x, pos_walt_y, "FIND DALE'S GREEN WAGON", "RETURN WAGON TO WALT'S")) {
      mission_finish("DALE OWES LESS NOW.");
    }
    break;
  case 2:
    if (!m_robbed) {
      marker_set(SL_TARGET, pos_laundry_x, pos_laundry_y, MK_TARGET);
      hud_objective("ROB LAUNDROMAT (HOLD FIRE INSIDE)");
      if (rob_active && rob_prog >= 60) {
        m_robbed = 1;
        hud_msg("GOT THE RENT! NOW LOSE THE COPS!", 70);
      }
    } else {
      marker_set(SL_TARGET, 0, 0, MK_NONE);
      hud_objective("LOSE YOUR WANTED LEVEL");
      if (heat == 0) {
        mission_finish("LAUNDROMAT PAID UP.");
      }
    }
    break;
  case 3:
    if (car_delivery_step(pos_export_x, pos_export_y, "FIND THE RED SUV", "DELIVER SUV TO THE DOCKS")) {
      mission_finish("SHIPPED OUT CLEAN.");
    }
    break;
  default: /* RAY RUNS */
    if (m_car < 0 || !cars[m_car].used) {
      mission_fail("RAY GOT AWAY.");
      return;
    }
    marker_set(SL_TARGET, cars[m_car].x, cars[m_car].y, MK_TARGET);
    hud_objective("STOP OR WRECK RAY'S HATCHBACK");
    if (cars[m_car].ai == AI_WRECK || cars[m_car].health < 260 ||
        (peds[PLAYER].car == m_car)) {
      mission_finish("WALT GOT HIS ENVELOPE.");
    }
    break;
  }
}
