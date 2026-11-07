/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann */
/* GNOMNOM's screen and controls, main-loop context, included last into app.c's unity unit: the title, play,
 * pause, over and attract screens drawn from the game's snapshot and event ring on gfx's two layers, the
 * background's post vector by stage theme and trip level, particles and toasts, the lights under the keys, the
 * buttons and knobs and the commands they send the game, the settings' saves, the screen reader, the splash. */
#include "gn_art.h"

#define UI_MSG_MS 1300u
#define UI_ATTRACT_MS 20000u
#define UI_CPU_MS 2000u
#define UI_NPART 32u
#define UI_NRING 6u
#define UI_NSTAR 24u
#define UI_NEYE 5u
#define UI_FIELD_H (GFX_MOUTH - GFX_FIELD_TOP)
#define UI_LANE_X(l) ((int32_t)(l) * GFX_LANE_W + 8)
#define UI_GNOME_Y 92                    /* the gnome's top: 20 tall, feet on the ground row */
#define UI_BTN_ALL ((1u << NB) - 1u)
#define UI_OUTLINE 128u                  /* ui_draw_mush: the silhouette's edge only (an AUTO note) */

static const char *const UI_BTN_NAME[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                            "PLAY", "REC", "OCT-", "OCT+"};
static const char *const UI_ENC_NAME[NE] = {"SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"};
static const char *const UI_AB_NAME[AB_COUNT] = {"", "ARP", "CHORD", "ECHO", "SLOW", "WIDE", "FREEZE", "SHIMMER",
                                                 "BURP"};
static const char *const UI_LP_NAME[LP_COUNT] = {"PLUCK", "SAW", "SQUARE", "BELL"};
static const char UI_LANE_LETTER[GN_LANES] = {'C', 'D', 'E', 'F', 'G', 'A', 'B'};
static const uint8_t UI_WKEY[GN_NWHITE] = GN_WHITE_KEYS, UI_WLANE[GN_NWHITE] = GN_WKEY_LANE;
static const uint8_t UI_BKEY[GN_NBLACK] = GN_BLACK_KEYS;
/* the sky ramp's loop: night blue 0, violet 4, magenta 8, orange 12, gold 16, pale yellow 20, pale green 24, teal
 * 28; a theme's sky runs from base at the top across span steps to the horizon */
static const int8_t UI_SKY_BASE[VT_COUNT] = {28, 4, 0, 8, 0, 0, 2, 0};
static const int8_t UI_SKY_SPAN[VT_COUNT] = {-8, 9, 7, 9, 32, 32, 8, 32};

typedef struct {
    int16_t x, y, dx, dy;                /* 1/16 px */
    uint8_t life, c, fall;               /* fall: gravity on */
} ui_particle_t;
typedef struct {
    uint8_t x, r, ttl;
} ui_ring_t;

static struct {
    gn_snap_t snap;
    gfx_post_t post;
    uint32_t now, frame_t, dt, frame;
    uint32_t keys, btn;                  /* held at the last input */
    uint32_t idle_t, rec_t;              /* the last input (the attract); REC went down (0: not held or acted) */
    uint32_t air_seen, flash_t;          /* the game's air count (MIDI play is input too); the last PERFECT flash */
    uint8_t looking;                     /* Bluetooth MIDI devices: the scan is up (lib/btdev) */
    uint8_t last_state;
    uint32_t pick, start_stage;          /* the title's pick, and the stage this run began at (PLAY AGAIN) */
    char msg[2][24];
    uint32_t msg_until;
    int32_t tone, space;                 /* K1 and K2 in detents, -12..12 */
    uint32_t cpu_until, agaric_until, card_until, boss_card_until, counters_until;
    uint8_t card_stage, counters_valid;
    uint16_t clear_eaten, clear_missed, clear_best;
    uint8_t bite, wince, dash, trip_face, invert, strobe, kick, flash, poison, lurch, shake, burst_wave;
    uint8_t pop[GN_STASH];
    uint8_t lane_toast[GN_LANES], lane_toast_q[GN_LANES], splat[GN_LANES], ghost[GN_LANES];
    int32_t gnome_x;
    uint8_t gnome_lane, echo_lane[3], last_beat;
    int16_t prev_y[GN_MAX_MUSH];
    uint8_t prev_lane[GN_MAX_MUSH], prev_state[GN_MAX_MUSH];
    uint8_t newhigh, over_seen;
    uint32_t wave_ph, zoom_ph, melt_ph;
    uint8_t cyc_a, cyc_b, cyc_fx;
    uint32_t rng, pt_next;
    uint32_t lane_keys[GN_LANES];
    uint8_t star_x[UI_NSTAR], star_y[UI_NSTAR], eye_x[UI_NEYE], eye_y[UI_NEYE];
    ui_particle_t pt[UI_NPART];
    ui_ring_t ring[UI_NRING];
} ui;
static volatile uint32_t ui_cpu_flag;    /* app_overload runs in the audio interrupt: it only raises this */

/* ------------------------------------------------------------------ small helpers --- */
static uint32_t ui_rand(void)
{
    uint32_t x = ui.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    ui.rng = x;
    return x;
}

static void ui_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static void ui_num(char *d, uint32_t v, uint32_t pad)   /* appended, zero-padded to pad digits (0: none) */
{
    char t[12];
    uint32_t n = 0;
    while (*d)
        d++;
    do
        t[n++] = (char)('0' + v % 10u);
    while ((v /= 10u) != 0u && n < 10u);
    while (n < pad && n < 10u)
        t[n++] = '0';
    while (n)
        *d++ = t[--n];
    *d = 0;
}

static int ui_in_run(void)
{
    return ui.snap.state == GS_PLAY || ui.snap.state == GS_PAUSE || ui.snap.state == GS_ATTRACT;
}

static uint32_t ui_stage(void) { return ui.snap.stage < GN_NSTAGES ? ui.snap.stage : GN_NSTAGES - 1u; }

static uint32_t ui_theme(void)
{
    return ui.snap.state == GS_TITLE ? VT_FOREST : GN_STAGES[ui_stage()].theme % VT_COUNT;
}

static uint32_t ui_level(void) { return ui.snap.trip_level > 9u ? 9u : ui.snap.trip_level; }

static uint32_t ui_bar_ms(uint32_t bpm) { return 240000u / (bpm < 40u ? 40u : bpm); }

static void ui_text_c(int32_t y, const char *s, uint8_t c, uint32_t flags)   /* centred */
{
    gfx_text((GFX_W - gfx_text_w(s, flags)) / 2, y, s, c, flags);
}

static void ui_hat_palette(void)         /* four shades of hue HAT x 360 / 32 at s 0.9 into 96..99 */
{
    static const uint8_t V[4] = {255, 204, 153, 102};
    uint32_t h = gn_set.hat * 48u, sx = h >> 8, f = h & 255u, i;
    for (i = 0; i < 4u; i++) {
        uint32_t v = V[i], p = v / 10u, q = v - (v - p) * f / 256u, t = p + (v - p) * f / 256u, r, g, b;
        switch (sx) {
        case 0: r = v, g = t, b = p; break;
        case 1: r = q, g = v, b = p; break;
        case 2: r = p, g = v, b = t; break;
        case 3: r = p, g = q, b = v; break;
        case 4: r = t, g = p, b = v; break;
        default: r = v, g = p, b = q; break;
        }
        gfx_pal_set(GN_PAL_GNOME + i, r, g, b);
    }
}

