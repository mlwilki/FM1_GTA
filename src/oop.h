/* SPDX-License-Identifier: GPL-3.0-only */
/* Out on Parole — M-VAVE FM-1 Edition (pure integer fixed-point port of shaydex-codes/outonparole) */
#ifndef OOP_H
#define OOP_H

#include "fm1.h"
#include <stdint.h>

/* ---- fixed-point math (Q8: 1 meter = 256 units; angles 0..255, 0 = North/-Y, 64 = East/+X) */
#define FP_SHIFT 8
#define FP_ONE (1 << FP_SHIFT)
#define TILE_M 8
#define TILE_FP (TILE_M << FP_SHIFT) /* 2048 units per tile */
#define MAP_W 64
#define MAP_H 64
#define WORLD_W (MAP_W * TILE_FP)
#define WORLD_H (MAP_H * TILE_FP)

#define TICK_HZ 30
#define MAX_CARS 24
#define MAX_PEDS 36
#define MAX_BULLETS 24
#define MAX_PICKUPS 24
#define MAX_FX 40
#define MAX_SKIDS 48
#define MAX_MARKERS 10
#define NSTASH 16
#define NMISSION 5

/* ---- input bitmask (mirrors outonparole) */
enum {
  IN_FIRE = 1 << 0,
  IN_ENTER = 1 << 1,
  IN_SPRINT = 1 << 2,
  IN_RELOAD = 1 << 3,
  IN_NEXT = 1 << 4,
  IN_PREV = 1 << 5,
  IN_MAP = 1 << 6,
  IN_PAUSE = 1 << 7,
  IN_RADIO = 1 << 8,
  IN_SAVE = 1 << 9,
  IN_ZOOM_IN = 1 << 10,
  IN_ZOOM_OUT = 1 << 11,
};

/* ---- 3D-style cutscenes */
enum { CUT_NONE = 0, CUT_CARJACK, CUT_BUSTED };

typedef struct {
  int16_t mx; /* -256..256: right/steer */
  int16_t my; /* -256..256: forward/gas(+)/brake(-) */
  uint16_t held;
  uint16_t hit;
} GameInput;

/* ---- game modes */
enum { MODE_TITLE, MODE_PLAY, MODE_DEAD, MODE_BUSTED, MODE_PAUSE, MODE_MAP };

/* ---- tile types */
enum {
  T_WATER = 0,
  T_SAND,
  T_GRASS,
  T_SIDEWALK,
  T_ROAD_H,
  T_ROAD_V,
  T_ROAD_X,
  T_CONCRETE,
  T_BLDG_1,
  T_BLDG_2,
  T_BLDG_3,
  T_SHOP,
  T_GARAGE,
  T_HOSP,
  T_POLICE,
};

/* ---- vehicle models (exact 10 from outonparole) */
enum {
  CAR_SEDAN = 0,
  CAR_COMPACT,
  CAR_HATCH,
  CAR_MINIVAN,
  CAR_PICKUP,
  CAR_SUV,
  CAR_WAGON,
  CAR_OFFROAD,
  CAR_COP,
  CAR_TAXI,
  CAR_MODELS
};

enum { AI_NONE = 0, AI_PARKED, AI_TRAFFIC, AI_COP, AI_FLEE, AI_WRECK };

typedef struct {
  const char *name;
  int16_t half_w;    /* Q8 meters */
  int16_t half_l;    /* Q8 meters */
  int16_t max_spd;   /* Q8 m/s */
  int16_t accel;     /* Q8 m/s^2 per tick */
  int16_t turn_rate; /* angle units per tick * 16 */
  int16_t max_hp;
  uint8_t cyls;
  int16_t export_pay;
} CarSpec;

