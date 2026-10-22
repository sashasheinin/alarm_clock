/*
 * Minecraft-style clock faces for a 480x480 LVGL 9 display.
 * Everything is drawn from plain squares: no image assets, original art.
 *
 *  1. Sky      - blocky sun/moon follows the real time of day, drifting clouds, grass+dirt ground
 *  2. Lamp     - 5x7 "lamp" digits on a dark wall, redstone-style signal pulse along the bottom
 *  3. Buddy    - friendly green block face that blinks, time underneath
 *  4. Cave     - stone wall, sparkling diamond-ore digits, a block that cracks over each minute
 *  5. Torch    - nearly black, a flickering torch and dim warm digits (for the night)
 *  6. Nether   - dark red rock, glowing orange digits, a lava pool with slow waves and bubbles
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

static const uint8_t DIGITS[11][7] = {
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
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},  /* dash: time unknown */
};
#define DIGIT_DASH 10

static bool s_time_valid;   /* false = show --:-- (set by the manager each tick) */
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
    if (!s_time_valid) {
        dg[0] = dg[1] = dg[2] = dg[3] = DIGIT_DASH;
        colon = true;
    }
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

static const uint16_t STAR_POS[8][2] = {{30,40},{120,90},{200,30},{290,70},{360,25},{420,95},{70,150},{440,170}};

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

/* ===================================================================== 4. CAVE */

/* Stone wall, diamond-ore digits that sparkle, and a block below that cracks over each
 * minute and breaks at :00 into the next ore. */
#define CAVE_CELL   16          /* mining block: 8x8 cells */
#define CAVE_BX     (240 - 4 * CAVE_CELL)
#define CAVE_BY     300
#define CAVE_CRACKS 24

typedef struct { uint32_t speck, dark; } ore_t;
static const ore_t ORES[] = {
    {0xD8AF93, 0x9C7A64},   /* iron */
    {0xFCDC3C, 0xC79A1E},   /* gold */
    {0xE8261C, 0x9A1510},   /* redstone */
    {0x3FE07A, 0x1E9A4A},   /* emerald */
    {0x5FE8E2, 0x2BA5A0},   /* diamond */
    {0x3060E0, 0x1C3A8C},   /* lapis (no coal: its specks look like the cracks) */
};
#define ORE_COUNT ((int)(sizeof(ORES) / sizeof(ORES[0])))

/* ore specks in the 8x8 block: 0 = stone, 1 = ore, 2 = ore shadow */
static const uint8_t ORE_MAP[8][8] = {
    {0,0,0,0,0,0,0,0}, {0,1,1,0,0,0,0,0}, {0,1,2,0,0,1,1,0}, {0,0,0,0,0,1,2,0},
    {0,0,0,1,0,0,0,0}, {0,0,1,2,0,0,1,0}, {0,0,0,0,0,1,2,0}, {0,0,0,0,0,0,0,0},
};
/* crack path from the middle outwards, one cell per 2.5 s */
static const uint8_t CRACK[CAVE_CRACKS][2] = {
    {3,3},{4,4},{3,4},{2,4},{4,3},{5,5},{2,2},{1,4},{5,2},{6,5},{1,1},{4,6},
    {6,2},{2,6},{0,4},{6,6},{7,1},{1,6},{3,1},{5,7},{0,1},{7,4},{2,0},{7,6},
};
static const uint32_t STONES[4] = {0x6B6B6B, 0x747474, 0x626262, 0x5A5A5A};

typedef struct {
    pixel_clock_t pc;
    lv_obj_t *blk[8][8];
    int8_t ore, cracked;       /* ore index shown, crack cells drawn (-1 = redraw) */
    lv_obj_t *spark;           /* digit cell lit white right now, or NULL */
} cave_t;

static uint32_t stone_shade(int r, int c) { return STONES[(r * 7 + c * 11 + (r * c) % 5) % 4]; }

static void cave_draw_block(cave_t *k, int ore)
{
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            uint32_t col = ORE_MAP[r][c] == 1 ? ORES[ore].speck
                         : ORE_MAP[r][c] == 2 ? ORES[ore].dark : stone_shade(r + 3, c + 1);
            set_cell(k->blk[r][c], lv_color_hex(col), LV_OPA_COVER);
        }
}