/* ------------------------------------------------------------------ messages, speech --- */
void ui_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + UI_MSG_MS;
    if (ann_on()) {
        t[0] = 0;
        ui_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            ui_cat(t, " ");
            ui_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void ui_speak(void)               /* a reader asking for the screen hears the state, stage, score, trip */
{
    const gn_snap_t *s = &ui.snap;
    char t[160];
    if (ann_on() && ann_where()) {
        t[0] = 0;
        ui_cat(t, "GNOMNOM. ");
        if (s->state == GS_TITLE) {
            ui_cat(t, "TITLE. PRESS PLAY. HIGH SCORE ");
            ui_num(t, gn_set.hiscore, 0);
            ui_cat(t, ". STAGE ");
            ui_num(t, ui.pick + 1u, 0);
            if (ui.pick)
                ui_cat(t, " PRACTICE");
            ui_cat(t, gn_set.jam ? ". JAM ON" : ". JAM OFF");
            if (gn_set.flash_off)
                ui_cat(t, ". FLASH OFF");
        } else {
            if (s->state == GS_ATTRACT)
                ui_cat(t, "DEMO. ");
            if (s->state == GS_OVER)
                ui_cat(t, "YOU CAME DOWN. ");
            if (s->state == GS_PAUSE)
                ui_cat(t, "PAUSED. ");
            ui_cat(t, "STAGE ");
            ui_num(t, ui_stage() + 1u, 0);
            ui_cat(t, " ");
            ui_cat(t, GN_STAGES[ui_stage()].name);
            if (s->practice)
                ui_cat(t, ". PRACTICE");
            ui_cat(t, ". SCORE ");
            ui_num(t, s->score, 0);
            ui_cat(t, ". TRIP ");
            ui_num(t, (uint32_t)(s->trip < 0 ? 0 : s->trip), 0);
            ui_cat(t, ". COMBO ");
            ui_num(t, s->combo, 0);
            if (s->state == GS_OVER && ui.newhigh)
                ui_cat(t, ". NEW HIGH SCORE");
        }
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(ui.now);
}

/* ------------------------------------------------------------------ commands, saves --- */
static void ui_cmd(uint32_t cmd, uint32_t stage, uint32_t jam)   /* one a frame, only once the last was taken */
{
    if (gn_cmd.ack != gn_cmd.req)
        return;
    gn_cmd.cmd = cmd;
    gn_cmd.stage = stage;
    gn_cmd.jam = jam;
#ifndef PLAT_HOST                        /* each run its own death caps, stash picks and remixes; the simulator keeps
                                          * app.c's fixed seed (or gn seed) so a run reproduces */
    if (cmd == CMD_START || cmd == CMD_ATTRACT)
        gn_cmd.seed = (plat_ms() ^ plat_ticks24() << 8) | 1u;
#endif
    GN_BARRIER();
    gn_cmd.req++;
}

/* what a run leaves in the settings: the furthest stage, the high score (runs from stage 0 only); saved when
 * anything is dirty. Called at GAME OVER (the game is silent then), on HOME from the pause and before PLAY */
static void ui_bank(void)
{
    const gn_snap_t *s = &ui.snap;
    if (ui_in_run() || s->state == GS_OVER) {
        if (ui_stage() > gn_set.furthest) {
            gn_set.furthest = (uint8_t)ui_stage();
            gn_set_dirty = 1;
        }
        if (!s->practice && s->score > gn_set.hiscore) {
            gn_set.hiscore = s->score;
            gn_set_dirty = 1;
            ui.newhigh = 1;
            game_hiscore(gn_set.hiscore);
        }
    }
    if (gn_set_dirty)
        gn_settings_save();
}

static void ui_pick_step(int32_t d)
{
    char t[16];
    int32_t p = clamp((int32_t)ui.pick + d, 0, (int32_t)gn_set.furthest);
    ui.pick = (uint32_t)p;
    t[0] = 0;
    ui_num(t, ui.pick + 1u, 0);
    if (ui.pick)
        ui_cat(t, " PRACTICE");
    ui_say("STAGE", t);
}

void ui_pick_stage(uint32_t stage) { ui.pick = stage < GN_NSTAGES ? stage : GN_NSTAGES - 1u; }

/* ------------------------------------------------------------------ Bluetooth (as the template) --- */
static void ui_ble_toggle(void)          /* REC held a second: off or on, kept with the settings */
{
    gn_set.ble_off = (uint8_t)!gn_set.ble_off;
    gn_set_dirty = 1;
    plat_ble_enable(!gn_set.ble_off);
    ui_say("BLUETOOTH", gn_set.ble_off ? "OFF" : "ON");
}

static void ui_ble_device_say(void)
{
    char name[21];
    if (btdev_found(btdev_pick(), name, 0))
        ui_say("DEVICE", name);
    else
        ui_say("BLE", "SCANNING");
}

static void ui_ble_device_button(void)   /* a REC tap: look for devices, then connect the pick, then let it go */
{
    int ev;
    if (!btdev_busy() && !ui.looking) {
        ui.looking = 1;
        ui_say("BLE", "SCANNING");
        return;
    }
    ev = btdev_save();
    ui.looking = 0;
    ui_say(ev == BTDEV_CONNECTING ? btdev_word(ev) : "BLE", ev == BTDEV_CONNECTING ? btdev_name() : btdev_word(ev));
}

/* ------------------------------------------------------------------ input --- */
static void ui_button(uint32_t k)        /* a button's edge */
{
    uint32_t st = ui.snap.state;
    switch (k) {
    case B_PLAY:
        if (st == GS_TITLE) {
            if (gn_set_dirty)
                gn_settings_save();
            ui.start_stage = ui.pick;
            ui_cmd(CMD_START, ui.pick, gn_set.jam);
        } else if (st == GS_PLAY || (st == GS_PAUSE && ui.snap.countin)) {   /* the count-in looks like play:
                                          * PLAY and HOME pause it again, as in play */
            ui_cmd(CMD_PAUSE, 0, 0);
        } else if (st == GS_PAUSE) {
            ui_cmd(CMD_RESUME, 0, 0);
        } else if (st == GS_OVER) {
            ui_cmd(CMD_START, ui.start_stage, gn_set.jam);
        }
        return;
    case B_HOME:
        if (st == GS_PLAY || (st == GS_PAUSE && ui.snap.countin)) {
            ui_cmd(CMD_PAUSE, 0, 0);
        } else if (st == GS_PAUSE) {
            ui_bank();
            ui_cmd(CMD_STOP, 0, 0);
        } else if (st == GS_OVER) {     /* the run was banked in the silence; a knob since stays dirty until the
                                          * next PLAY: a save now would cut the march */
            ui_cmd(CMD_STOP, 0, 0);
        } else {
            ui_say("BUTTON", "HOME");
        }
        return;
    case B_SEL:
        if (st == GS_TITLE) {
            gn_set.jam ^= 1u;
            gn_set_dirty = 1;
            ui_say("JAM", gn_set.jam ? "ON" : "OFF");
            return;
        }
        break;
    case B_EDIT:
        if (st == GS_TITLE) {
            gn_set.flash_off ^= 1u;
            gn_set_dirty = 1;
            ui_say("FLASH", gn_set.flash_off ? "OFF" : "ON");
            return;
        }
        break;
    case B_OCTDN:
    case B_OCTUP:
        if (st == GS_TITLE) {
            ui_pick_step(k == B_OCTUP ? 1 : -1);
            return;
        }
        break;
    case B_SAVE:
        if (st == GS_TITLE && gn_set_dirty) {
            gn_settings_save();
            return;
        }
        break;
    default:
        break;
    }
    ui_say("BUTTON", UI_BTN_NAME[k]);
}

static void ui_encoder(uint32_t k, int32_t e)
{
    char t[24];
    switch (k) {
    case EN_SELECT:
        if (ui.snap.state == GS_TITLE)
            ui_pick_step(e);
        else
            ui_say("SELECT", "");
        return;
    case EN_K1:
    case EN_K2: {                        /* a twelfth of the range a detent */
        int32_t *acc = k == EN_K1 ? &ui.tone : &ui.space;
        *acc = clamp(*acc + e, -12, 12);
        if (k == EN_K1)
            gn_cmd.tone = (float)*acc * (1.0f / 12.0f);
        else
            gn_cmd.space = (float)*acc * (1.0f / 12.0f);
        t[0] = *acc < 0 ? '-' : '+';
        t[1] = 0;
        ui_num(t, (uint32_t)(*acc < 0 ? -*acc : *acc), 0);
        ui_say(k == EN_K1 ? "TONE" : "SPACE", t);
        return;
    }
    case EN_K3: {                        /* the LEAD patch: the stage's own, then PLUCK SAW SQUARE BELL */
        int32_t v = gn_set.voice == 255u ? 0 : (int32_t)gn_set.voice + 1;
        v = (v + e) % (LP_COUNT + 1);
        if (v < 0)
            v += LP_COUNT + 1;
        gn_set.voice = (uint8_t)(v == 0 ? 255 : v - 1);
        gn_cmd.voice = gn_set.voice;
        gn_set_dirty = 1;
        t[0] = 0;
        if (gn_set.voice == 255u) {
            ui_cat(t, "AUTO ");
            ui_cat(t, UI_LP_NAME[GN_STAGES[ui_stage()].lead % LP_COUNT]);
        } else {
            ui_cat(t, UI_LP_NAME[gn_set.voice]);
        }
        ui_say("VOICE", t);
        return;
    }
    case EN_K4:
        gn_set.hat = (uint8_t)((uint32_t)((int32_t)gn_set.hat + e) & 31u);
        gn_set_dirty = 1;
        ui_hat_palette();
        t[0] = 0;
        ui_num(t, gn_set.hat, 0);
        ui_say("HAT", t);
        return;
    case EN_ALGO:
        if (FM1B_BLE_CENTRAL && ui.looking) {
            btdev_choose(btdev_pick() + (int)e);
            ui_ble_device_say();
            return;
        }
        break;
    default:
        break;
    }
    ui_say(UI_ENC_NAME[k], "");
}

static void ui_input(void)
{
    uint32_t now = ui.now, keys = plat_keys(), btn = plat_buttons(), edges = btn & ~ui.btn, k;
    int32_t e;
    if (keys != ui.keys || edges || game_count(GC_AIR) != ui.air_seen)
        ui.idle_t = now;
    ui.air_seen = game_count(GC_AIR);
    if (ui.snap.state == GS_ATTRACT && edges) {   /* any button leaves the demo (the game leaves on a key) */
        ui_cmd(CMD_STOP, 0, 0);
        ui.rec_t = 0;
    } else {
        for (k = 0; k < NB; k++) {
            if (!(edges >> k & 1u))
                continue;
            if (k == B_REC && FM1B_BLE)
                ui.rec_t = (now - 1u) | 1u;   /* a press acts on release; a hold switches Bluetooth. Never 0 and
                                               * never ahead of now (now | 1 was, and a tap then toggled it) */
            else
                ui_button(k);
        }
    }
    if (FM1B_BLE && ui.rec_t) {
        if (btn >> B_REC & 1u) {
            if (now - ui.rec_t >= 1000u) {
                ui.rec_t = 0;
                ui_ble_toggle();
            }
        } else {
            ui.rec_t = 0;
            if (FM1B_BLE_CENTRAL && !gn_set.ble_off)
                ui_ble_device_button();
            else
                ui_say("BUTTON", "REC");
        }
    }
    for (k = 0; k < NE; k++) {
        if ((e = plat_enc((int)k)) == 0)
            continue;
        ui.idle_t = now;
        if (ui.snap.state == GS_ATTRACT)
            ui_cmd(CMD_STOP, 0, 0);
        else
            ui_encoder(k, e);
    }
    ui.keys = keys;
    ui.btn = btn;
}

void ui_input_only(void)
{
    ui.now = plat_ms();
    ui_input();
}

/* ------------------------------------------------------------------ particles, rings --- */
static void ui_particle(int32_t x, int32_t y, int32_t dx, int32_t dy, uint32_t life, uint8_t c, uint8_t fall)
{
    ui_particle_t *p = &ui.pt[ui.pt_next++ % UI_NPART];   /* round robin: a full table recycles the oldest */
    p->x = (int16_t)(x * 16);
    p->y = (int16_t)(y * 16);
    p->dx = (int16_t)dx;
    p->dy = (int16_t)dy;
    p->life = (uint8_t)life;
    p->c = c;
    p->fall = fall;
}

static void ui_burst(int32_t x, int32_t y, uint32_t n, uint8_t c, int32_t speed)   /* up and out */
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        uint32_t r = ui_rand();
        int32_t dx = ((int32_t)(r & 63u) - 32) * speed / 16, dy = -(int32_t)((r >> 6) & 31u) * speed / 12 - speed / 2;
        ui_particle(x, y, dx, dy, 10u + ((r >> 11) & 7u), c, 1);
    }
}