typedef struct {
  uint8_t used;
  uint8_t model;
  uint8_t ai;
  uint8_t flags; /* bit 0: siren, bit 1: horn, bit 2: lights, bit 3: mission_owned */
  int32_t x, y;  /* Q8 world coords */
  int32_t vx, vy;
  int16_t ang16; /* angle << 4 (0..4095) for smooth steering */
  int16_t speed; /* signed forward speed Q8 */
  int16_t health;
  uint16_t paint; /* RGB565 */
  int8_t driver;  /* ped index or -1 */
  int8_t target;  /* ped index or -1 */
  int16_t fire_t;
  int16_t ai_t;
  uint8_t skid; /* 0..255 */
  uint8_t rpm;  /* 0..255 */
} Car;

#define CF_SIREN 1
#define CF_HORN 2
#define CF_LIGHTS 4
#define CF_MISSION 8

/* ---- weapons (exact 4 from outonparole) */
enum { W_FIST = 0, W_PISTOL, W_SMG, W_SHOTGUN, W_COUNT };

typedef struct {
  const char *name;
  int16_t dmg;
  int16_t fire_cd;   /* ticks between shots */
  int16_t range_fp;  /* Q8 meters */
  int16_t clip;
  int16_t reload_t;  /* ticks */
  uint8_t pellets;
  uint8_t spread;    /* angle jitter */
} WeaponSpec;

/* ---- pedestrians */
#define PLAYER 0
enum { PED_CIV = 0, PED_COP, PED_PLAYER };
enum {
  PS_WALK = 0,
  PS_IDLE,
  PS_FLEE,
  PS_FIGHT,
  PS_CHASE,
  PS_DEAD,
  PS_IN_CAR,
  PS_ARREST
};

typedef struct {
  uint8_t used;
  uint8_t kind;
  uint8_t state;
  uint8_t ang;
  int32_t x, y;
  int32_t vx, vy;
  int16_t health;
  int16_t armor;
  int8_t car; /* -1 on foot */
  uint8_t weapon;
  uint8_t owned_w; /* bitmask of unlocked weapons */
  int16_t ammo[W_COUNT];
  int16_t clip_ammo[W_COUNT];
  int16_t fire_cd;
  int16_t reload_t;
  int16_t timer;
  int16_t walk_anim;
  uint16_t shirt;
  uint16_t skin;
  int8_t target;
} Ped;

/* ---- bullets / tracers */
typedef struct {
  uint8_t used;
  int8_t owner;
  int32_t x, y;
  int32_t vx, vy;
  int16_t dmg;
  int16_t life;
} Bullet;

/* ---- pickups */
enum { PK_PISTOL = 0, PK_SMG, PK_SHOTGUN, PK_AMMO, PK_HEALTH, PK_ARMOR, PK_CASH, PK_TYPES };

typedef struct {
  uint8_t used;
  uint8_t type;
  int32_t x, y;
  int16_t amount;
} Pickup;

/* ---- particles / fx */
enum { FX_SPARK = 0, FX_BLOOD, FX_SMOKE, FX_FIRE, FX_EXPLOSION, FX_MUZZLE };

typedef struct {
  uint8_t used;
  uint8_t kind;
  int32_t x, y;
  int16_t vx, vy;
  int16_t life;
  int16_t max_life;
} Particle;

typedef struct {
  uint8_t used;
  int32_t x, y;
  uint8_t life;
} Skid;

/* ---- crimes & wanted */
enum {
  CR_ASSAULT = 0,
  CR_MURDER,
  CR_SHOTS,
  CR_CARJACK,
  CR_HIT_PED,
  CR_COP_ASSAULT,
  CR_COP_MURDER,
  CR_ROBBERY,
  CR_DESTROY_CAR
};

/* ---- markers */
enum { MK_NONE = -1, MK_MISSION = 0, MK_TARGET, MK_SHOP, MK_RESPRAY, MK_EXPORT, MK_STASH };
enum { SL_MISSION = 0, SL_TARGET, SL_SHOP, SL_RESPRAY, SL_EXPORT, SL_TAXI, SL_ROB, SL_STASH };

typedef struct {
  int8_t kind;
  int32_t x, y;
} Marker;