static lv_obj_t *cave_create(lv_obj_t *parent)
{
    cave_t *k = lv_zalloc(sizeof(*k));
    if (!k) return NULL;
    lv_obj_t *root = make_root(parent, 0x5A5A5A, k);

    for (int r = 0; r < 12; r++)             /* stone wall, 40 px blocks */
        for (int c = 0; c < 12; c++)
            px(root, c * 40, r * 40, 40, 40, stone_shade(r, c) - 0x1C1C1C, LV_OPA_COVER);

    pc_init(&k->pc, root, PC_X(17, 15), 120, 17, 15, 0x5FE8E2, 0, LV_OPA_TRANSP);

    px(root, CAVE_BX - 4, CAVE_BY - 4, 8 * CAVE_CELL + 8, 8 * CAVE_CELL + 8, 0x2A2A2A, LV_OPA_COVER);
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            k->blk[r][c] = px(root, CAVE_BX + c * CAVE_CELL, CAVE_BY + r * CAVE_CELL,
                              CAVE_CELL, CAVE_CELL, 0, LV_OPA_COVER);
    k->ore = -1;
    k->cracked = -1;
    return root;
}

static void cave_update(lv_obj_t *root, const struct tm *t, uint32_t tick)
{
    (void)tick;
    cave_t *k = lv_obj_get_user_data(root);

    if (k->spark) set_cell(k->spark, k->pc.on, LV_OPA_COVER);   /* last tick's sparkle off */
    k->spark = NULL;
    pc_set(&k->pc, t->tm_hour, t->tm_min, (t->tm_sec & 1) == 0);
    if (s_time_valid && esp_random() % 3 == 0) {                /* sparkle one lit cell */
        int d = esp_random() % 4, r = esp_random() % 7, c = esp_random() % 5;
        if (DIGITS[k->pc.shown[d]][r] & (1 << (4 - c))) {
            k->spark = k->pc.cell[d][r][c];
            set_cell(k->spark, lv_color_hex(0xFFFFFF), LV_OPA_COVER);
        }
    }

    int ore = t->tm_min % ORE_COUNT;
    int cracked = t->tm_sec * CAVE_CRACKS / 60;
    if (ore != k->ore || cracked < k->cracked) {                /* new minute: fresh block */
        k->ore = ore;
        cave_draw_block(k, ore);
        k->cracked = 0;
    }
    for (; k->cracked < cracked; k->cracked++)
        set_cell(k->blk[CRACK[k->cracked][0]][CRACK[k->cracked][1]], lv_color_hex(0x1C1C1C), LV_OPA_COVER);
}

/* ==================================================================== 5. TORCH */

/* Nearly black, one flickering torch and dim warm digits: few lit pixels for the night. */
#define TORCH_CELL 16
#define TORCH_X    (240 - 2 * TORCH_CELL)    /* flame is 4 cells wide, stick 2 */
#define TORCH_Y    64

static const uint32_t FLAMES[4] = {0xFFE07A, 0xFFC23A, 0xFF8C1A, 0xE0561A};

typedef struct {
    pixel_clock_t pc;
    lv_obj_t *flame[4][4];
    lv_obj_t *glow[12];
} torch_t;

static lv_obj_t *torch_create(lv_obj_t *parent)
{
    torch_t *k = lv_zalloc(sizeof(*k));
    if (!k) return NULL;
    lv_obj_t *root = make_root(parent, 0x050302, k);

    /* glow: a ring of faint warm squares around the flame */
    static const int8_t GLOW[12][2] = {{-1,0},{-1,1},{-1,2},{-1,3},{4,0},{4,1},{4,2},{4,3},
                                       {0,-1},{1,-1},{2,-1},{3,-1}};
    for (int i = 0; i < 12; i++)
        k->glow[i] = px(root, TORCH_X + GLOW[i][1] * TORCH_CELL, TORCH_Y + GLOW[i][0] * TORCH_CELL,
                        TORCH_CELL, TORCH_CELL, 0xFF9A30, LV_OPA_20);
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            k->flame[r][c] = px(root, TORCH_X + c * TORCH_CELL, TORCH_Y + r * TORCH_CELL,
                                TORCH_CELL, TORCH_CELL, FLAMES[r], LV_OPA_COVER);
    for (int r = 0; r < 8; r++)               /* stick, two shades of wood */
        for (int c = 0; c < 2; c++)
            px(root, TORCH_X + (c + 1) * TORCH_CELL, TORCH_Y + (r + 4) * TORCH_CELL, TORCH_CELL,
               TORCH_CELL, c ? 0x4A3018 : 0x6B4626, LV_OPA_COVER);

    pc_init(&k->pc, root, PC_X(13, 11), 300, 13, 11, 0xB07838, 0, LV_OPA_TRANSP);
    return root;
}