static void ui_ring_burst(int32_t x, int32_t y, uint32_t n, uint8_t c, int32_t speed, int rainbow)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        uint32_t a = i * 256u / n;
        int32_t dx = speed * gfx_sin((a + 64u) & 255u) / 256, dy = speed * gfx_sin(a) / 256;
        ui_particle(x, y, dx, dy, 14, rainbow ? (uint8_t)(GN_PAL_FX + (i * 32u / n)) : c, (uint8_t)rainbow);
    }
}

static void ui_ring(uint32_t lane)
{
    uint32_t i;
    for (i = 0; i < UI_NRING; i++)
        if (!ui.ring[i].ttl)
            break;
    if (i == UI_NRING)
        i = ui.frame % UI_NRING;
    ui.ring[i].x = (uint8_t)UI_LANE_X(lane);
    ui.ring[i].r = 1;
    ui.ring[i].ttl = 7;
}

/* ------------------------------------------------------------------ events --- */
static void ui_events(void)
{
    gn_event_t e;
    uint32_t n = 0, lv = ui_level(), th = ui_theme();
    while (n++ < GN_EVENTS && game_event_pop(&e)) {
        uint32_t lane = e.a % GN_LANES;
        switch (e.type) {
        case GE_EAT:
            ui_burst(UI_LANE_X(lane), GFX_MOUTH, 8, GN_SPECIES_CAP[e.c % SP_COUNT], 24);
            ui.bite = 6;
            ui.lane_toast[lane] = 20;
            ui.lane_toast_q[lane] = e.b;
            if (e.b == Q_PERFECT && lv >= 7u && ui.now - ui.flash_t >= 500u) {   /* only high on the trip, at most
                                          * twice a second, held two frames (a single-frame flip tears) */
                ui.flash = 2;
                ui.flash_t = ui.now;
            }
            break;
        case GE_MISS:
            ui.wince = 8;
            ui.splat[lane] = 10;
            ui_burst(UI_LANE_X(lane), GFX_MOUTH, 4, GN_C_GREY, 12);
            break;
        case GE_POISON:
            ui.poison = 10;
            ui.burst_wave = 24;
            ui.trip_face = 24;
            ui_burst(UI_LANE_X(lane), GFX_MOUTH, 8, GN_C_GREEN, 20);
            break;
        case GE_AGARIC:
            ui.agaric_until = ui.now + 4u * ui_bar_ms(ui.snap.bpm);
            ui_burst(UI_LANE_X(lane), GFX_MOUTH, 8, GN_C_RED, 28);
            break;
        case GE_STASH:
            ui.pop[e.a % GN_STASH] = 4;
            break;
        case GE_USE:
            if (e.a == AB_BURP) {
                ui.invert = 2;
                ui_ring_burst(ui.gnome_x + 8, GFX_MOUTH, 16, GN_C_WHITE, 40, 0);
            }
            ui_say(UI_AB_NAME[e.a % AB_COUNT], "");
            break;
        case GE_REFUSED:
            ui_say("WAIT", UI_AB_NAME[e.a % AB_COUNT]);
            break;
        case GE_CHAIN:
            ui_say("CHAIN", "+50");
            ui_ring_burst(ui.gnome_x + 8, GFX_MOUTH - 8, 12, GN_C_YELLOW, 24, 1);
            break;
        case GE_BOSS_CHOMP:
            ui_say("CHOMP!", "");
            ui.shake = 6;
            ui.bite = 6;
            ui_ring_burst(60, GFX_FIELD_TOP + 40, 12, GN_C_WHITE, 24, 1);
            break;
        case GE_BOSS_EATEN:
            ui_say("THE BIG ONE", "EATEN");
            ui.shake = 10;
            ui_ring_burst(60, GFX_FIELD_TOP + 40, 32, GN_C_WHITE, 32, 1);
            break;
        case GE_BOSS_LANDED:
            ui.shake = 10;
            ui.wince = 10;
            ui_burst(60, GFX_MOUTH, 8, GN_C_GREY, 20);
            break;
        case GE_SECTION:
            if (e.a == SEC_INTRO) {
                uint32_t st = e.b % GN_NSTAGES, bar = ui_bar_ms(GN_STAGES[st].bpm0);
                char t[32];
                ui.card_stage = (uint8_t)st;
                ui.card_until = ui.now + 2u * bar;
                if (ui.counters_valid)
                    ui.counters_until = ui.card_until;
                if (ann_on()) {
                    t[0] = 0;
                    ui_cat(t, "STAGE ");
                    ui_num(t, st + 1u, 0);
                    ui_cat(t, ". ");
                    ui_cat(t, GN_STAGES[st].name);
                    ann_say(t, 0, ANN_NOW);
                }
            } else if (e.a == SEC_BOSS) {
                ui.boss_card_until = ui.now + ui_bar_ms(ui.snap.bpm);
                if (ann_on())
                    ann_say("THE BIG ONE", 0, ANN_NOW);
            }
            break;
        case GE_OVER:
            if (!ui.over_seen) {
                ui.over_seen = 1;
                ui_bank();
            }
            break;
        case GE_KICK:
            if ((th == VT_EYES || th == VT_CHAOS || ui.snap.round) && e.a == 0u)
                ui.strobe = 2;
            else if (th == VT_EYES || th == VT_CHAOS || ui.snap.round)
                ui.kick = 2;
            break;
        case GE_NOTE:
            ui_ring(lane);
            break;
        case GE_HIGHSCORE:
            ui.newhigh = 1;
            if (!gn_set_dirty)           /* still dirty: the save just failed, and SAVE FAILED must stay up */
                ui_say("NEW HIGH SCORE", "");
            break;
        case GE_CLAM:                    /* a wrong key: OOPS over the lane, the gnome winces */
            ui.lane_toast[lane] = 20;
            ui.lane_toast_q[lane] = Q_NONE;
            ui.wince = 6;
            break;
        case GE_GHOST:
            ui.ghost[lane] = 8;
            ui_burst(UI_LANE_X(lane), GFX_MOUTH, 4, GN_C_LGREY, 16);
            break;
        case GE_WOBBLE:
            ui.lurch = 12;
            ui_say("WOBBLE", "");
            break;
        default:
            break;
        }
    }
}

