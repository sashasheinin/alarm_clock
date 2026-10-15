/*
 * Minecraft-style clock faces for a 480x480 LVGL 9 display.
 * Everything is drawn from plain squares: no image assets, original art.
 *
 *  1. Sky      - blocky sun/moon follows the real time of day, drifting clouds, grass+dirt ground
 *  2. Lamp     - 5x7 "lamp" digits on a dark wall, redstone-style signal pulse along the bottom
 *  3. Buddy    - friendly green block face that blinks, time underneath
 */
#include "mc_clock.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_random.h"

#define MC_SHUFFLE_MS   (10 * 60 * 1000)   /* auto-switch face every 10 min */
#define MC_TICK_MS      200

/* ------------------------------------------------------------------ helpers */

static lv_obj_t *px(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, lv_opa_t opa)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);   /* taps fall through to the screen */
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void free_cb(lv_event_t *e) { lv_free(lv_event_get_user_data(e)); }

static lv_obj_t *make_root(lv_obj_t *parent, uint32_t bg, void *data)
{
    lv_obj_t *r = px(parent, 0, 0, 480, 480, bg, LV_OPA_COVER);
    lv_obj_set_user_data(r, data);
    lv_obj_add_event_cb(r, free_cb, LV_EVENT_DELETE, data);
    return r;
}

static void set_cell(lv_obj_t *o, lv_color_t c, lv_opa_t opa)
{
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
}

/* ------------------------------------------------------- 5x7 pixel digit grid */

static const uint8_t DIGITS[10][7] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},  /* 0 */
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},  /* 1 */
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},  /* 2 */
    {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},  /* 3 */
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},  /* 4 */
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},  /* 5 */
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},  /* 6 */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},  /* 7 */
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},  /* 8 */
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},  /* 9 */
};
static const uint8_t DIGIT_COL[4] = {0, 6, 14, 20};   /* 25 columns total, colon at col 12 */

typedef struct {
    lv_obj_t *cell[4][7][5];
    lv_obj_t *colon[2];
    int8_t    shown[4];
    int8_t    colon_state;
    lv_color_t on, off;
    lv_opa_t   off_opa;
} pixel_clock_t;

static void pc_init(pixel_clock_t *pc, lv_obj_t *parent, int x, int y, int pitch, int size,
                    uint32_t on, uint32_t off, lv_opa_t off_opa)
{
    pc->on = lv_color_hex(on);
    pc->off = lv_color_hex(off);
    pc->off_opa = off_opa;
    for (int d = 0; d < 4; d++) {
        pc->shown[d] = -1;
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 5; c++)
                pc->cell[d][r][c] = px(parent, x + (DIGIT_COL[d] + c) * pitch, y + r * pitch,
                                       size, size, off, off_opa);
    }
    pc->colon[0] = px(parent, x + 12 * pitch, y + 2 * pitch, size, size, off, off_opa);
    pc->colon[1] = px(parent, x + 12 * pitch, y + 4 * pitch, size, size, off, off_opa);
    pc->colon_state = -1;
}

static void pc_set(pixel_clock_t *pc, int hh, int mm, bool colon)
{
    int dg[4] = {hh / 10, hh % 10, mm / 10, mm % 10};
    for (int d = 0; d < 4; d++) {
        if (pc->shown[d] == dg[d]) continue;
        pc->shown[d] = dg[d];
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 5; c++) {
                bool lit = DIGITS[dg[d]][r] & (1 << (4 - c));
                set_cell(pc->cell[d][r][c], lit ? pc->on : pc->off, lit ? LV_OPA_COVER : pc->off_opa);
            }
    }
    if (pc->colon_state != (int8_t)colon) {
        pc->colon_state = colon;
        for (int i = 0; i < 2; i++)
            set_cell(pc->colon[i], colon ? pc->on : pc->off, colon ? LV_OPA_COVER : pc->off_opa);
    }
}

/* width of a grid at a given pitch/size, to center it */
#define PC_X(pitch, size) ((480 - (24 * (pitch) + (size))) / 2)

/* ======================================================================= 1. SKY */

typedef struct {
    pixel_clock_t pc;
    lv_obj_t *sun;
    lv_obj_t *clouds[3];
    lv_obj_t *stars[8];
    int phase;       /* 0 night, 1 twilight, 2 day */
    int last_min;
} sky_t;

static const uint8_t STAR_POS[8][2] = {{30,40},{120,90},{200,30},{290,70},{360,25},{420,95},{70,150},{440,170}};

static lv_obj_t *make_cloud(lv_obj_t *parent, int y, int ms, int delay)
{
    lv_obj_t *c = px(parent, -150, y, 120, 40, 0xFFFFFF, LV_OPA_TRANSP);
    px(c, 0, 16, 120, 24, 0xFFFFFF, LV_OPA_COVER);
    px(c, 20, 0, 60, 24, 0xFFFFFF, LV_OPA_COVER);
    px(c, 70, 8, 40, 16, 0xFFFFFF, LV_OPA_COVER);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, c);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
    lv_anim_set_values(&a, -150, 480);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
    return c;
}