/* ---- sound effects */
enum {
  SFX_PISTOL = 0,
  SFX_SMG,
  SFX_SHOTGUN,
  SFX_EMPTY,
  SFX_RELOAD,
  SFX_PUNCH,
  SFX_HIT,
  SFX_CRASH,
  SFX_EXPLOSION,
  SFX_DOOR,
  SFX_PICKUP,
  SFX_CASH,
  SFX_UI,
  SFX_SIREN_BLIP,
  SFX_COUNT
};

/* ---- radio stations */
enum {
  RAD_OFF = 0,
  RAD_VICE,     /* 80s synthwave */
  RAD_WEST,     /* West Coast G-Funk */
  RAD_LIBERTY,  /* Gritty East Coast minor piano/bass */
  RAD_ELECTRO,  /* Parole Electro / Arcade */
  RAD_SCANNER,  /* Police Dispatch Scanner */
  RAD_COUNT
};

/* ---- flash save data */
#define SAVE_MAGIC 0x4F4F5031u /* "OOP1" */
typedef struct {
  uint32_t magic;
  int32_t cash;
  int16_t mission_cur;
  uint16_t stashes_mask;
  uint8_t owned_w;
  uint8_t radio_station;
  uint8_t zoom_level;
  uint8_t pad0;
  int16_t ammo[W_COUNT];
  int16_t armor;
  uint16_t checksum;
} SaveData;

/* ---- globals */
extern uint8_t game_mode;
extern uint32_t game_ticks;
extern int32_t cash;
extern int32_t cam_x, cam_y;
extern int16_t cam_zoom; /* 256 = normal, >256 zoomed in, <256 zoomed out */
extern int16_t cam_shake;
extern uint8_t zoom_level; /* 0=CLOSE, 1=NORMAL, 2=WIDE, 3=SATELLITE */
extern uint8_t cut_kind;
extern int16_t cut_t;
extern uint16_t cut_car_paint;
extern uint16_t cut_ped_shirt;
extern uint8_t cut_car_model;
extern uint16_t day_min; /* 0..1439 (minutes in day, starts at 9:00 = 540) */

extern uint8_t tile_map[MAP_H][MAP_W];
extern uint8_t bldg_var[MAP_H][MAP_W];
extern Car cars[MAX_CARS];
extern Ped peds[MAX_PEDS];
extern Bullet bullets[MAX_BULLETS];
extern Pickup pickups[MAX_PICKUPS];
extern Particle fx_list[MAX_FX];
extern Skid skids[MAX_SKIDS];
extern Marker markers[MAX_MARKERS];

extern const CarSpec car_specs[CAR_MODELS];
extern const WeaponSpec weapon_specs[W_COUNT];

extern int8_t heat;          /* 0..5 */
extern int16_t heat_search;  /* ticks left to clear wanted */
extern int32_t last_seen_x, last_seen_y;
extern uint8_t heli_active;
extern int32_t heli_x, heli_y;
extern uint8_t heli_ang;

extern uint8_t shop_open;
extern int8_t shop_sel;
extern uint8_t rob_active;
extern int16_t rob_prog; /* 0..100 */
extern int16_t rob_cash;
extern int16_t mission_cur;
extern uint16_t stashes_mask;
extern uint8_t radio_station;

/* HUD banners & messages */
extern char hud_msg_buf[64];
extern int16_t hud_msg_t;
extern char hud_big_buf[32];
extern int16_t hud_big_t;
extern char hud_obj_buf[48];

/* ---- landmarks (world.c) */
extern int32_t pos_walt_x, pos_walt_y;
extern int32_t pos_shop_x, pos_shop_y;
extern int32_t pos_respray_x, pos_respray_y;
extern int32_t pos_export_x, pos_export_y;
extern int32_t pos_hosp_x, pos_hosp_y;
extern int32_t pos_police_x, pos_police_y;
extern int32_t pos_laundry_x, pos_laundry_y;
extern int32_t pos_harbor_x, pos_harbor_y;
extern int32_t pos_dale_x, pos_dale_y;
extern int32_t pos_suv_x, pos_suv_y;