/* ------------------------------------------------------------------ per-frame state --- */
static void ui_watch(void)               /* state changes (before the frame's events), the attract timer, the CLEAR
                                          * counters, the gnome's walk, the particles */
{
    const gn_snap_t *s = &ui.snap;
    uint32_t i, st = s->state;
    if (st != ui.last_state) {
        if (st == GS_TITLE) {
            ui.idle_t = ui.now;
            ui.looking = 0;
        }
        if ((st == GS_PLAY || st == GS_ATTRACT) && (ui.last_state == GS_TITLE || ui.last_state == GS_OVER)) {
            ui.newhigh = ui.over_seen = ui.counters_valid = 0;
            ui.card_until = ui.boss_card_until = ui.counters_until = ui.agaric_until = 0;
            ui.gnome_x = (int32_t)s->gnome_lane * GFX_LANE_W;
        }
        if (st == GS_OVER && !ui.over_seen) {   /* bank now, the game is silent; GE_OVER, when it comes, does not
                                          * bank again (a failed save would be tried twice and its message lost) */
            ui.over_seen = 1;
            ui_bank();
        }
        if (ann_on()) {                  /* the reader hears where the game went, not only on WHERE */
            if (st == GS_PAUSE && ui.last_state == GS_PLAY)
                ann_say("PAUSED", 0, 0);
            else if (st == GS_PLAY && ui.last_state == GS_PAUSE)
                ann_say("RESUME", 0, 0);
            else if (st == GS_OVER)
                ann_say("YOU CAME DOWN", 0, 0);
            else if (st == GS_TITLE && ui.last_state != GS_ATTRACT)
                ann_say("TITLE", 0, 0);
        }
        ui.last_state = (uint8_t)st;
    }
    if (st == GS_TITLE) {
        if (ui.pick > gn_set.furthest)
            ui.pick = gn_set.furthest;
        if (ui.now - ui.idle_t >= UI_ATTRACT_MS) {
            ui_cmd(CMD_ATTRACT, 0, 1);
            ui.idle_t = ui.now;
        }
    }
    if (s->section == SEC_CLEAR && ui_in_run()) {
        ui.clear_eaten = s->st_eaten;
        ui.clear_missed = s->st_missed;
        ui.clear_best = s->st_best_combo;
        ui.counters_valid = 1;
    }
    if (ui_in_run() || st == GS_OVER) {  /* he dashes a lane a frame */
        int32_t target = (int32_t)s->gnome_lane * GFX_LANE_W;
        if (s->gnome_lane != ui.gnome_lane) {
            ui.gnome_lane = s->gnome_lane;
            ui.dash = 2;
        }
        if (ui.gnome_x < target)
            ui.gnome_x = ui.gnome_x + GFX_LANE_W > target ? target : ui.gnome_x + GFX_LANE_W;
        else if (ui.gnome_x > target)
            ui.gnome_x = ui.gnome_x - GFX_LANE_W < target ? target : ui.gnome_x - GFX_LANE_W;
        if (s->bar_beat != ui.last_beat) {   /* ECHO's ghosts stand where he was one and two beats ago */
            ui.last_beat = s->bar_beat;
            ui.echo_lane[2] = ui.echo_lane[1];
            ui.echo_lane[1] = ui.echo_lane[0];
            ui.echo_lane[0] = s->gnome_lane;
        }
    } else {
        ui.gnome_x = 6 * GFX_LANE_W;
    }
    if (ui_cpu_flag) {
        ui_cpu_flag = 0;
        ui.cpu_until = ui.now + UI_CPU_MS;
    }
    for (i = 0; i < UI_NPART; i++) {
        ui_particle_t *p = &ui.pt[i];
        if (!p->life)
            continue;
        p->life--;
        p->x = (int16_t)(p->x + p->dx);
        p->y = (int16_t)(p->y + p->dy);
        if (p->fall)
            p->dy = (int16_t)(p->dy + 2);
    }
    for (i = 0; i < UI_NRING; i++)
        if (ui.ring[i].ttl) {
            ui.ring[i].ttl--;
            ui.ring[i].r++;
        }
    if (ui_in_run() && s->ability == AB_FREEZE && (ui.frame & 1u))   /* crystals falling */
        ui_particle((int32_t)(ui_rand() % GFX_W), GFX_FIELD_TOP, 0, 6 + (int32_t)(ui_rand() & 7u), 24,
                    (uint8_t)(GN_PAL_FX + (ui.frame & 31u)), 0);
    if (ui_in_run() && s->combo >= 16u)  /* the rainbow trail */
        ui_particle(ui.gnome_x + 2 + (int32_t)(ui_rand() % 12u), GFX_MOUTH + 14, 0, -4 - (int32_t)(ui_rand() & 3u),
                    12, (uint8_t)(GN_PAL_FX + (ui.frame & 31u)), 0);
}

static void ui_decay(void)               /* the frame-counted bursts, after the draw */
{
    uint32_t i;
    if (ui.bite) ui.bite--;
    if (ui.wince) ui.wince--;
    if (ui.dash) ui.dash--;
    if (ui.trip_face) ui.trip_face--;
    if (ui.invert) ui.invert--;
    if (ui.strobe) ui.strobe--;
    if (ui.kick) ui.kick--;
    if (ui.flash) ui.flash--;
    if (ui.poison) ui.poison--;
    if (ui.lurch) ui.lurch--;
    if (ui.shake) ui.shake--;
    if (ui.burst_wave) ui.burst_wave = (uint8_t)(ui.burst_wave > 2u ? ui.burst_wave - 2u : 0u);
    for (i = 0; i < GN_STASH; i++)
        if (ui.pop[i])
            ui.pop[i]--;
    for (i = 0; i < GN_LANES; i++) {
        if (ui.lane_toast[i]) ui.lane_toast[i]--;
        if (ui.splat[i]) ui.splat[i]--;
        if (ui.ghost[i]) ui.ghost[i]--;
    }
    for (i = 0; i < GN_MAX_MUSH; i++) {  /* the trails' previous positions */
        ui.prev_y[i] = ui.snap.mush[i].y256;
        ui.prev_lane[i] = ui.snap.mush[i].lane;
        ui.prev_state[i] = ui.snap.mush[i].state;
    }
}