static lv_obj_t *sky_create(lv_obj_t *parent)
{
    sky_t *s = lv_zalloc(sizeof(*s));
    if (!s) return NULL;
    lv_obj_t *root = make_root(parent, 0x78B4FF, s);
    s->phase = -1;
    s->last_min = -1;

    for (int i = 0; i < 8; i++)
        s->stars[i] = px(root, STAR_POS[i][0], STAR_POS[i][1], 6, 6, 0xFFFFFF, LV_OPA_COVER);

    s->sun = px(root, 0, 0, 56, 56, 0xFFE14D, LV_OPA_COVER);

    s->clouds[0] = make_cloud(root, 60, 70000, 0);
    s->clouds[1] = make_cloud(root, 150, 95000, 20000);
    s->clouds[2] = make_cloud(root, 100, 80000, 45000);

    /* ground: 20 columns x 4 rows of 24px blocks, grass on top, dirt below */
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 20; col++) {
            uint32_t c = (row == 0) ? ((col & 1) ? 0x4F8F28 : 0x5DA130)
                                    : (((col + row) & 1) ? 0x6B4627 : 0x7A5230);
            px(root, col * 24, 384 + row * 24, 24, 24, c, LV_OPA_COVER);
        }

    /* digits last so they sit above sun and clouds */
    pc_init(&s->pc, root, PC_X(17, 15), 232, 17, 15, 0xFFFFFF, 0x000000, LV_OPA_TRANSP);
    return root;
}

