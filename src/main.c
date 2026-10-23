/* ============================================================
   NIGHT DRIFT — portage NumWorks (.nwa)
   Jeu de drift nocturne pseudo-3D, inspire des "night drift"
   jouables en ligne. Ecran 320x240, EADK C.
   Contrôles :
     <- / ->  : tourner (drift a vitesse elevee)
     ^        : accelerer
     v        : freiner
     OK       : (re)commencer
     Back/Power : quitter
   ============================================================ */
#include <eadk.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Declarations obligatoires pour une app externe .nwa */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Night Drift";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

/* ---------- Constantes ---------- */
#define W 320
#define H 240
#define HORIZON 100

/* RGB565 : eadk_color_t est un uint16_t */
#define C(r,g,b) ((eadk_color_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|(((b)&0xF8)>>3)))

static const eadk_color_t COL_SKY     = C(4,6,20);
static const eadk_color_t COL_STAR    = C(220,225,255);
static const eadk_color_t COL_GROUND  = C(2,4,10);
static const eadk_color_t COL_ROAD_A  = C(22,22,30);
static const eadk_color_t COL_ROAD_B  = C(28,28,38);
static const eadk_color_t COL_DASH    = C(216,200,0);
static const eadk_color_t COL_CURB_R  = C(216,0,0);
static const eadk_color_t COL_CURB_W  = C(222,222,222);
static const eadk_color_t COL_LAMP    = C(255,200,32);
static const eadk_color_t COL_POLE    = C(60,60,70);
static const eadk_color_t COL_BODY    = C(220,30,30);
static const eadk_color_t COL_ROOF    = C(30,30,44);
static const eadk_color_t COL_TAIL    = C(255,40,40);
static const eadk_color_t COL_HEAD    = C(255,250,200);
static const eadk_color_t COL_SMOKE   = C(90,90,100);
static const eadk_color_t COL_CITY    = C(10,10,18);
static const eadk_color_t COL_WINDOW  = C(255,190,80);
static const eadk_color_t COL_TEXT    = C(255,255,255);
static const eadk_color_t COL_ACCENT  = C(255,200,32);

/* ---------- Virages du circuit (valeurs par tranche de 200 m) ---------- */
static const float s_curves[16] = {
  0.0f, 1.0f, 2.0f, 1.0f, 0.0f, -1.0f, -2.0f, -1.0f,
  2.0f, 0.0f, -2.0f, 1.0f, 0.0f, 2.0f, -2.0f, 0.0f
};

static float track_curve(float z) {
  int i = ((int)(z / 200.0f)) % 16;
  if (i < 0) i += 16;
  return s_curves[i];
}

/* ---------- Trafic ---------- */
#define N_CARS 4
typedef struct {
  float z;
  float lane;
  eadk_color_t col;
} Traffic;

/* ---------- Etat du jeu ---------- */
typedef struct {
  float playerZ;
  float playerX;
  float speed;
  float driftAngle;
  int   score;
  int   best;
  int   crashFlash;
  Traffic cars[N_CARS];
} Game;

static void game_reset(Game *g) {
  g->playerZ = 0; g->playerX = 0; g->speed = 0;
  g->driftAngle = 0; g->score = 0; g->crashFlash = 0;
  static const eadk_color_t cols[N_CARS] = {
    C(40,90,230), C(230,150,20), C(40,200,90), C(200,60,220)
  };
  for (int i = 0; i < N_CARS; i++) {
    g->cars[i].z = 600.0f + 900.0f * i;
    g->cars[i].lane = (float)(i % 3) - 1.0f;
    g->cars[i].col = cols[i];
  }
}

/* ---------- Dessin ---------- */
static void fill_rect(int x, int y, int w, int h, eadk_color_t c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  if (w <= 0 || h <= 0) return;
  eadk_display_push_rect_uniform((eadk_rect_t){(uint16_t)x,(uint16_t)y,(uint16_t)w,(uint16_t)h}, c);
}

static unsigned int hash(unsigned int x) {
  x = (x << 13) ^ x;
  return (x * (x * x * 15731 + 789221) + 1376312589) & 0x7fffffff;
}

static void draw_night_background(void) {
  fill_rect(0, 0, W, HORIZON, COL_SKY);
  for (unsigned int i = 0; i < 90; i++) {
    unsigned int h1 = hash(i + 1);
    unsigned int h2 = hash(i + 77);
    int sx = (int)(h1 % W);
    int sy = (int)(h2 % (HORIZON - 30));
    if ((h1 & 7) != 0) continue;
    fill_rect(sx, sy, 1, 1, COL_STAR);
  }
  int bx = 0;
  unsigned int bi = 0;
  while (bx < W) {
    unsigned int hsh = hash(bi + 500);
    int bw = 12 + (int)(hsh % 22);
    int bh = 14 + (int)((hsh >> 5) % 26);
    fill_rect(bx, HORIZON - bh, bw, bh, COL_CITY);
    for (int wy = HORIZON - bh + 3; wy < HORIZON - 2; wy += 5) {
      for (int wx = bx + 2; wx < bx + bw - 2; wx += 4) {
        unsigned int wh = hash((unsigned)(wx * 131 + wy));
        if ((wh & 3) == 0) fill_rect(wx, wy, 1, 2, COL_WINDOW);
      }
    }
    bx += bw + 2; bi++;
  }
}