/* ------------------------------------------------------------------ the post vector --- */
static void ui_post(void)
{
    gfx_post_t *p = &ui.post;
    const gn_snap_t *s = &ui.snap;
    uint32_t th = ui_theme(), lv = ui_level(), dt = ui.dt, run = (uint32_t)ui_in_run();
    uint32_t rate_a = 0, rate_b = 0, rate_fx = 6, wave = 0, freq = 3, zoom = 0, melt = 0, fwave = 0, amt = 0;
    uint32_t r = 0, g = 0, b = 0;
    int32_t bend = 0;
    uint32_t cps_q8 = 256u + 512u * lv / 9u;   /* one wave cycle a second at level 0, three at level 9 */
    ui.wave_ph += dt * cps_q8 * 256u / 1000u;
    ui.zoom_ph += dt * 33u;
    ui.melt_ph += dt * 66u;
    memset(p, 0, sizeof *p);
    p->wave_phase = (uint16_t)ui.wave_ph;
    p->zoom_phase = (uint16_t)ui.zoom_ph;
    p->melt_phase = (uint16_t)ui.melt_ph;
    if (!run) {
        rate_a = 8;
        rate_fx = 4;
        if (s->state == GS_OVER) {
            r = g = b = 128;
            amt = 90;
        }
    } else {
        switch (th) {
        case VT_DUSK:
            wave = 2u + lv;
            p->wave_phase = (uint16_t)(ui.wave_ph >> 1);
            break;
        case VT_NIGHT:
            rate_a = 4;
            wave = 3;
            break;
        case VT_RED:
            wave = 6u + lv;
            freq = 5;
            zoom = 8;
            rate_b = 4;
            break;
        case VT_KALEIDO:
            p->mirror = 1;
            wave = 8;
            freq = 4;
            rate_a = 2;
            break;
        case VT_MIRROR4:
            p->mirror = 2;
            bend = -6;
            wave = 10;
            freq = 4;
            rate_a = 2;
            break;
        case VT_EYES:
            zoom = 8u + 16u * (255u - s->beat_q8) / 255u;
            wave = 10;
            freq = 5;
            rate_a = 2;
            break;
        case VT_CHAOS:
            p->mirror = (uint8_t)((s->bar & 1u) + 1u);
            wave = 16u + lv * 2u;
            freq = 6;
            zoom = 32;
            bend = ((s->bar_beat & 1u) ? 10 : -10) * (int32_t)(255u - s->beat_q8) / 255;
            rate_a = rate_b = rate_fx = 1;
            break;
        default:
            break;
        }
        if (ui.now < ui.agaric_until) {  /* the fly agaric's four bars */
            wave += 12u;
            rate_a = rate_a > 1u ? rate_a / 2u : 1u;
            rate_b = rate_b > 1u ? rate_b / 2u : (rate_b ? 1u : 4u);
            rate_fx = rate_fx > 1u ? rate_fx / 2u : 1u;
        }
        wave += ui.burst_wave;
        if (s->ability == AB_SLOW) {
            melt = 12;
            r = 128, g = 0, b = 160, amt = 60;
        } else if (s->ability == AB_FREEZE) {
            r = 40, g = 120, b = 255, amt = 90;
        } else if (lv <= 1u) {
            r = g = b = 128, amt = 40;
        }
        if (s->ability == AB_SHIMMER)
            rate_fx = 1;
        if (s->boss_on)
            zoom += (255u - s->beat_q8) / 16u;
        if (lv >= 5u)
            fwave = lv - 4u > 3u ? 3u : lv - 4u;
        if (ui.poison)
            r = 40, g = 220, b = 60, amt = 120;
        if (ui.kick && !gn_set.flash_off)
            r = g = b = 255, amt = 77;
        if (ui.flash && !gn_set.flash_off)
            r = g = b = 255, amt = 110;
        if (ui.shake) {
            fwave = 3;
            bend += (ui.frame & 1u) ? 8 : -8;
        }
        if (ui.lurch) {
            fwave = 3;
            bend += (int32_t)ui.lurch * ((ui.frame & 1u) ? 1 : -1);
        }
        p->strobe = (uint8_t)(ui.strobe != 0u);
        p->invert = (uint8_t)(ui.invert != 0u);
    }
    if (rate_a && ui.frame % rate_a == 0u)
        ui.cyc_a = (uint8_t)((ui.cyc_a + 1u) & 31u);
    if (rate_b && ui.frame % rate_b == 0u)
        ui.cyc_b = (uint8_t)((ui.cyc_b + 1u) & 31u);
    if (rate_fx && ui.frame % rate_fx == 0u)
        ui.cyc_fx = (uint8_t)((ui.cyc_fx + 1u) & 31u);
    p->cycle_a = ui.cyc_a;
    p->cycle_b = ui.cyc_b;
    p->cycle_fx = ui.cyc_fx;
    p->wave_amp = (uint8_t)(wave > 24u ? 24u : wave);
    p->wave_freq = (uint8_t)freq;
    p->zoom = (uint8_t)(zoom > 64u ? 64u : zoom);
    p->bend = (int8_t)clamp(bend, -32, 32);
    p->melt = (uint8_t)(melt > 16u ? 16u : melt);
    p->field_wave = (uint8_t)fwave;
    p->tint_r = (uint8_t)r;
    p->tint_g = (uint8_t)g;
    p->tint_b = (uint8_t)b;
    p->tint_amt = (uint8_t)(amt > 255u ? 255u : amt);
    if (gn_set.flash_off) {
        p->strobe = p->invert = 0;
        p->wave_amp /= 2u;
        p->field_wave /= 2u;
    }
}

/* ------------------------------------------------------------------ the background --- */
static void ui_draw_bg(uint32_t th)
{
    uint8_t *bg = gfx_buf(GFX_BG);
    uint32_t y, i, t = ui.now, drift = ui_in_run() ? 0u : t / 400u;
    int32_t base = UI_SKY_BASE[th], span = UI_SKY_SPAN[th];
    memset(bg, GN_C_NIGHT, GFX_FIELD_TOP * GFX_W);
    if (th >= VT_KALEIDO) {              /* a plasma, one sine sum per 2 x 2 block */
        uint32_t bx, by, t1 = t / 8u, t2 = t / 11u, t3 = t / 5u;
        for (by = GFX_FIELD_TOP; by < GFX_MOUTH; by += 2u) {
            uint8_t *row = bg + by * GFX_W;
            for (bx = 0; bx < GFX_W; bx += 2u) {
                int32_t v = gfx_sin((bx * 5u + t1) & 255u) + gfx_sin((by * 7u + t2) & 255u)
                          + gfx_sin(((bx + by) * 3u + t3) & 255u);
                uint8_t c = (uint8_t)(GN_PAL_SKY + (((uint32_t)(v + 768) * 32u / 1537u) & 31u));
                row[bx] = row[bx + 1u] = row[GFX_W + bx] = row[GFX_W + bx + 1u] = c;
            }
        }
    } else {
        for (y = GFX_FIELD_TOP; y < GFX_MOUTH; y++) {
            int32_t k = base + span * (int32_t)(y - GFX_FIELD_TOP) / UI_FIELD_H + (int32_t)drift;
            memset(bg + y * GFX_W, GN_PAL_SKY + ((uint32_t)k & 31u), GFX_W);
        }
    }
    for (y = GFX_MOUTH; y < GFX_H; y++) {   /* the floor he stands on, then the ground row */
        uint32_t k = y < 112u ? 8u + (y - GFX_MOUTH) / 4u : (y - 112u) / 2u;
        memset(bg + y * GFX_W, GN_PAL_GROUND + k, GFX_W);
    }
    for (i = 0; i < GFX_W; i += 5u)      /* grass ticks */
        bg[112u * GFX_W + ((i + (i / 5u) * 3u) % GFX_W)] = GN_PAL_GROUND + 9u;
    gfx_target(GFX_BG);
    if (th >= VT_NIGHT)
        for (i = 0; i < UI_NSTAR; i++)
            if (((t / 300u) + i) % 7u)   /* one in seven twinkles off */
                gfx_sprite(&GN_STAR, ui.star_x[i], ui.star_y[i], 0);
    if (th <= VT_NIGHT) {
        if (th <= VT_DUSK)
            for (i = 0; i < 2u; i++)
                gfx_sprite(&GN_CLOUD, (int32_t)((t / 60u + i * 70u) % 170u) - 30, 16 + (int32_t)i * 14, 0);
        gfx_sprite(&GN_TREE[0], -4, GFX_MOUTH - GN_TREE[0].h, 0);
        gfx_sprite(&GN_TREE[1], GFX_W - GN_TREE[1].w + 4, GFX_MOUTH - GN_TREE[1].h, 0);
        gfx_sprite(&GN_HOUSE, ui_in_run() ? (GFX_W - GN_HOUSE.w) / 2 : 80, GFX_MOUTH - GN_HOUSE.h, 0);
    }
    if (th == VT_EYES)
        for (i = 0; i < UI_NEYE; i++) {  /* blinking on a three-second cycle, each its own moment */
            int closed = (t + i * 500u) % 3000u < 200u;
            gfx_sprite(&GN_EYE[closed], ui.eye_x[i], ui.eye_y[i], 0);
        }
    gfx_target(GFX_FIELD);
}