static void torch_update(lv_obj_t *root, const struct tm *t, uint32_t tick)
{
    (void)tick;
    torch_t *k = lv_obj_get_user_data(root);
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            /* top row and outer columns flicker in and out; the core stays lit */
            bool edge = r == 0 || c == 0 || c == 3;
            uint32_t rnd = esp_random();
            if (edge && (rnd & 3) == 0) {
                set_cell(k->flame[r][c], lv_color_hex(0), LV_OPA_TRANSP);
            } else {
                int shade = r + (int)((rnd >> 2) % 2) - (r == 3 ? 1 : 0);
                set_cell(k->flame[r][c], lv_color_hex(FLAMES[shade > 3 ? 3 : shade]), LV_OPA_COVER);
            }
        }
    for (int i = 0; i < 12; i++)
        lv_obj_set_style_bg_opa(k->glow[i], (lv_opa_t)(25 + esp_random() % 30), 0);
    pc_set(&k->pc, t->tm_hour, t->tm_min, (t->tm_sec & 1) == 0);
}

/* =================================================================== 6. NETHER */

/* Dark red rock, glowing orange digits, a lava pool along the bottom with slow waves
 * and the odd bubble. */
#define LAVA_ROWS 3
#define LAVA_COLS 12
#define LAVA_Y    (480 - LAVA_ROWS * 40)

static const uint32_t NETHERRACK[4] = {0x5A1A1A, 0x6B2020, 0x4A1414, 0x732424};
static const uint32_t LAVA[6] = {0xC8380C, 0xE0500E, 0xF2701A, 0xFF9426, 0xF2701A, 0xE0500E};

typedef struct {
    pixel_clock_t pc;
    lv_obj_t *lava[LAVA_ROWS][LAVA_COLS];
    int phase;
} nether_t;

static lv_obj_t *nether_create(lv_obj_t *parent)
{
    nether_t *k = lv_zalloc(sizeof(*k));
    if (!k) return NULL;
    lv_obj_t *root = make_root(parent, 0x4A1414, k);

    for (int r = 0; r < 8; r++)               /* rock, 48 px blocks, above the lava */
        for (int c = 0; c < 10; c++)
            px(root, c * 48, r * 48, 48, 48, NETHERRACK[(r * 7 + c * 5 + (r * c) % 3) % 4], LV_OPA_COVER);
    for (int r = 0; r < LAVA_ROWS; r++)
        for (int c = 0; c < LAVA_COLS; c++)
            k->lava[r][c] = px(root, c * 40, LAVA_Y + r * 40, 40, 40, LAVA[0], LV_OPA_COVER);

    pc_init(&k->pc, root, PC_X(17, 15), 130, 17, 15, 0xFFB238, 0, LV_OPA_TRANSP);
    k->phase = -1;
    return root;
}