static int project_t(float worldZ) {
  if (worldZ <= 220.0f) return 144;
  int t = (int)(15000.0f / (worldZ - 100.0f));
  if (t < 1) t = 1;
  if (t > 144) t = 144;
  return t;
}

static void draw_scene(Game *g) {
  draw_night_background();
  fill_rect(0, HORIZON, W, H - HORIZON, COL_GROUND);

  for (int t = 1; t <= 144; t += 2) {
    float worldZ = 100.0f + 15000.0f / (float)t;
    float segZ = g->playerZ + worldZ;
    float curve = track_curve(segZ);
    int stripe = ((int)(segZ / 25.0f)) & 1;

    float halfW = 10.0f + (float)t * t * 0.0055f;
    float center = 160.0f + curve * t * t * 0.0035f - g->playerX * halfW * 0.9f;

    int y = H - t;
    int cx = (int)center;
    int hw = (int)halfW;

    eadk_color_t roadCol = stripe ? COL_ROAD_B : COL_ROAD_A;
    fill_rect(cx - hw, y, hw * 2, 2, roadCol);

    eadk_color_t curb = stripe ? COL_CURB_W : COL_CURB_R;
    fill_rect(cx - hw, y, 3, 2, curb);
    fill_rect(cx + hw - 3, y, 3, 2, curb);

    if (stripe) fill_rect(cx - 1, y, 2, 2, COL_DASH);

    int lampIdx = (int)(segZ / 120.0f);
    if ((lampIdx & 3) == 0) {
      int px = cx - hw - 5;
      fill_rect(px, y, 2, 6, COL_POLE);
      fill_rect(px - 1, y, 4, 2, COL_LAMP);
      int px2 = cx + hw + 3;
      fill_rect(px2, y, 2, 6, COL_POLE);
      fill_rect(px2 - 1, y, 4, 2, COL_LAMP);
    }
  }

  for (int i = 0; i < N_CARS; i++) {
    float rel = g->cars[i].z - g->playerZ;
    if (rel < 220.0f || rel > 15200.0f) continue;
    int t = project_t(rel);
    float halfW = 10.0f + (float)t * t * 0.0055f;
    float curve = track_curve(g->playerZ + rel);
    float center = 160.0f + curve * t * t * 0.0035f - g->playerX * halfW * 0.9f;
    float x = center + g->cars[i].lane * halfW * 0.55f;
    int scale = t / 24; if (scale < 1) scale = 1;
    int cw = scale * 6, ch = scale * 4;
    int y = H - t - ch;
    fill_rect((int)x - cw/2, y, cw, ch, g->cars[i].col);
    fill_rect((int)x - cw/2, y + ch/2, scale, scale, COL_TAIL);
    fill_rect((int)x + cw/2 - scale, y + ch/2, scale, scale, COL_TAIL);
  }

  {
    int px = 160 + (int)(g->driftAngle * 14.0f);
    int py = H - 62;
    if (g->crashFlash > 0 && (g->crashFlash & 1)) px += 3;

    if (g->driftAngle > 0.35f || g->driftAngle < -0.35f) {
      int sx = (g->driftAngle > 0) ? px - 30 : px + 10;
      for (int s = 0; s < 5; s++) {
        int hsh = (unsigned)(s + (int)g->playerZ) % 7;
        fill_rect(sx + hsh, py + 34 - s*6, 6, 4, COL_SMOKE);
      }
    }

    fill_rect(px - 24, py + 24, 10, 12, COL_ROOF);
    fill_rect(px + 14, py + 24, 10, 12, COL_ROOF);
    fill_rect(px - 22, py + 8, 44, 18, COL_BODY);
    fill_rect(px - 18, py + 2, 36, 8, COL_ROOF);
    fill_rect(px - 14, py + 3, 12, 5, COL_SKY);
    fill_rect(px + 2, py + 3, 12, 5, COL_SKY);
    fill_rect(px - 22, py + 12, 3, 3, COL_TAIL);
    fill_rect(px + 19, py + 12, 3, 3, COL_TAIL);
    fill_rect(px - 18, py + 16, 3, 2, COL_HEAD);
    fill_rect(px + 15, py + 16, 3, 2, COL_HEAD);
  }

  char buf[48];
  snprintf(buf, sizeof(buf), "SCORE %d", g->score);
  eadk_display_draw_string(buf, (eadk_point_t){8, 8}, false, COL_TEXT, COL_SKY);
  snprintf(buf, sizeof(buf), "BEST %d", g->best);
  eadk_display_draw_string(buf, (eadk_point_t){8, 24}, false, COL_ACCENT, COL_SKY);
  snprintf(buf, sizeof(buf), "%d km/h", (int)g->speed);
  eadk_display_draw_string(buf, (eadk_point_t){W - 90, 8}, false, COL_TEXT, COL_SKY);
  snprintf(buf, sizeof(buf), "%d m", (int)g->playerZ);
  eadk_display_draw_string(buf, (eadk_point_t){W - 90, 24}, false, COL_TEXT, COL_SKY);

  int meterW = (int)((g->driftAngle < 0 ? -g->driftAngle : g->driftAngle) * 60.0f);
  fill_rect(W/2 - 62, 10, 124, 4, C(30,30,40));
  fill_rect(W/2 - 60, 11, meterW, 2, g->driftAngle > 0 ? COL_ACCENT : COL_TAIL);
  if (g->driftAngle > 0.35f || g->driftAngle < -0.35f) {
    eadk_display_draw_string("DRIFT !", (eadk_point_t){W/2 - 25, 20}, false, COL_ACCENT, COL_SKY);
  }
  if (g->crashFlash > 0) {
    eadk_display_draw_string("CRASH !", (eadk_point_t){W/2 - 30, 40}, false, COL_TAIL, COL_SKY);
  }
}