/* ------------------------------------------------------------------ the field --- */
static void ui_sprite_x2(const gn_sprite_t *sp, int32_t x, int32_t y)   /* a stash icon popping */
{
    int32_t i, j;
    for (j = 0; j < sp->h; j++)
        for (i = 0; i < sp->w; i++) {
            uint8_t c = sp->px[j * sp->w + i];
            if (c)
                gfx_rect(x + 2 * i, y + 2 * j, 2, 2, c);
        }
}

static void ui_draw_gnome(int32_t x)
{
    const gn_snap_t *s = &ui.snap;
    const gn_sprite_t *sp;
    uint32_t lv = ui_level(), i;
    if (s->state == GS_OVER) {
        sp = &GN_GNOME[GNOME_DOWN];
        gfx_sprite(sp, x - 2, 112 - sp->h, 0);
        return;
    }
    if (ui_in_run() && s->ability == AB_ECHO)   /* ghosts a beat and two behind */
        for (i = 1; i < 3u; i++)
            gfx_sprite(&GN_GNOME[GNOME_IDLE0], (int32_t)ui.echo_lane[i] * GFX_LANE_W, UI_GNOME_Y, GFX_GHOST | GFX_DIM);
    for (i = 0; i < GN_LANES; i++)
        if (ui.ghost[i])
            gfx_sprite(&GN_GNOME[GNOME_BITE], (int32_t)i * GFX_LANE_W, UI_GNOME_Y, GFX_GHOST);
    if (ui_in_run() && s->ability == AB_WIDE) {
        gfx_sprite(&GN_GNOME_WIDE, x - 4, UI_GNOME_Y, 0);
        return;
    }
    if (ui.wince)
        sp = &GN_GNOME[GNOME_WINCE];
    else if (ui.bite)
        sp = &GN_GNOME[GNOME_BITE];
    else if (ui.dash)
        sp = &GN_GNOME[GNOME_DASH];
    else if (ui_in_run() && (ui.trip_face || lv >= 6u))
        sp = &GN_GNOME[(ui.frame / 10u) & 1u ? GNOME_TRIP1 : GNOME_TRIP0];
    else if (ui_in_run() && s->lane_next_q8[s->gnome_lane % GN_LANES] < 32u)
        sp = &GN_GNOME[GNOME_OPEN];
    else
        sp = &GN_GNOME[(ui.frame / 20u) & 1u ? GNOME_IDLE1 : GNOME_IDLE0];
    gfx_sprite(sp, x, UI_GNOME_Y, 0);
}

static void ui_sprite_outline(const gn_sprite_t *sp, int32_t x, int32_t y, uint8_t c)
{
    int32_t i, j, w = sp->w, h = sp->h;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            const uint8_t *p = sp->px + j * w + i;
            if (*p && (i == 0 || j == 0 || i == w - 1 || j == h - 1 || !p[-1] || !p[1] || !p[-w] || !p[w]))
                gfx_pixel(x + i, y + j, c);
        }
}

static void ui_draw_mush(const gn_mush_snap_t *m, int32_t y256, uint32_t lane, uint32_t flags)
{
    const gn_sprite_t *sp = &GN_MUSH[m->species % SP_COUNT];
    int32_t x = UI_LANE_X(lane) - sp->w / 2, y = GFX_FIELD_TOP + UI_FIELD_H * y256 / 256 - sp->h / 2;
    if (flags & UI_OUTLINE) {
        ui_sprite_outline(sp, x, y, GN_C_LGREY);
        return;
    }
    gfx_sprite(sp, x, y, flags);
    if (m->state == MS_FROZEN)
        gfx_frame(x - 1, y - 1, sp->w + 2, sp->h + 2, GN_C_CYAN);
}

static void ui_draw_hud(void)
{
    const gn_snap_t *s = &ui.snap;
    uint32_t lv = ui_level(), w;
    char t[16];
    uint8_t c;
    gfx_rect(0, 0, GFX_W, GFX_FIELD_TOP, GN_C_NIGHT);
    t[0] = 0;
    ui_num(t, s->score > 999999u ? 999999u : s->score, 6);
    gfx_text(44 - gfx_text_w(t, GFX_BIG), 1, t, GN_C_WHITE, GFX_BIG);
    gfx_frame(46, 2, 36, 6, GN_C_GREY);
    w = (uint32_t)clamp(s->trip, 0, GN_TRIP_MAX) * 34u / GN_TRIP_MAX;
    c = (uint8_t)(GN_PAL_FX + ((lv * 3u + ui.cyc_fx) & 31u));
    if (s->trip < 200 && (ui.frame / 6u) & 1u)
        c = GN_C_RED;
    if (w)
        gfx_rect(47, 3, (int32_t)w, 4, c);
    if (ui.now < ui.cpu_until)
        gfx_text(84, 2, "CPU", GN_C_RED, 0);
    t[0] = 'S';
    t[1] = 0;
    ui_num(t, ui_stage() + 1u, 0);
    if (s->round)
        ui_cat(t, "+");
    gfx_text(100, 2, t, GN_C_LGREY, 0);
    if (ui.now < ui.msg_until)           /* a message has the line under the HUD */
        return;
    if (s->state == GS_ATTRACT)
        ui_text_c(GFX_FIELD_TOP + 1, "DEMO", GN_C_YELLOW, 0);
    else if (s->practice)
        ui_text_c(GFX_FIELD_TOP + 1, "PRACTICE", GN_C_YELLOW, 0);
}

static void ui_draw_ground_row(void)
{
    const gn_snap_t *s = &ui.snap;
    uint32_t i;
    char t[12];
    for (i = 0; i < GN_STASH; i++) {
        const gn_sprite_t *sp = &GN_ICON[s->stash[i] % AB_COUNT];
        if (ui.pop[i])
            ui_sprite_x2(sp, 1 + 8 * (int32_t)i - 3, 112 - 7);
        else
            gfx_sprite(sp, 1 + 8 * (int32_t)i, 112, 0);
    }
    if (s->ability && s->ability < AB_COUNT) {
        gfx_sprite(&GN_ICON[s->ability], 26, 112, 0);
        if (s->ability_bars)
            gfx_rect(35, 114, 3 * (int32_t)(s->ability_bars > 8u ? 8u : s->ability_bars), 3,
                     (uint8_t)(GN_PAL_FX + ((ui.cyc_fx + 8u) & 31u)));
    }
    if (s->combo >= 4u) {
        t[0] = 'X';
        t[1] = 0;
        ui_num(t, s->combo, 0);
        gfx_text(GFX_W - 1 - gfx_text_w(t, 0), 113, t, s->combo >= 16u ? GN_C_YELLOW : GN_C_WHITE, 0);
    }
}

static void ui_draw_message(void)        /* in play a line under the HUD, never over the mushrooms; else a box
                                          * over the field's middle */
{
    uint32_t big = gfx_text_w(ui.msg[0], GFX_X2) <= GFX_W ? GFX_X2 : 0u;
    int32_t w0 = gfx_text_w(ui.msg[0], big), w1 = ui.msg[1][0] ? gfx_text_w(ui.msg[1], 0) : 0;
    int32_t w = (w0 > w1 ? w0 : w1) + 6, h = ui.msg[1][0] ? 24 : 16, y = 38, x;
    if (ui.now >= ui.msg_until)
        return;
    if (ui_in_run() && ui.snap.state != GS_PAUSE) {
        w0 = gfx_text_w(ui.msg[0], 0);
        w = w0 + (w1 ? 4 + w1 : 0);
        x = (GFX_W - w) / 2;
        gfx_rect(x - 2, GFX_FIELD_TOP, w + 4, 7, GN_C_NIGHT);
        x = gfx_text(x, GFX_FIELD_TOP + 1, ui.msg[0], GN_C_WHITE, 0);
        if (w1)
            gfx_text(x + 4, GFX_FIELD_TOP + 1, ui.msg[1], GN_C_YELLOW, 0);
        return;
    }
    gfx_rect((GFX_W - w) / 2, y, w, h, GN_C_NIGHT);
    gfx_frame((GFX_W - w) / 2, y, w, h, GN_C_DGREY);
    ui_text_c(y + (big ? 2 : 5), ui.msg[0], GN_C_WHITE, big);
    if (ui.msg[1][0])
        ui_text_c(y + 15, ui.msg[1], GN_C_YELLOW, 0);
}

