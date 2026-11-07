/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann */
/* GNOMNOM on fm1-base: the app's unity build (compiled into fm1-base's, after the platform) and the glue between
 * fm1-base's contract (app.h) and the parts: the renderer (gfx.c), the tunes and stages (tunes.c), the composer
 * (music.c), the rules (game.c), the screen and controls (ui.c), and the band (engine/band.c, its own -O2 unit). */
#include "gn.h"
#include "fastmath.h"

gn_cmd_t gn_cmd;
gn_set_t gn_set;
uint8_t gn_set_dirty;

#include "gfx.c"
#include "tunes.c"
#include "music.c"
#include "game.c"
#include "ui.c"

static uint32_t gn_arena[BAND_ARENA_BYTES / 4u] PLAT_POOL __attribute__((aligned(32)));   /* cache lines: core 2's hall */

/* ---- MIDI out (render context: the band and the composer call these) */
void gn_midi_note(uint32_t ch, uint32_t note, uint32_t vel, int on)
{
    uint32_t st = (on ? 0x90u : 0x80u) | (ch & 15u);
    if (on) {
        uint32_t room = plat_midi_out_room(TO_USB | TO_BLE);
        if (room == 0u || room < 8u)         /* nobody listens, or the room is kept for the offs owed */
            return;
    }
    plat_midi_send((st >> 4) | st << 8 | (note & 127u) << 16 | (on ? vel & 127u : 0u) << 24, TO_USB | TO_BLE);
}
void gn_midi_realtime(uint32_t st) { plat_midi_send(0x0Fu | st << 8, TO_USB | TO_BLE); }

/* ---- MIDI in (render context): notes play like keys, two controllers are honoured */
static void midi_in(void)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t full = (pkt >> 8) & 0xFFu, st = full & 0xF0u, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        if (full >= 0xF0u)
            continue;
        switch (st) {
        case 0x90u: game_midi_key(d1, d2, d2 != 0u); break;
        case 0x80u: game_midi_key(d1, 0, 0); break;
        case 0xB0u:
            if (d1 == 120u)
                band_panic();
            else if (d1 == 123u)
                band_all_off();
            break;
        default: break;
        }
    }
}

/* ---- the settings */
static void settings_defaults(void)
{
    gn_set.magic = GN_SET_MAGIC;
    gn_set.hiscore = 0;
    gn_set.furthest = 0;
    gn_set.jam = 0;
    gn_set.hat = 0;
    gn_set.ble_off = 0;
    gn_set.voice = 255;
    gn_set.flash_off = 0;
    gn_set.rsv[0] = gn_set.rsv[1] = 0;
}
int gn_ble_off(void) { return gn_set.ble_off; }
void gn_settings_save(void)
{
    if (plat_store_save(OBJ_SETTINGS, &gn_set, sizeof gn_set) == 0) {
        gn_set_dirty = 0;
        ui_say("SAVED", "");
    } else {
        ui_say("SAVE FAILED", "");
    }
}

void app_overload(void) { ui_overload(); }

/* ---- the contract */
void app_init(void)
{
    if (plat_store_load(OBJ_SETTINGS, &gn_set, sizeof gn_set) != (int)sizeof gn_set || gn_set.magic != GN_SET_MAGIC
        || gn_set.furthest >= GN_NSTAGES || gn_set.jam > 1u || gn_set.hat > 31u || gn_set.ble_off > 1u
        || gn_set.flash_off > 1u || (gn_set.voice != 255u && gn_set.voice >= LP_COUNT))
        settings_defaults();
    band_init(gn_arena, sizeof gn_arena);
#if FM1B_CPU2
    plat_cpu2_set(1);                        /* the hall renders a block behind on the second core; one core if it fails */
#endif
    band_midi_out(gn_midi_note);
    music_init();
    game_init(0x474E4F4Du);
    game_hiscore(gn_set.hiscore);
    music_title();                           /* the music box plays from the first block */
    gn_cmd.cmd = CMD_NONE;
    gn_cmd.stage = 0;
    gn_cmd.jam = gn_set.jam;
    gn_cmd.seed = 0x474E4F4Du;               /* a fixed seed: the simulator reproduces a run */
    gn_cmd.tone = gn_cmd.space = 0.0f;
    gn_cmd.voice = gn_set.voice;
}
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    midi_in();
    game_tick(n);
    band_render(lr, n, master_q12);
}
void app_panic(void) { band_panic(); }
void app_ui_init(void) { ui_init(); }
void app_frame(void) { ui_frame(); }
void app_input_only(void) { ui_input_only(); }
void app_say(const char *a, const char *b) { ui_say(a, b); }