static void nether_update(lv_obj_t *root, const struct tm *t, uint32_t tick)
{
    nether_t *k = lv_obj_get_user_data(root);
    pc_set(&k->pc, t->tm_hour, t->tm_min, (t->tm_sec & 1) == 0);

    int phase = (tick / (2 * MC_TICK_MS)) % 6;      /* waves move one step every 400 ms */
    if (phase == k->phase) return;
    k->phase = phase;
    int bubble = esp_random() % (LAVA_COLS * 4);    /* about one bubble in four steps */
    for (int r = 0; r < LAVA_ROWS; r++)
        for (int c = 0; c < LAVA_COLS; c++) {
            uint32_t col = LAVA[(c + r * 2 + phase) % 6];
            if (r == 0 && c == bubble) col = 0xFFD86A;
            set_cell(k->lava[r][c], lv_color_hex(col), LV_OPA_COVER);
        }
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
    {"cave",  cave_create,  cave_update},
    {"torch", torch_create, torch_update},
    {"nether", nether_create, nether_update},
};
#define FACE_COUNT ((int)(sizeof(FACES) / sizeof(FACES[0])))

static lv_obj_t *s_screen, *s_root;
static lv_timer_t *s_shuffle;
static int s_idx = -1;
static int s_dx, s_dy;          /* pixel-shift offset */
static bool s_asleep;
static unsigned s_enabled = ~0u;   /* faces in the shuffle (bit per face) */

static void tick_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_root) return;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    s_time_valid = tm.tm_year + 1900 >= 2025;   /* before RTC load / NTP sync the year is 1970 */
    FACES[s_idx].update(s_root, &tm, lv_tick_get());
}

/* Registered on the screen, not the face root: the root is deleted on switch,
 * and an object must not be deleted from inside its own event handler. */
static void tap_cb(lv_event_t *e) { (void)e; if (!s_asleep) mc_clock_next(); }

static void show(int idx)
{
    if (s_root) lv_obj_delete(s_root);
    s_idx = idx;
    s_root = FACES[idx].create(s_screen);   /* NULL if out of memory: blank until next switch */
    if (s_root) lv_obj_set_pos(s_root, s_dx, s_dy);
    tick_cb(NULL);
}

void mc_clock_next(void)
{
    int pool[FACE_COUNT], count = 0;
    for (int i = 0; i < FACE_COUNT; i++)
        if ((s_enabled & (1u << i)) && i != s_idx) pool[count++] = i;
    if (count == 0) {                                   /* only the current face is enabled */
        if (s_idx >= 0 && s_root) return;
        for (int i = 0; i < FACE_COUNT; i++)
            if (s_enabled & (1u << i)) pool[count++] = i;
    }
    show(pool[esp_random() % count]);
    if (s_shuffle) lv_timer_reset(s_shuffle);   /* a hand-picked face gets the full interval */
}

void mc_clock_show(int idx)
{
    if (idx < 0 || idx >= FACE_COUNT) return;
    show(idx);
    if (s_shuffle) lv_timer_reset(s_shuffle);
}

int mc_clock_face_count(void) { return FACE_COUNT; }

const char *mc_clock_face_name(int idx) { return (idx >= 0 && idx < FACE_COUNT) ? FACES[idx].name : ""; }

void mc_clock_set_enabled(unsigned mask)
{
    mask &= (1u << FACE_COUNT) - 1;
    s_enabled = mask ? mask : ~0u;
    if (s_screen && !s_asleep && s_idx >= 0 && !(s_enabled & (1u << s_idx))) mc_clock_next();
}

void mc_clock_set_offset(int dx, int dy)
{
    s_dx = dx;
    s_dy = dy;
    if (s_root) lv_obj_set_pos(s_root, dx, dy);
}

void mc_clock_sleep(void)
{
    if (s_asleep) return;
    s_asleep = true;
    if (s_root) lv_obj_delete(s_root);
    s_root = NULL;
    lv_timer_pause(s_shuffle);
}

void mc_clock_wake(void)
{
    if (!s_asleep) return;
    s_asleep = false;
    lv_timer_resume(s_shuffle);
    mc_clock_next();
}

static void shuffle_cb(lv_timer_t *timer) { (void)timer; mc_clock_next(); }

void mc_clock_init(lv_obj_t *screen)
{
    s_screen = screen;
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    /* black behind the face: the pixel shift exposes a few px at the edges */
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(s_screen, tap_cb, LV_EVENT_CLICKED, NULL);
    lv_timer_create(tick_cb, MC_TICK_MS, NULL);
    s_shuffle = lv_timer_create(shuffle_cb, MC_SHUFFLE_MS, NULL);
    mc_clock_next();
}