static void ui_draw_play(void)
{
    const gn_snap_t *s = &ui.snap;
    uint32_t i, lv = ui_level(), trails = lv >= 5u || s->ability == AB_SHIMMER;
    int32_t y;
    char t[32];
    gfx_target(GFX_FIELD);
    gfx_clear(0);
    if (ui_theme() >= VT_KALEIDO) {      /* the plasma stages: a dark track behind each lane, every other pixel
                                          * night, so the mushrooms read over the colour; the gaps keep it */
        uint8_t *f = gfx_buf(GFX_FIELD);
        for (y = GFX_FIELD_TOP; y < GFX_MOUTH; y++)
            for (i = 0; i < GN_LANES; i++) {
                int32_t x, x0 = UI_LANE_X(i) - 5;
                for (x = x0 + ((y + x0) & 1); x <= UI_LANE_X(i) + 5; x += 2)
                    f[y * GFX_W + x] = GN_C_NIGHT;
            }
    }
    for (i = 1; i < GN_LANES; i++)
        for (y = GFX_FIELD_TOP; y < GFX_MOUTH; y += 2)
            gfx_pixel((int32_t)i * GFX_LANE_W, y, GN_C_DGREY);
    for (i = 0; i < GFX_W; i += 2u)
        gfx_pixel((int32_t)i, GFX_MOUTH, GN_C_GREY);
    if (ui_stage() <= 2u || gn_set.furthest == 0u)
        for (i = 0; i < GN_LANES; i++) {
            char l[2] = {UI_LANE_LETTER[i], 0};
            gfx_text(UI_LANE_X(i) - gfx_text_w(l, 0) / 2 + 1, 106, l, GN_C_NIGHT, 0);   /* a shadow: the floor
                                          * ramp under them changes with the theme */
            gfx_text(UI_LANE_X(i) - gfx_text_w(l, 0) / 2, 105, l, GN_C_LGREY, 0);
        }
    if (s->ability == AB_ARP)            /* lasers up the lanes in time with the beat */
        for (i = 0; i < GN_LANES; i++) {
            int32_t top = GFX_MOUTH - (int32_t)(s->beat_q8 * UI_FIELD_H / 256u);
            gfx_vline(UI_LANE_X(i), top, 10, (uint8_t)(GN_PAL_FX + ((i * 4u + ui.cyc_fx) & 31u)));
        }
    if (s->ability == AB_CHORD)          /* three beams: the pressed lane, a third and a fifth up */
        for (i = 0; i < 3u; i++) {
            uint32_t lane = (s->gnome_lane + 2u * i) % GN_LANES;
            for (y = GFX_FIELD_TOP + (int32_t)(ui.frame & 3u); y < GFX_MOUTH; y += 4)
                gfx_pixel(UI_LANE_X(lane), y, (uint8_t)(GN_PAL_FX + ((i * 10u + ui.cyc_fx) & 31u)));
        }
    for (i = 0; i < GN_MAX_MUSH; i++) {
        const gn_mush_snap_t *m = &s->mush[i];
        uint32_t flags = 0;
        if (m->state == MS_FREE || m->state == MS_EATEN)
            continue;
        if (m->flags & GN_SPAWN_AUTO)
            flags = UI_OUTLINE;
        else if (m->state == MS_MISSED)
            flags = GFX_DIM;
        if (trails && !(flags & UI_OUTLINE) && ui.prev_state[i] == MS_FALLING && m->state == MS_FALLING && ui.prev_lane[i] == m->lane
            && ui.prev_y[i] < m->y256)
            ui_draw_mush(m, ui.prev_y[i], m->lane, flags | GFX_DIM);
        ui_draw_mush(m, m->y256, m->lane, flags);
    }
    if (s->boss_on) {                    /* centred over its three lanes, each marked at the mouth line */
        const gn_sprite_t *sp = &GN_BOSS[s->boss_bites > 2u ? 2u : s->boss_bites];
        int32_t cx = 0, bottom = GFX_FIELD_TOP + UI_FIELD_H * s->boss_y256 / 256;
        for (i = 0; i < 3u; i++) {
            int32_t lx = UI_LANE_X(s->boss_lanes[i] % GN_LANES);
            cx += lx;
            gfx_vline(lx, bottom, GFX_MOUTH - bottom, (uint8_t)(GN_PAL_FX + ((ui.cyc_fx + i * 8u) & 31u)));
            gfx_rect(lx - 2, GFX_MOUTH - 3, 5, 3, GN_C_YELLOW);
        }
        gfx_sprite(sp, cx / 3 - sp->w / 2, bottom - sp->h, 0);
    }
    ui_draw_gnome(ui.gnome_x);
    for (i = 0; i < GN_LANES; i++)
        if (ui.splat[i])
            gfx_sprite(&GN_SPLAT, UI_LANE_X(i) - GN_SPLAT.w / 2, GFX_MOUTH - 2, 0);
    for (i = 0; i < UI_NRING; i++)
        if (ui.ring[i].ttl)
            gfx_circle(ui.ring[i].x, GFX_MOUTH, ui.ring[i].r, GN_C_DGREY);
    for (i = 0; i < UI_NPART; i++) {
        const ui_particle_t *p = &ui.pt[i];
        int32_t px = p->x / 16, py = p->y / 16;
        if (p->life && py >= GFX_FIELD_TOP && py < GFX_H - 1)
            gfx_rect(px, py, 2, 2, p->c);
    }
    ui_draw_hud();
    ui_draw_ground_row();
    for (i = 0; i < GN_LANES; i++)
        if (ui.lane_toast[i]) {
            uint32_t qq = ui.lane_toast_q[i];
            const char *q = qq == Q_PERFECT ? "PERFECT" : qq == Q_GOOD ? "GOOD" : "OOPS";
            int32_t x = clamp(UI_LANE_X(i) - gfx_text_w(q, 0) / 2, 0, GFX_W - gfx_text_w(q, 0));
            gfx_text(x, 84 - (20 - (int32_t)ui.lane_toast[i]) / 2, q,
                     qq == Q_PERFECT ? GN_C_WHITE : qq == Q_GOOD ? GN_C_YELLOW : GN_C_RED, 0);
        }
    if (ui.now < ui.card_until && s->section == SEC_INTRO) {   /* the title card */
        const char *name = GN_STAGES[ui.card_stage % GN_NSTAGES].name;
        t[0] = 0;
        ui_cat(t, "STAGE ");
        ui_num(t, ui.card_stage + 1u, 0);
        ui_text_c(34, t, GN_C_WHITE, GFX_X2);
        ui_text_c(50, name, (uint8_t)(GN_PAL_FX + ui.cyc_fx), gfx_text_w(name, GFX_X2) <= GFX_W ? GFX_X2 : 0u);
    } else if (ui.now < ui.boss_card_until && s->section == SEC_BOSS) {
        ui_text_c(40, "THE BIG ONE", GN_C_RED, GFX_X2);
    } else if (s->section == SEC_CLEAR && ui.now >= ui.card_until) {
        ui_text_c(34, "CLEAR", GN_C_YELLOW, GFX_X2);
    }
    if (s->section == SEC_CLEAR || (ui.counters_valid && ui.now < ui.counters_until)) {
        t[0] = 0;
        ui_cat(t, "EATEN ");
        ui_num(t, s->section == SEC_CLEAR ? s->st_eaten : ui.clear_eaten, 0);
        ui_text_c(66, t, GN_C_WHITE, 0);
        t[0] = 0;
        ui_cat(t, "MISSED ");
        ui_num(t, s->section == SEC_CLEAR ? s->st_missed : ui.clear_missed, 0);
        ui_text_c(74, t, GN_C_LGREY, 0);
        t[0] = 0;
        ui_cat(t, "BEST COMBO ");
        ui_num(t, s->section == SEC_CLEAR ? s->st_best_combo : ui.clear_best, 0);
        ui_text_c(82, t, GN_C_LGREY, 0);
    }
    if (s->state == GS_PAUSE && !s->countin) {   /* the field dimmed: every other pixel to night */
        uint8_t *f = gfx_buf(GFX_FIELD);
        uint32_t x;
        for (y = GFX_FIELD_TOP; y < 112; y++)
            for (x = (uint32_t)y & 1u; x < GFX_W; x += 2u)
                f[(uint32_t)y * GFX_W + x] = GN_C_NIGHT;
        ui_text_c(42, "PAUSED", GN_C_WHITE, GFX_X2);
        ui_text_c(60, "PLAY RESUME  HOME QUIT", GN_C_LGREY, 0);
    }
}