/* ---- math helpers (world.c) */
int32_t sin256(int32_t a);
int32_t cos256(int32_t a);
uint8_t atan2_256(int32_t dx, int32_t dy);
int32_t isqrt32(uint32_t v);
int32_t dist_fp(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
int32_t ang_diff(int32_t a, int32_t b); /* signed -128..127 */
uint32_t oop_rnd(void);
void str_copy(char *dst, const char *src, int max);
void str_cat(char *dst, const char *src, int max);
void str_cat_int(char *dst, int32_t v, int max);

/* ---- world.c */
void world_init(void);
void world_tick(void);
uint8_t world_tile(int32_t wx, int32_t wy);
int world_solid(int32_t wx, int32_t wy);
int world_is_road(int tx, int ty);
int world_is_water(int32_t wx, int32_t wy);
int world_light_ns(void); /* 0=green, 1=yellow, 2=red for N-S traffic */
int world_light_ew(void); /* 0=green, 1=yellow, 2=red for E-W traffic */
int world_push(int32_t *x, int32_t *y, int32_t rad);
int world_ray(int32_t x0, int32_t y0, int32_t dx, int32_t dy, int32_t max_steps, int32_t *hx, int32_t *hy);
int world_night_level(void); /* 0 (day) .. 255 (midnight) */

/* ---- car.c */
void cars_init(void);
int car_spawn(uint8_t model, int32_t x, int32_t y, uint8_t ang, uint8_t ai, uint16_t paint);
void car_remove(int ci);
void car_damage(int ci, int16_t dmg, int attacker);
int car_near(int32_t x, int32_t y, int32_t rad);
void cars_tick(const GameInput *in);
void car_explode(int ci, int attacker);
void car_add_skid(int32_t x, int32_t y);
uint16_t car_random_paint(uint8_t model);

/* ---- ped.c */
void peds_init(void);
int ped_spawn(uint8_t kind, int32_t x, int32_t y, uint8_t ang);
void ped_remove(int pi);
void ped_damage(int pi, int16_t dmg, int32_t kx, int32_t ky, int attacker);
void ped_alarm(int32_t x, int32_t y, int32_t rad);
void peds_tick(const GameInput *in);
void player_respawn(int32_t rx, int32_t ry, const char *banner);

/* ---- combat.c */
void combat_init(void);
void combat_tick(const GameInput *in);
void fire_weapon(int pi);
void pickup_drop(int32_t x, int32_t y, uint8_t type, int16_t amount);
int give_item(uint8_t pk_type);
void fx_burst(uint8_t kind, int32_t x, int32_t y, int n);
void fx_tick(void);

/* ---- cops.c */
void cops_init(void);
void cops_reset(void);
void heat_clear(void);
void crime(uint8_t kind, int32_t x, int32_t y);
void cops_tick(void);

/* ---- jobs.c */
void jobs_init(void);
void jobs_tick(const GameInput *in);
const char *shop_item_name(int i);
int shop_item_price(int i);
int shop_item_count(void);
int export_wanted_model(int idx);

/* ---- missions.c */
void missions_init(void);
void missions_tick(void);
void marker_set(int slot, int32_t x, int32_t y, int8_t kind);
void hud_msg(const char *s, int16_t ticks);
void hud_big(const char *s, int16_t ticks);
void hud_objective(const char *s);

/* ---- render.c */
void render_frame(void);

/* ---- audio.c */
void audio_init(void);
void audio_sfx(uint8_t id);
void audio_update_state(int engine_on, uint8_t cyls, uint8_t rpm, uint8_t skid, int horn, int siren, uint8_t station, int muted);
const char *radio_name(uint8_t station);

/* ---- main.c */
void save_game(void);
void load_game(void);

#endif