static void sky_update(lv_obj_t *root, const struct tm *t, uint32_t tick)
{
    (void)tick;
    sky_t *s = lv_obj_get_user_data(root);
    int h = t->tm_hour, mins = h * 60 + t->tm_min;

    int phase = (h >= 7 && h < 17) ? 2 : ((h == 5 || h == 6 || h == 17 || h == 18) ? 1 : 0);
    bool sun_up = (h >= 6 && h < 18);

    if (phase != s->phase || mins != s->last_min) {
        if (phase != s->phase) {
            static const uint32_t SKY[3] = {0x0B1030, 0xFF9A5C, 0x78B4FF};
            static const uint8_t CLOUD_OPA[3] = {70, 200, 255};
            lv_obj_set_style_bg_color(root, lv_color_hex(SKY[phase]), 0);
            for (int i = 0; i < 3; i++) lv_obj_set_style_opa(s->clouds[i], CLOUD_OPA[phase], 0);
            for (int i = 0; i < 8; i++) {
                if (phase == 0) lv_obj_remove_flag(s->stars[i], LV_OBJ_FLAG_HIDDEN);
                else            lv_obj_add_flag(s->stars[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        /* progress 0..1 along the arc: sun 06:00-18:00, moon 18:00-06:00 */
        int since = sun_up ? (mins - 360) : ((mins + 1440 - 1080) % 1440);
        float p = since / 720.0f;
        int x = 10 + (int)(p * (480 - 76));
        int y = 30 + (int)(180.0f * (1.0f - 4.0f * p * (1.0f - p)));
        lv_obj_set_pos(s->sun, x, y);
        lv_obj_set_style_bg_color(s->sun, lv_color_hex(sun_up ? 0xFFE14D : 0xE8ECF4), 0);
        s->phase = phase;
        s->last_min = mins;
    }
    pc_set(&s->pc, h, t->tm_min, (t->tm_sec & 1) == 0);
}

/* ===================================================================== 2. LAMP */

typedef struct {
    pixel_clock_t pc;
    lv_obj_t *wire[20];
    int pulse;
} lamp_t;

static lv_obj_t *lamp_create(lv_obj_t *parent)
{
    lamp_t *l = lv_zalloc(sizeof(*l));
    if (!l) return NULL;
    lv_obj_t *root = make_root(parent, 0x15100C, l);
    l->pulse = -1;

    pc_init(&l->pc, root, PC_X(17, 15), 150, 17, 15, 0xFFC24A, 0x3A2A14, LV_OPA_COVER);

    for (int i = 0; i < 20; i++)
        l->wire[i] = px(root, i * 24 + 2, 372, 20, 12, 0x3A0D0D, LV_OPA_COVER);
    return root;
}

static void lamp_update(lv_obj_t *root, const struct tm *t, uint32_t tick)
{
    lamp_t *l = lv_obj_get_user_data(root);
    pc_set(&l->pc, t->tm_hour, t->tm_min, (t->tm_sec & 1) == 0);

    int pulse = (tick / MC_TICK_MS) % 24;    /* one cell per update, then a 4-step pause */
    if (pulse != l->pulse) {
        l->pulse = pulse;
        for (int i = 0; i < 20; i++) {
            uint32_t c = 0x3A0D0D;
            if (i == pulse)      c = 0xFF2B1F;
            else if (i == pulse - 1) c = 0x8A1A14;
            lv_obj_set_style_bg_color(l->wire[i], lv_color_hex(c), 0);
        }
    }
}

/* ==================================================================== 3. BUDDY */

typedef struct {
    pixel_clock_t pc;
    lv_obj_t *face[8][8];
    int8_t blink;    /* -1 = eyes not drawn yet */
} buddy_t;

static const uint32_t GREENS[3] = {0x4CAF3E, 0x43A036, 0x5BBF4A};

static uint32_t buddy_base(int r, int c) { return GREENS[(r * 7 + c * 13 + r * c) % 3]; }

static lv_obj_t *buddy_create(lv_obj_t *parent)
{
    buddy_t *b = lv_zalloc(sizeof(*b));
    if (!b) return NULL;
    lv_obj_t *root = make_root(parent, 0x1B3A19, b);

    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            b->face[r][c] = px(root, 136 + c * 26, 24 + r * 26, 26, 26, buddy_base(r, c), LV_OPA_COVER);

    /* cheeks */
    set_cell(b->face[4][1], lv_color_hex(0xE88A8A), LV_OPA_COVER);
    set_cell(b->face[4][6], lv_color_hex(0xE88A8A), LV_OPA_COVER);
    /* smile */
    static const uint8_t SMILE[6][2] = {{5,1},{5,6},{6,2},{6,3},{6,4},{6,5}};
    for (int i = 0; i < 6; i++)
        set_cell(b->face[SMILE[i][0]][SMILE[i][1]], lv_color_hex(0x10230F), LV_OPA_COVER);
    b->blink = -1;   /* eyes drawn by the first update(), which show() calls right away */

    pc_init(&b->pc, root, PC_X(17, 15), 270, 17, 15, 0xE9FFE5, 0x24491F, LV_OPA_COVER);
    return root;
}

static void buddy_update(lv_obj_t *root, const struct tm *t, uint32_t tick)
{
    buddy_t *b = lv_obj_get_user_data(root);
    int8_t blink = ((tick / MC_TICK_MS) % 20) == 0;        /* quick blink about every 4 s */
    if (blink != b->blink) {
        b->blink = blink;
        lv_color_t eye = blink ? lv_color_hex(buddy_base(3, 2)) : lv_color_hex(0x10230F);
        lv_color_t eye2 = blink ? lv_color_hex(buddy_base(3, 5)) : lv_color_hex(0x10230F);
        set_cell(b->face[3][2], eye, LV_OPA_COVER);
        set_cell(b->face[3][5], eye2, LV_OPA_COVER);
    }
    pc_set(&b->pc, t->tm_hour, t->tm_min, (t->tm_sec & 1) == 0);
}

/* ================================================================== manager */

typedef struct {
    const char *name;
    lv_obj_t *(*create)(lv_obj_t *parent);
    void (*update)(lv_obj_t *root, const struct tm *t, uint32_t tick);
} face_t;

static const face_t FACES[] = {
    {"sky",   sky_create,   sky_update},
    {"lamp",  lamp_create,  lamp_update},
    {"buddy", buddy_create, buddy_update},
};
#define FACE_COUNT ((int)(sizeof(FACES) / sizeof(FACES[0])))

static lv_obj_t *s_screen, *s_root;
static lv_timer_t *s_shuffle;
static int s_idx = -1;

static void tick_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_root) return;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    FACES[s_idx].update(s_root, &tm, lv_tick_get());
}

/* Registered on the screen, not the face root: the root is deleted on switch,
 * and an object must not be deleted from inside its own event handler. */
static void tap_cb(lv_event_t *e) { (void)e; mc_clock_next(); }

static void show(int idx)
{
    if (s_root) lv_obj_delete(s_root);
    s_idx = idx;
    s_root = FACES[idx].create(s_screen);   /* NULL if out of memory: blank until next switch */
    tick_cb(NULL);
}

void mc_clock_next(void)
{
    int n;
    do { n = esp_random() % FACE_COUNT; } while (n == s_idx && FACE_COUNT > 1);
    show(n);
    if (s_shuffle) lv_timer_reset(s_shuffle);   /* a hand-picked face gets the full interval */
}

void mc_clock_show(int idx)
{
    if (idx < 0 || idx >= FACE_COUNT) return;
    show(idx);
    if (s_shuffle) lv_timer_reset(s_shuffle);
}

int mc_clock_face_count(void) { return FACE_COUNT; }

static void shuffle_cb(lv_timer_t *timer) { (void)timer; mc_clock_next(); }

void mc_clock_init(lv_obj_t *screen)
{
    s_screen = screen;
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_screen, tap_cb, LV_EVENT_CLICKED, NULL);
    lv_timer_create(tick_cb, MC_TICK_MS, NULL);
    s_shuffle = lv_timer_create(shuffle_cb, MC_SHUFFLE_MS, NULL);
    mc_clock_next();
}