static void ui_draw_countin(void)        /* the resume's count, 4 3 2 1, over everything */
{
    char t[4];
    if (!ui_in_run() || !ui.snap.countin)
        return;
    t[0] = 0;
    ui_num(t, ui.snap.countin, 0);
    ui_text_c(40, t, GN_C_WHITE, GFX_BIG | GFX_X2);
}

static void ui_draw_title(void)
{
    char t[32];
    gfx_target(GFX_FIELD);
    gfx_clear(0);
    gfx_sprite(&GN_LOGO, (GFX_W - GN_LOGO.w) / 2, 14, 0);
    ui_draw_gnome(ui.gnome_x);
    if ((ui.frame / 24u) & 1u)
        ui_text_c(42, "PRESS PLAY", GN_C_WHITE, GFX_X2);
    t[0] = 0;
    ui_cat(t, "HIGH SCORE ");
    ui_num(t, gn_set.hiscore > 999999u ? 999999u : gn_set.hiscore, 6);
    ui_text_c(58, t, GN_C_LGREY, 0);
    t[0] = 0;
    ui_cat(t, "STAGE ");
    ui_num(t, ui.pick + 1u, 0);
    if (ui.pick)
        ui_cat(t, " PRACTICE");
    ui_cat(t, gn_set.jam ? "  JAM ON" : "  JAM OFF");
    ui_text_c(66, t, GN_C_WHITE, 0);
    if (gn_set.flash_off)
        ui_text_c(74, "FLASH OFF", GN_C_GREY, 0);
    gfx_text(2, 100, "SEL JAM  OCT STAGE", GN_C_GREY, 0);
    gfx_text(2, 108, "EDIT FLASH", GN_C_GREY, 0);
}

static void ui_draw_over(void)
{
    const gn_snap_t *s = &ui.snap;
    char t[24];
    gfx_target(GFX_FIELD);
    gfx_clear(0);
    ui_text_c(24, "YOU CAME DOWN", GN_C_RED, GFX_X2);
    ui_draw_gnome(ui.gnome_x);
    t[0] = 0;
    ui_cat(t, "SCORE ");
    ui_num(t, s->score > 999999u ? 999999u : s->score, 6);
    ui_text_c(46, t, GN_C_WHITE, 0);
    t[0] = 0;
    ui_cat(t, "STAGE ");
    ui_num(t, ui_stage() + 1u, 0);
    if (s->practice)
        ui_cat(t, " PRACTICE");
    ui_text_c(56, t, GN_C_LGREY, 0);
    if (ui.newhigh)
        ui_text_c(68, "NEW HIGH SCORE", (uint8_t)(GN_PAL_FX + ui.cyc_fx), 0);
    ui_text_c(114, "PLAY AGAIN  HOME TITLE", GN_C_GREY, 0);
}

/* ------------------------------------------------------------------ the lights --- */
static void ui_lights(void)
{
    const gn_snap_t *s = &ui.snap;
    uint32_t btn = 0, keys = 0, mid_btn = 0, mid_keys = 0, dim_btn = 0, i;
    int glow = 0;
    if (ui_in_run()) {
        for (i = 0; i < GN_LANES; i++) {
            uint32_t m = ui.lane_keys[i], q = s->lane_next_q8[i];
            uint32_t thr = 1024u / GN_STAGES[ui_stage()].fall_q;
            if (q == 255u)
                continue;
            if (q >= (thr > 128u ? 128u : thr))
                mid_keys |= m;
            else if (!s->lane_poison[i] || (ui.now / 125u) & 1u)
                keys |= m;
        }
        if (s->boss_on)
            for (i = 0; i < 3u; i++) {
                uint32_t m = ui.lane_keys[s->boss_lanes[i] % GN_LANES];
                if (s->beat_q8 < 64u)
                    keys |= m;
                else
                    mid_keys |= m;
            }
        for (i = 0; i < GN_STASH; i++)
            if (s->stash[i])
                keys |= 1u << UI_BKEY[i];
        btn |= s->trip < 200 ? 1u << B_PLAY : LED_PLAY_GREEN;
        plat_play_red(s->trip < 200);
    } else {
        glow = 1;
        dim_btn = UI_BTN_ALL & ~(1u << B_PLAY);
        if (s->state == GS_TITLE)
            mid_btn |= 1u << B_OCTDN | 1u << B_OCTUP;
        else
            mid_btn |= 1u << B_PLAY | 1u << B_HOME;
        if (gn_set_dirty)
            btn |= 1u << B_SAVE;
        plat_play_red(0);
    }
    if (ui.rec_t && ui.now - ui.rec_t >= 300u && (ui.now / 150u) & 1u)   /* the hold counting */
        btn |= 1u << B_REC;
    plat_lights(btn, keys, dim_btn, 0, glow);
    plat_lights_mid(mid_btn, mid_keys & ~keys);
}

/* ------------------------------------------------------------------ the frame --- */
void ui_frame(void)
{
    uint32_t now = plat_ms(), st;
    ui.dt = now - ui.frame_t > 100u ? 100u : now - ui.frame_t;
    ui.frame_t = ui.now = now;
    ui.frame++;
    game_snapshot(&ui.snap);
    ui_input();
    ui_watch();
    ui_events();
    st = ui.snap.state;
    ui_post();
    ui_draw_bg(ui_theme());
    if (st == GS_TITLE)
        ui_draw_title();
    else if (st == GS_OVER)
        ui_draw_over();
    else
        ui_draw_play();
    ui_draw_message();
    ui_draw_countin();
    gfx_present(&ui.post);
    ui_decay();
    ui_lights();
    ui_speak();
    if (FM1B_BLE_CENTRAL) {              /* looking: a device found is named at once (REC connects to it) */
        static int found;
        int n = ui.looking ? btdev_count() : 0, ev;
        btdev_page(ui.looking && !gn_set.ble_off);
        if (n > found && btdev_pick() < n)
            ui_ble_device_say();
        found = n;
        while ((ev = btdev_event()) != BTDEV_NONE)
            if (ev == BTDEV_CONNECTED || ev == BTDEV_NO_ANSWER || ev == BTDEV_LOST)
                ui_say("DEVICE", btdev_word(ev));
    }
}

void ui_overload(void) { ui_cpu_flag = 1; }

void ui_init(void)
{
    uint32_t i, w;
    memset(&ui, 0, sizeof ui);
    gfx_init();
    gfx_palette(GN_PAL, GN_DIM);
    ui_hat_palette();
    ui.rng = 0x9E3779B9u;
    for (i = 0; i < UI_NSTAR; i++) {
        ui.star_x[i] = (uint8_t)(ui_rand() % (GFX_W - 3u));
        ui.star_y[i] = (uint8_t)(GFX_FIELD_TOP + ui_rand() % 56u);
    }
    for (i = 0; i < UI_NEYE; i++) {
        ui.eye_x[i] = (uint8_t)(4u + i * 22u + ui_rand() % 8u);
        ui.eye_y[i] = (uint8_t)(GFX_FIELD_TOP + 6u + ui_rand() % 50u);
    }
    for (w = 0; w < GN_NWHITE; w++)
        if (UI_WLANE[w] != GN_NO_LANE)
            ui.lane_keys[UI_WLANE[w]] |= 1u << UI_WKEY[w];
    ui.now = ui.frame_t = ui.idle_t = plat_ms();
    ui.btn = plat_buttons();
    ui.keys = plat_keys();
    game_snapshot(&ui.snap);
    ui.last_state = ui.snap.state;
    ui.gnome_lane = ui.snap.gnome_lane;
    ui.gnome_x = 6 * GFX_LANE_W;
    gn_cmd.voice = gn_set.voice;
}

/* the boot screen (APP_SPLASH), before app_init's work: the logo on a dark sky and the tagline */
void app_splash(void)
{
    gfx_post_t post;
    uint8_t *bg;
    uint32_t y;
    memset(&post, 0, sizeof post);
    gfx_init();
    gfx_palette(GN_PAL, GN_DIM);
    bg = gfx_buf(GFX_BG);
    for (y = 0; y < GFX_H; y++)
        memset(bg + y * GFX_W, GN_PAL_SKY + y * 6u / GFX_H, GFX_W);
    gfx_target(GFX_FIELD);
    gfx_clear(0);
    gfx_sprite(&GN_LOGO, (GFX_W - GN_LOGO.w) / 2, 44, 0);
    ui_text_c(72, "A GNOME EATS MUSHROOMS", GN_C_CREAM, 0);
    gfx_present(&post);
}