#ifdef PLAT_HOST
/* the simulator's checks (docs/DESIGN.md section 6) */
int app_expect(const char *what, int *got)
{
    static gn_snap_t s;
    uint32_t i, lane = 0;
    game_snapshot(&s);
    if (!strcmp(what, "state"))
        *got = (int)s.state;
    else if (!strcmp(what, "section"))
        *got = (int)s.section;
    else if (!strcmp(what, "stage"))
        *got = (int)s.stage;
    else if (!strcmp(what, "round"))
        *got = (int)s.round;
    else if (!strcmp(what, "score"))
        *got = (int)s.score;
    else if (!strcmp(what, "trip"))
        *got = (int)s.trip;
    else if (!strcmp(what, "triplevel"))
        *got = (int)s.trip_level;
    else if (!strcmp(what, "combo"))
        *got = (int)s.combo;
    else if (!strcmp(what, "hiscore"))
        *got = (int)s.hiscore;
    else if (!strcmp(what, "jam"))
        *got = (int)s.jam;
    else if (!strcmp(what, "practice"))
        *got = (int)s.practice;
    else if (!strcmp(what, "bpm"))
        *got = (int)s.bpm;
    else if (!strcmp(what, "bar"))
        *got = (int)s.bar;
    else if (!strcmp(what, "gnomelane"))
        *got = (int)s.gnome_lane;
    else if (!strcmp(what, "ability"))
        *got = (int)s.ability;
    else if (!strcmp(what, "abilitybars"))
        *got = (int)s.ability_bars;
    else if (!strcmp(what, "bosson"))
        *got = (int)s.boss_on;
    else if (!strcmp(what, "bossbites"))
        *got = (int)s.boss_bites;
    else if (!strcmp(what, "auto"))
        *got = (int)(s.auto_left > 0x7FFFFFFFu ? 0x7FFFFFFF : s.auto_left);
    else if (!strcmp(what, "countin"))
        *got = (int)s.countin;
    else if (!strncmp(what, "stash", 5))
        *got = (int)s.stash[(uint32_t)atoi(what + 5) % GN_STASH];
    else if (!strcmp(what, "mush")) {        /* mushrooms falling (or frozen) now, AUTO notes left out */
        *got = 0;
        for (i = 0; i < GN_MAX_MUSH; i++)
            *got += (s.mush[i].state == MS_FALLING || s.mush[i].state == MS_FROZEN) && !(s.mush[i].flags & GN_SPAWN_AUTO);
    } else if (!strcmp(what, "eaten"))
        *got = (int)game_count(GC_EATEN);
    else if (!strcmp(what, "missed"))
        *got = (int)game_count(GC_MISSED);
    else if (!strcmp(what, "poisoned"))
        *got = (int)game_count(GC_POISONED);
    else if (!strcmp(what, "perfect"))
        *got = (int)game_count(GC_PERFECT);
    else if (!strcmp(what, "spawned"))
        *got = (int)game_count(GC_SPAWNED);
    else if (!strcmp(what, "air"))
        *got = (int)game_count(GC_AIR);
    else if (!strcmp(what, "chains"))
        *got = (int)game_count(GC_CHAINS);
    else if (!strcmp(what, "chomps"))
        *got = (int)game_count(GC_BOSS_CHOMPS);
    else if (!strcmp(what, "clams"))
        *got = (int)game_count(GC_CLAMS);
    else if (!strcmp(what, "evdropped"))
        *got = (int)game_count(GC_EV_DROPPED);
    else if (!strcmp(what, "nexthit"))
        *got = (int)game_next_hit_ms(&lane);
    else if (!strcmp(what, "nextlane"))
        *got = game_next_hit_ms(&lane) < 0 ? -1 : (int)lane;
    else if (!strcmp(what, "voices"))
        *got = (int)band_voices();
    else if (!strcmp(what, "peakout"))      /* the band's output peak, last block, in 1/1000 */
        *got = (int)(band_peak(PART_COUNT + 1u) * 1000.0f);
    else if (!strncmp(what, "peak", 4) && what[4] >= '0' && what[4] <= '9')   /* peakN: part N's (PART_COUNT the drums) */
        *got = (int)(band_peak((uint32_t)atoi(what + 4)) * 1000.0f);
    else if (!strcmp(what, "furthest"))
        *got = (int)gn_set.furthest;
    else if (!strcmp(what, "hat"))
        *got = (int)gn_set.hat;
    else if (!strcmp(what, "flash"))
        *got = (int)gn_set.flash_off;
    else if (!strcmp(what, "dirty"))
        *got = (int)gn_set_dirty;
    else
        return 0;
    return 1;
}
/* the simulator's commands: gn auto N | gn autooff | gn demo 0|1 | gn stage N (1..8 as printed: the title's start
 * pick, past the furthest reached) | gn seed N (the next run's RNG seed) */
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)c, (void)d;
    if (strcmp(cmd, "gn"))
        return 0;
    if (!strcmp(a, "auto"))
        game_auto((uint32_t)strtoul(b, 0, 0));
    else if (!strcmp(a, "autooff"))
        game_auto(0);
    else if (!strcmp(a, "demo"))
        game_auto_demo(atoi(b) != 0);
    else if (!strcmp(a, "stage")) {
        uint32_t s = (uint32_t)atoi(b);
        if (s < 1u || s > GN_NSTAGES)
            return 0;
        if (s - 1u > gn_set.furthest)
            gn_set.furthest = (uint8_t)(s - 1u);
        ui_pick_stage(s - 1u);
    } else if (!strcmp(a, "seed"))
        gn_cmd.seed = (uint32_t)strtoul(b, 0, 0);
    else
        return 0;
    return 1;
}
#endif