static int title_screen(int best) {
  eadk_display_push_rect_uniform(eadk_screen_rect, COL_SKY);
  for (unsigned int i = 0; i < 120; i++) {
    unsigned int h1 = hash(i + 3);
    if ((h1 & 5) != 0) continue;
    fill_rect((int)(h1 % W), (int)(hash(i + 40) % H), 1, 1, COL_STAR);
  }
  eadk_display_draw_string("NIGHT DRIFT", (eadk_point_t){70, 70}, true, COL_ACCENT, COL_SKY);
  eadk_display_draw_string("Portage NumWorks", (eadk_point_t){110, 105}, false, COL_TEXT, COL_SKY);
  eadk_display_draw_string("fleches : conduire / drifter", (eadk_point_t){70, 140}, false, COL_TEXT, COL_SKY);
  eadk_display_draw_string("OK : demarrer   Back : quitter", (eadk_point_t){60, 160}, false, COL_TEXT, COL_SKY);
  char buf[32];
  snprintf(buf, sizeof(buf), "Meilleur score : %d", best);
  eadk_display_draw_string(buf, (eadk_point_t){95, 195}, false, COL_ACCENT, COL_SKY);

  while (true) {
    eadk_keyboard_state_t k = eadk_keyboard_scan();
    if (eadk_keyboard_key_down(k, eadk_key_ok)) return 1;
    if (eadk_keyboard_key_down(k, eadk_key_back)) return 0;
    eadk_timing_msleep(50);
  }
}

int main(int argc, char * argv[]) {
  (void)argc; (void)argv;
  int best = 0;

  while (title_screen(best)) {
    Game g;
    game_reset(&g);
    int quit = 0;
    while (!quit) {
      eadk_keyboard_state_t k = eadk_keyboard_scan();
      if (eadk_keyboard_key_down(k, eadk_key_back)) break;

      int steer = 0;
      if (eadk_keyboard_key_down(k, eadk_key_left))  steer -= 1;
      if (eadk_keyboard_key_down(k, eadk_key_right)) steer += 1;

      if (eadk_keyboard_key_down(k, eadk_key_up)) {
        g.speed += 3.2f;
      } else if (eadk_keyboard_key_down(k, eadk_key_down)) {
        g.speed -= 5.0f;
      } else {
        g.speed -= 0.8f;
      }
      if (g.speed < 0) g.speed = 0;
      if (g.speed > 260) g.speed = 260;

      float absX = g.playerX < 0 ? -g.playerX : g.playerX;
      int offroad = absX > 1.15f;
      if (offroad) g.speed -= 6.0f;

      float target = (float)steer;
      g.driftAngle += (target - g.driftAngle) * 0.18f;
      g.playerX += (g.driftAngle * 0.030f) * (0.5f + g.speed / 260.0f);
      if (g.playerX < -1.6f) g.playerX = -1.6f;
      if (g.playerX > 1.6f)  g.playerX = 1.6f;

      float absDrift = g.driftAngle < 0 ? -g.driftAngle : g.driftAngle;
      if (g.speed > 90.0f && absDrift > 0.35f) {
        g.score += (int)(absDrift * g.speed * 0.012f);
      }
      g.score += (int)(g.speed * 0.004f);

      g.playerZ += g.speed * 0.055f;

      for (int i = 0; i < N_CARS; i++) {
        g.cars[i].z += 1.1f;
        if (g.cars[i].z - g.playerZ < -50.0f) {
          g.cars[i].z = g.playerZ + 9000.0f + (float)(eadk_random() % 5000);
          g.cars[i].lane = (float)((eadk_random() % 3)) - 1.0f;
        }
        float rel = g.cars[i].z - g.playerZ;
        float laneDiff = g.cars[i].lane - g.playerX;
        if (laneDiff < 0) laneDiff = -laneDiff;
        if (rel < 230.0f && rel > 0.0f && laneDiff < 0.4f && g.speed > 40.0f) {
          g.speed *= 0.25f;
          g.crashFlash = 12;
        }
      }
      if (g.crashFlash > 0) g.crashFlash--;

      draw_scene(&g);
      eadk_timing_msleep(20);
    }
    if (g.score > best) best = g.score;
  }
  return 0;
}
