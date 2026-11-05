/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann */
/* GNOMNOM's rules, in the render: the keys read each block and judged against the mushroom table at the song's
 * clock, the trip meter, score and combo, the stash and its abilities, The Big One, the count-in, the autoplayer,
 * and what crosses to the UI (the snapshot seqlock, the event ring) and back (gn_cmd). music.c spawns, this file
 * eats. Included by app.c after music.c; everything here but the game_* functions is static and gm_-prefixed. */

#define GM_NONE 0xFFFFFFFFu
#define GM_REG_OWN 128                   /* an eat with no key register: the note sounds as the spawn gave it */
#define GM_GHOSTS 8u
#define GM_PEND 8u

static const uint8_t GM_WKEY_LANE[GN_NWHITE] = GN_WKEY_LANE;
static const int8_t GM_WKEY_REG[GN_NWHITE] = GN_WKEY_REG;
static const uint8_t GM_WKEY_LETTER[GN_NWHITE] = GN_WKEY_LETTER;
static const uint8_t GM_WHITE[GN_NWHITE] = GN_WHITE_KEYS;
static const uint8_t GM_BLACK[GN_NBLACK] = GN_BLACK_KEYS;
static const uint8_t GM_BLACK_LEFT[GN_NBLACK] = GN_BLACK_LEFT;
static const int8_t GM_BLACK_REG[GN_NBLACK] = GN_BLACK_REG;
static const uint8_t GM_SP_ABILITY[SP_COUNT] = GN_SPECIES_ABILITY;
static const uint8_t GM_AB_BARS[AB_COUNT] = GN_ABILITY_BARS;
static const int8_t GM_PC_LANE[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};   /* pitch class -> lane, -1 black */
static const int8_t GM_PC_LEFT[12] = {-1, 0, -1, 1, -1, -1, 3, -1, 4, -1, 5, -1}; /* a black pitch class: the lane left */

typedef struct {
    uint8_t lane, species, state, chain, flags;
    uint8_t note;
    uint8_t done;                        /* an AUTO note has sounded (or stayed silent) at its hit */
    uint8_t hold;                        /* MS_EATEN: blocks left */
    uint32_t hit, fall;
    int16_t y256;
} gm_mush_t;

typedef struct { uint8_t lane, reg, kind; } gm_pend_t;   /* a MIDI press waiting for the block's judging */

static gm_mush_t gm_mush[GN_MAX_MUSH];
static gn_snap_t gm_snap;
static volatile uint32_t gm_seq;
static gn_event_t gm_ev[GN_EVENTS];
static volatile uint32_t gm_ev_w, gm_ev_r;
static volatile uint32_t gm_state_v = GS_TITLE;
static volatile uint32_t gm_cnt[GC_COUNT];
static volatile uint32_t gm_auto_left, gm_auto_demo, gm_hiscore_saved, gm_snap_fall;

static uint32_t gm_stage, gm_round, gm_jam, gm_practice, gm_section;
static int32_t gm_trip;
static uint32_t gm_score, gm_combo, gm_st_eaten, gm_st_missed, gm_st_best_combo;
static uint8_t gm_stash[GN_STASH];
static uint32_t gm_ability, gm_ab_end, gm_ab_samples;
static float gm_base_pitch, gm_base_filter, gm_base_detune, gm_base_shimmer, gm_base_dly_div, gm_base_dly_fdbk,
             gm_base_dly_send;
static uint32_t gm_keys_last, gm_btn_last, gm_rng, gm_beat_last, gm_bar_seen, gm_sec_seen, gm_bar_tick;
static uint32_t gm_gnome_lane, gm_last_press, gm_last_melody_eaten, gm_voice_last = 255u;
static int32_t gm_gnome_reg;
static uint32_t gm_countin, gm_countin_left, gm_countin_spb, gm_hold_pause;
static uint32_t gm_chain_cur, gm_chain_n, gm_chain_broken;
static uint32_t gm_ghost_tick[GM_GHOSTS], gm_ghost_n;
static uint8_t gm_ghost_lane[GM_GHOSTS];
static gm_pend_t gm_pend[GM_PEND];
static uint32_t gm_pend_n;
static uint32_t gm_demo_bar_last;
static struct {
    uint32_t on, hit, bites, touch, landed, ptick[3];
    uint8_t lanes[3];
    int16_t y256;
} gm_boss;

static uint32_t gm_rnd(void)
{
    uint32_t x = gm_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    gm_rng = x;
    return x;
}
static int32_t gm_clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static uint32_t gm_absdiff(uint32_t a, uint32_t b) { return a > b ? a - b : b - a; }
static uint32_t gm_ms_ticks(uint32_t ms) { return (uint32_t)(music_ms_ticks((float)ms) + 0.5f); }
static uint32_t gm_lane_keys(uint32_t lane)
{
    uint32_t w, m = 0;
    for (w = 0; w < GN_NWHITE; w++)
        if (GM_WKEY_LANE[w] == lane)
            m |= 1u << GM_WHITE[w];
    return m;
}

void game_event_push(uint32_t type, uint32_t a, uint32_t b, uint32_t c)
{
    gn_event_t e;
    if (gm_ev_w - gm_ev_r >= GN_EVENTS) {
        gm_cnt[GC_EV_DROPPED]++;
        return;
    }
    e.type = (uint8_t)type;
    e.a = (uint8_t)a;
    e.b = (uint8_t)b;
    e.c = (uint8_t)c;
    gm_ev[gm_ev_w & (GN_EVENTS - 1u)] = e;
    GN_BARRIER();
    gm_ev_w++;
}
int game_event_pop(gn_event_t *e)
{
    if (gm_ev_r == gm_ev_w)
        return 0;
    *e = gm_ev[gm_ev_r & (GN_EVENTS - 1u)];
    GN_BARRIER();
    gm_ev_r++;
    return 1;
}

/* the bar's first tick, from the bar count changing: bar_beat survives the IDM 7/8 bar, whose last beat is short */
static uint32_t gm_bar_beat(void)
{
    uint32_t now = music_now(), sec = music_section(), bar = music_bar(), b;
    if (sec != gm_sec_seen || bar != gm_bar_seen) {
        gm_sec_seen = sec;
        gm_bar_seen = bar;
        gm_bar_tick = now;
    }
    b = (now - gm_bar_tick) / GN_PPQ;
    return b > 3u ? 3u : b;
}

static uint32_t gm_mult(void)
{
    uint32_t cm = 1u + gm_combo / 8u, sm = gm_round ? 8u : gm_stage + 1u;
    if (cm > 4u)
        cm = 4u;
    return cm * sm * (gm_ability == AB_SHIMMER ? 2u : 1u);
}
static void gm_trip_add(int32_t d) { gm_trip = gm_clampi(gm_trip + d, 0, GN_TRIP_MAX); }
static uint32_t gm_trip_level(void) { return (uint32_t)gm_clampi(gm_trip, 0, 999) / 100u; }

/* ---- the table and the windows */
static uint32_t gm_lane_gap(uint32_t i)   /* ticks to the nearest other mushroom in the lane, GM_NONE none */
{
    uint32_t j, best = GM_NONE;
    for (j = 0; j < GN_MAX_MUSH; j++) {
        const gm_mush_t *m = &gm_mush[j];
        uint32_t d;
        if (j == i || m->state == MS_FREE || m->lane != gm_mush[i].lane || (m->flags & GN_SPAWN_AUTO))
            continue;
        d = gm_absdiff(m->hit, gm_mush[i].hit);
        if (d < best)
            best = d;
    }
    return best;
}
static void gm_windows(uint32_t i, uint32_t *good, uint32_t *perfect)
{
    uint32_t g = gm_ms_ticks(GN_WIN_GOOD_MS), p = gm_ms_ticks(GN_WIN_PERFECT_MS), gap = gm_lane_gap(i);
    if (gap != GM_NONE) {
        uint32_t cap = gap * 45u / 100u;
        if (g > cap)
            g = cap;
        if (p > cap)
            p = cap;
    }
    *good = g;
    *perfect = p;
}
static uint32_t gm_eat_note(const gm_mush_t *m, int32_t reg)
{
    int32_t d, oct;
    if (reg == GM_REG_OWN)
        return m->note;
    d = (int32_t)m->note - (int32_t)music_lane_note(m->lane, 0);
    oct = gm_clampi((d + 6 + 120) / 12 - 10, -1, 1);   /* the register the note implies, its alteration rounded off */
    return (uint32_t)gm_clampi((int32_t)m->note + 12 * (reg - oct), 0, 127);
}
static void gm_stash_add(uint32_t ab)
{
    uint32_t s;
    for (s = 0; s < GN_STASH; s++)
        if (gm_stash[s] == AB_NONE)
            break;
    if (s == GN_STASH) {
        gm_stash[0] = gm_stash[1];
        gm_stash[1] = gm_stash[2];
        s = GN_STASH - 1u;
    }
    gm_stash[s] = (uint8_t)ab;
    game_event_push(GE_STASH, s, ab, 0);
}
static void gm_eat(uint32_t i, uint32_t q, int32_t reg, uint32_t half)
{
    gm_mush_t *m = &gm_mush[i];
    uint32_t note = gm_eat_note(m, reg), ab;
    m->state = MS_EATEN;
    m->hold = GN_EATEN_HOLD;
    if (m->species == SP_DEATHCAP) {
        band_note_on(PART_LEAD, note, 110, BN_SOUR | BN_NOTEOFF_SELF);
        gm_trip_add(-(int32_t)GN_STAGES[gm_stage].poison_trip);
        gm_combo = 0;
        gm_cnt[GC_POISONED]++;
        game_event_push(GE_POISON, m->lane, 0, 0);
        return;
    }
    if (q == Q_PERFECT) {
        band_note_on(PART_LEAD, note, 127, BN_ACCENT | BN_NOTEOFF_SELF);
        gm_cnt[GC_PERFECT]++;
        gm_trip_add(30);
    } else {
        band_note_on(PART_LEAD, note, 100, BN_NOTEOFF_SELF);
        gm_trip_add(20);
    }
    gm_combo++;
    if (gm_combo > gm_st_best_combo)
        gm_st_best_combo = gm_combo;
    gm_score += (half ? 5u : q == Q_PERFECT ? 15u : 10u) * gm_mult();
    gm_cnt[GC_EATEN]++;
    gm_st_eaten++;
    gm_last_melody_eaten = 1;
    game_event_push(GE_EAT, m->lane, q, m->species);
    if (m->species == SP_AGARIC) {
        if (gm_trip >= GN_TRIP_MAX)
            gm_score += 50u * gm_mult();
        else
            gm_trip_add(100);
        music_bump(2);
        game_event_push(GE_AGARIC, m->lane, 0, 0);
    }
    ab = m->species < SP_COUNT ? GM_SP_ABILITY[m->species] : AB_NONE;
    if (ab != AB_NONE)
        gm_stash_add(ab);
    if (m->chain) {
        if (m->chain != gm_chain_cur) {
            gm_chain_cur = m->chain;
            gm_chain_n = 0;
            gm_chain_broken = 0;
        }
        gm_chain_n++;
        if ((m->flags & GN_SPAWN_CHAIN_END) && !gm_chain_broken) {
            gm_score += 50u * gm_mult();
            gm_cnt[GC_CHAINS]++;
            game_event_push(GE_CHAIN, m->chain, gm_chain_n, 0);
        }
    }
}
static void gm_miss(uint32_t i)
{
    gm_mush_t *m = &gm_mush[i];
    m->state = MS_MISSED;
    if (m->species == SP_DEATHCAP)       /* passes: no event, no cost, it just falls off */
        return;
    gm_trip_add(-(int32_t)GN_STAGES[gm_stage].miss_trip);
    gm_combo = 0;
    gm_cnt[GC_MISSED]++;
    gm_st_missed++;
    gm_last_melody_eaten = 0;
    if (m->chain) {
        if (m->chain != gm_chain_cur) {
            gm_chain_cur = m->chain;
            gm_chain_n = 0;
        }
        gm_chain_broken = 1;
    }
    band_drum(DR_TOM, 40);
    game_event_push(GE_MISS, m->lane, 0, 0);
}
static void gm_air(uint32_t note)
{
    band_note_on(PART_LEAD, note, 60, BN_SOFT | BN_NOTEOFF_SELF);
    gm_cnt[GC_AIR]++;
}
static uint32_t gm_find(uint32_t lane, uint32_t t, uint32_t primary, uint32_t *q)
{
    uint32_t i, best = GM_NONE, bestd = GM_NONE;
    *q = Q_NONE;
    for (i = 0; i < GN_MAX_MUSH; i++) {
        const gm_mush_t *m = &gm_mush[i];
        uint32_t d, g, p;
        if (m->state != MS_FALLING || m->lane != lane || (m->flags & GN_SPAWN_AUTO))
            continue;
        if (m->species == SP_DEATHCAP && !primary)
            continue;
        d = gm_absdiff(m->hit, t);
        if (d >= bestd)
            continue;
        gm_windows(i, &g, &p);
        if (d > g)
            continue;
        best = i;
        bestd = d;
        *q = d <= p ? Q_PERFECT : Q_GOOD;
    }
    return best;
}
static uint32_t gm_find_frozen(uint32_t lane, uint32_t primary)   /* the lowest hanging: the soonest hit */
{
    uint32_t i, best = GM_NONE, besth = GM_NONE;
    for (i = 0; i < GN_MAX_MUSH; i++) {
        const gm_mush_t *m = &gm_mush[i];
        if (m->state != MS_FROZEN || m->lane != lane)
            continue;
        if (m->species == SP_DEATHCAP && !primary)
            continue;
        if (m->hit < besth) {
            besth = m->hit;
            best = i;
        }
    }
    return best;
}
static void gm_ghost_add(uint32_t lane, uint32_t t)
{
    uint32_t k;
    for (k = 1; k <= 2u && gm_ghost_n < GM_GHOSTS; k++) {
        gm_ghost_tick[gm_ghost_n] = t + k * GN_PPQ;
        gm_ghost_lane[gm_ghost_n] = (uint8_t)lane;
        gm_ghost_n++;
    }
}

/* ---- the boss's chomps */
static void gm_boss_chomp(void)
{
    uint32_t k;
    gm_boss.touch++;
    gm_boss.bites++;
    for (k = 0; k < 3u; k++)
        gm_boss.ptick[k] = GM_NONE;
    gm_cnt[GC_BOSS_CHOMPS]++;
    gm_score += 30u * gm_mult();
    gm_trip_add(30);
    for (k = 0; k < 3u; k++) {
        uint32_t note = music_lane_note(gm_boss.lanes[k], 0);
        band_note_on(PART_STAB, note, 127, BN_NOTEOFF_SELF);
        band_note_on(PART_LEAD, note, 127, BN_ACCENT | BN_NOTEOFF_SELF);
    }
    game_event_push(GE_BOSS_CHOMP, gm_boss.bites, 0, 0);
    if (gm_boss.bites >= 3u) {
        gm_score += 200u * gm_mult();
        band_drum(DR_CRASH, 127);
        game_event_push(GE_BOSS_EATEN, 0, 0, 0);
        gm_boss.on = 0;
    }
}
static void gm_boss_press(uint32_t lane, uint32_t t)
{
    uint32_t k, lo = GM_NONE, hi = 0, tk;
    if (!gm_boss.on || gm_boss.touch >= 3u || gm_boss.landed)
        return;
    for (k = 0; k < 3u; k++)
        if (gm_boss.lanes[k] == lane)
            gm_boss.ptick[k] = t;
    for (k = 0; k < 3u; k++) {
        if (gm_boss.ptick[k] == GM_NONE)
            return;
        if (gm_boss.ptick[k] < lo)
            lo = gm_boss.ptick[k];
        if (gm_boss.ptick[k] > hi)
            hi = gm_boss.ptick[k];
    }
    tk = gm_boss.hit + 2u * GN_PPQ * gm_boss.touch;
    if (hi - lo <= gm_ms_ticks(GN_BOSS_CHORD_MS) && gm_absdiff(t, tk) <= gm_ms_ticks(GN_WIN_GOOD_MS))
        gm_boss_chomp();
}

/* ---- a lane press, at the block's start tick */
static void gm_press_lane(uint32_t lane, int32_t reg, uint32_t t, uint32_t primary)
{
    uint32_t i, q, ate = 0;
    if (lane >= GN_LANES)
        return;
    if (primary) {
        gm_gnome_lane = lane;
        gm_gnome_reg = reg;
        gm_last_press = t;
        gm_boss_press(lane, t);
    }
    if (gm_ability == AB_FREEZE) {
        i = gm_find_frozen(lane, primary);
        if (i != GM_NONE) {
            gm_eat(i, Q_GOOD, reg, 0);
            ate = 1;
        }
    } else {
        i = gm_find(lane, t, primary, &q);
        if (i != GM_NONE) {
            gm_eat(i, q, reg, 0);
            ate = 1;
            if (gm_ability == AB_ECHO && primary)
                gm_ghost_add(lane, t);
        }
    }
    if (!ate && (primary || gm_ability == AB_CHORD))   /* CHORD's extra lanes sound the triad; WIDE's stay quiet */
        gm_air(music_lane_note(lane, reg));
    if (!primary)
        return;
    if (gm_ability == AB_CHORD) {
        gm_press_lane((lane + 2u) % GN_LANES, reg + (lane + 2u >= GN_LANES ? 1 : 0), t, 0);
        gm_press_lane((lane + 4u) % GN_LANES, reg + (lane + 4u >= GN_LANES ? 1 : 0), t, 0);
    } else if (gm_ability == AB_WIDE) {
        if (lane > 0u)
            gm_press_lane(lane - 1u, reg, t, 0);
        if (lane + 1u < GN_LANES)
            gm_press_lane(lane + 1u, reg, t, 0);
    }
}
static void gm_spice(uint32_t b)
{
    band_note_on(PART_LEAD, music_spice_note(b), 60, BN_SOFT | BN_NOTEOFF_SELF);
    gm_cnt[GC_AIR]++;
}

/* ---- the stash and the abilities */
static void gm_ability_end(void)
{
    uint32_t i;
    switch (gm_ability) {
    case AB_ARP:
        music_arp(0);
        break;
    case AB_ECHO:
        band_set(BP_DLY_DIV, gm_base_dly_div);
        band_set(BP_DLY_FDBK, gm_base_dly_fdbk);
        band_set(BP_DLY_SEND, gm_base_dly_send);
        break;
    case AB_SLOW:
        music_rate(1.0f);
        band_set(BP_PITCH, gm_base_pitch);
        band_set(BP_FILTER, gm_base_filter);
        break;
    case AB_WIDE:
        band_set(BP_LEAD_DETUNE, gm_base_detune);
        break;
    case AB_FREEZE:
        music_hold(HOLD_FREEZE, 0);
        for (i = 0; i < GN_MAX_MUSH; i++)
            if (gm_mush[i].state == MS_FROZEN)
                gm_mush[i].state = MS_FALLING;
        break;
    case AB_SHIMMER:
        band_set(BP_SHIMMER, gm_base_shimmer);
        break;
    default:
        break;
    }
    gm_ability = AB_NONE;
}
static void gm_ability_start(uint32_t ab)
{
    uint32_t i, bpm;
    gm_ability = ab;
    gm_ab_end = music_now() + (uint32_t)GM_AB_BARS[ab] * GN_BAR;
    switch (ab) {
    case AB_ARP:
        music_arp(1);
        break;
    case AB_ECHO:
        gm_base_dly_div = band_get(BP_DLY_DIV);
        gm_base_dly_fdbk = band_get(BP_DLY_FDBK);
        gm_base_dly_send = band_get(BP_DLY_SEND);
        band_set(BP_DLY_DIV, 2.0f);
        band_set(BP_DLY_FDBK, 0.6f);
        band_set(BP_DLY_SEND, 0.35f);
        break;
    case AB_SLOW:
        gm_base_pitch = band_get(BP_PITCH);
        gm_base_filter = band_get(BP_FILTER);
        music_rate(0.5f);
        band_set(BP_PITCH, -12.0f);
        band_set(BP_FILTER, -0.6f);
        break;
    case AB_WIDE:
        gm_base_detune = band_get(BP_LEAD_DETUNE);
        band_set(BP_LEAD_DETUNE, 1.0f);
        break;
    case AB_FREEZE:
        music_hold(HOLD_FREEZE, 1);
        for (i = 0; i < GN_MAX_MUSH; i++)
            if (gm_mush[i].state == MS_FALLING && !(gm_mush[i].flags & GN_SPAWN_AUTO))
                gm_mush[i].state = MS_FROZEN;
        bpm = music_bpm();
        if (bpm == 0u)
            bpm = 120u;
        gm_ab_samples = (uint32_t)(GN_FS * 240.0f / (float)bpm) * (uint32_t)GM_AB_BARS[ab];   /* real time: 4 beats a bar */
        break;
    case AB_SHIMMER:
        gm_base_shimmer = band_get(BP_SHIMMER);
        band_set(BP_SHIMMER, 0.6f);
        break;
    default:
        break;
    }
}
static void gm_burp(void)
{
    uint32_t i;
    uint8_t lanes[3];
    for (i = 0; i < GN_MAX_MUSH; i++) {
        gm_mush_t *m = &gm_mush[i];
        if ((m->state != MS_FALLING && m->state != MS_FROZEN) || (m->flags & GN_SPAWN_AUTO))
            continue;
        if (m->species == SP_DEATHCAP) {
            m->state = MS_FREE;
            continue;
        }
        gm_eat(i, Q_GOOD, GM_REG_OWN, 1);
    }
    music_chord_lanes(lanes);
    for (i = 0; i < 3u; i++)
        band_note_on(PART_STAB, music_lane_note(lanes[i], 0), 127, BN_NOTEOFF_SELF);
    band_drum(DR_CRASH, 120);
}
static void gm_stash_use(uint32_t slot)
{
    uint32_t ab = gm_stash[slot], s;
    if (ab != AB_BURP && gm_ability != AB_NONE) {
        game_event_push(GE_REFUSED, ab, 0, 0);
        return;
    }
    for (s = slot; s + 1u < GN_STASH; s++)
        gm_stash[s] = gm_stash[s + 1u];
    gm_stash[GN_STASH - 1u] = AB_NONE;
    game_event_push(GE_USE, ab, 0, 0);
    if (ab == AB_BURP)
        gm_burp();
    else
        gm_ability_start(ab);
}
static void gm_ability_tick(uint32_t n)
{
    if (gm_ability == AB_NONE)
        return;
    if (gm_ability == AB_FREEZE) {
        if (gm_ab_samples > n)
            gm_ab_samples -= n;
        else
            gm_ability_end();
    } else if ((int32_t)(music_now() - gm_ab_end) >= 0)
        gm_ability_end();
}
static uint32_t gm_ability_bars(void)
{
    uint32_t now = music_now();
    if (gm_ability == AB_NONE)
        return 0;
    if (gm_ability == AB_FREEZE)
        return 1;
    if ((int32_t)(gm_ab_end - now) <= 0)
        return 0;
    return (gm_ab_end - now + GN_BAR - 1u) / GN_BAR;
}

/* ---- the boss's descent: 0 through the riser bar, then down a beat and up a beat around each touch */
static int16_t gm_boss_pos(uint32_t now)
{
    int32_t d = (int32_t)(now - gm_boss.hit), b, frac, from, to;
    if (d < -2 * (int32_t)GN_PPQ)
        return 0;
    b = (d + 2 * (int32_t)GN_PPQ) / (int32_t)GN_PPQ - 2;
    frac = d - b * (int32_t)GN_PPQ;
    if (b >= 6) {
        from = 256;
        to = 400;
    } else if (b == -2) {
        from = 0;
        to = 128;
    } else if (b & 1) {
        from = 128;
        to = 256;
    } else {
        from = 256;
        to = 128;
    }
    return (int16_t)(from + (to - from) * frac / (int32_t)GN_PPQ);
}
static void gm_boss_tick(uint32_t now, uint32_t keys)
{
    uint32_t good, k;
    if (!gm_boss.on)
        return;
    gm_boss.y256 = gm_boss_pos(now);
    good = gm_ms_ticks(GN_WIN_GOOD_MS);
    while (gm_boss.touch < 3u) {
        uint32_t tk = gm_boss.hit + 2u * GN_PPQ * gm_boss.touch;
        if (gm_absdiff(now, tk) <= good) {
            for (k = 0; k < 3u; k++)
                if (!(keys & gm_lane_keys(gm_boss.lanes[k])))
                    break;
            if (k == 3u) {               /* all three held at the touch */
                gm_boss_chomp();
                continue;
            }
            break;
        }
        if ((int32_t)(now - tk) > (int32_t)good) {
            gm_boss.touch++;             /* the touch passed unbitten */
            continue;
        }
        break;
    }
    if (!gm_boss.on)
        return;
    if (!gm_boss.landed && (int32_t)(now - (gm_boss.hit + 6u * GN_PPQ)) >= 0) {
        gm_boss.landed = 1;
        game_event_push(GE_BOSS_LANDED, 0, 0, 0);
        if (gm_stage >= 1u)
            gm_trip_add(-(int32_t)(3u - gm_boss.bites) * 40);
    }
    if (gm_boss.landed && (int32_t)(now - (gm_boss.hit + 7u * GN_PPQ)) >= 0)
        gm_boss.on = 0;
}

/* ---- the table each block: misses, and slots falling off or done with their bite frame */
static void gm_table_tick(uint32_t now)
{
    uint32_t i, g, p;
    for (i = 0; i < GN_MAX_MUSH; i++) {
        gm_mush_t *m = &gm_mush[i];
        int32_t past = (int32_t)(now - m->hit);
        switch (m->state) {
        case MS_FALLING:
            if (m->flags & GN_SPAWN_AUTO) {
                if (past > (int32_t)(m->fall / 4u))   /* y256 past GN_Y_GONE */
                    m->state = MS_FREE;
                break;
            }
            gm_windows(i, &g, &p);
            if (past > (int32_t)g)
                gm_miss(i);
            break;
        case MS_MISSED:
            if (past > (int32_t)(m->fall / 4u))
                m->state = MS_FREE;
            break;
        case MS_EATEN:
            if (m->hold > 0u)
                m->hold--;
            if (m->hold == 0u)
                m->state = MS_FREE;
            break;
        default:
            break;
        }
    }
}
static void gm_ghosts(uint32_t now)
{
    uint32_t i = 0, q, j;
    while (i < gm_ghost_n) {
        if ((int32_t)(now - gm_ghost_tick[i]) < 0) {
            i++;
            continue;
        }
        j = gm_find(gm_ghost_lane[i], gm_ghost_tick[i], 0, &q);
        if (j != GM_NONE)
            gm_eat(j, Q_GOOD, GM_REG_OWN, 0);
        game_event_push(GE_GHOST, gm_ghost_lane[i], 0, 0);
        gm_ghost_n--;
        gm_ghost_tick[i] = gm_ghost_tick[gm_ghost_n];
        gm_ghost_lane[i] = gm_ghost_lane[gm_ghost_n];
    }
}
static void gm_decay(uint32_t now)
{
    uint32_t beat = now / GN_PPQ;
    if (beat == gm_beat_last)
        return;
    if (gm_beat_last != GM_NONE && beat > gm_beat_last && gm_stage >= 2u
        && (gm_section == SEC_TUNE || gm_section == SEC_BOSS))
        gm_trip_add(-(int32_t)(gm_trip_level() / 3u) * (int32_t)(beat - gm_beat_last));
    gm_beat_last = beat;
}

/* ---- the run's ends and starts */
static void gm_clear_run(void)
{
    uint32_t i;
    gm_ability_end();
    if (gm_hold_pause) {
        music_hold(HOLD_PAUSE, 0);
        gm_hold_pause = 0;
    }
    gm_countin = 0;
    for (i = 0; i < GN_MAX_MUSH; i++)
        gm_mush[i].state = MS_FREE;
    for (i = 0; i < GN_STASH; i++)
        gm_stash[i] = AB_NONE;
    gm_boss.on = 0;
    gm_ghost_n = 0;
    gm_pend_n = 0;
}
static void gm_stop(void)
{
    gm_clear_run();
    gm_auto_left = 0;
    gm_auto_demo = 0;
    gm_state_v = GS_TITLE;
    music_title();
}
static void gm_over(void)
{
    gm_clear_run();
    gm_state_v = GS_OVER;
    music_over();
    game_event_push(GE_OVER, 0, 0, 0);
    if (!gm_practice && gm_score > gm_hiscore_saved)
        game_event_push(GE_HIGHSCORE, 0, 0, 0);
}
static void gm_trip_check(void)
{
    if (gm_trip > 0)
        return;
    if (gm_jam) {
        gm_trip = GN_TRIP_START;
        game_event_push(GE_WOBBLE, 0, 0, 0);
        band_note_on(PART_LEAD, music_lane_note(0, 0), 110, BN_SOUR | BN_NOTEOFF_SELF);
    } else
        gm_over();
}
static void gm_run_start(uint32_t stage, uint32_t jam, uint32_t seed, uint32_t state)
{
    uint32_t i;
    gm_clear_run();
    for (i = 0; i < GC_COUNT; i++)
        gm_cnt[i] = 0;
    gm_stage = stage >= GN_NSTAGES ? GN_NSTAGES - 1u : stage;
    gm_round = 0;
    gm_jam = jam != 0u;
    gm_practice = gm_stage > 0u;
    gm_trip = GN_TRIP_START;
    gm_score = gm_combo = 0;
    gm_st_eaten = gm_st_missed = gm_st_best_combo = 0;
    gm_chain_cur = gm_chain_n = gm_chain_broken = 0;
    gm_last_melody_eaten = 0;
    gm_rng = seed ? seed : 0x9E3779B9u;
    gm_demo_bar_last = GM_NONE;
    gm_state_v = state;
    music_stage(gm_stage, 0, seed);
    gm_beat_last = music_now() / GN_PPQ;
}
static void gm_resume(void)
{
    uint32_t bpm = music_bpm();
    if (bpm == 0u)
        bpm = 120u;
    gm_countin = GN_COUNTIN_BEATS;
    gm_countin_spb = (uint32_t)(GN_FS * 60.0f / (float)bpm) * (gm_ability == AB_SLOW ? 2u : 1u);
    gm_countin_left = gm_countin_spb;
    band_drum(DR_RIM, 90);
}
static void gm_countin_tick(uint32_t n)
{
    if (gm_countin_left > n) {
        gm_countin_left -= n;
        return;
    }
    gm_countin_left += gm_countin_spb - n;
    gm_countin--;
    if (gm_countin) {
        band_drum(DR_RIM, 90);
        return;
    }
    music_hold(HOLD_PAUSE, 0);
    gm_hold_pause = 0;
    gm_state_v = GS_PLAY;
}
static void gm_cmd(void)
{
    uint32_t req = gn_cmd.req, v;
    if (req != gn_cmd.ack) {
        GN_BARRIER();
        switch (gn_cmd.cmd) {
        case CMD_START:
            if (gm_state_v == GS_ATTRACT) {
                gm_auto_left = 0;
                gm_auto_demo = 0;
            }
            gm_run_start(gn_cmd.stage, gn_cmd.jam, gn_cmd.seed, GS_PLAY);
            break;
        case CMD_PAUSE:
            if (gm_state_v == GS_PLAY || gm_state_v == GS_PAUSE) {
                gm_state_v = GS_PAUSE;
                gm_countin = 0;
                if (!gm_hold_pause) {
                    music_hold(HOLD_PAUSE, 1);
                    gm_hold_pause = 1;
                }
                band_all_off();
            }
            break;
        case CMD_RESUME:
            if (gm_state_v == GS_PAUSE && gm_countin == 0u)
                gm_resume();
            break;
        case CMD_STOP:
            gm_stop();
            break;
        case CMD_ATTRACT:
            gm_run_start(0, 1, gn_cmd.seed, GS_ATTRACT);
            gm_auto_left = GM_NONE;
            gm_auto_demo = 1;
            break;
        default:
            break;
        }
        gn_cmd.ack = req;
    }
    band_set(BP_LEAD_TONE, gn_cmd.tone);
    band_set(BP_SPACE, gn_cmd.space);
    v = gn_cmd.voice;
    if (v != 255u) {
        if (v < LP_COUNT)
            band_set(BP_LEAD_PATCH, (float)v);
    } else if (gm_voice_last != 255u)    /* the knob let go of the patch: back to the stage's (the title's bell) */
        band_set(BP_LEAD_PATCH, (float)(gm_state_v == GS_TITLE ? LP_BELL : GN_STAGES[gm_stage].lead));
    gm_voice_last = v;
}

/* ---- the keys */
static int32_t gm_note_reg(const gm_mush_t *m)
{
    int32_t d = (int32_t)m->note - (int32_t)music_lane_note(m->lane, 0);
    return gm_clampi((d + 6 + 120) / 12 - 10, -1, 1);
}
static uint32_t gm_in_run(uint32_t st) { return st == GS_PLAY || st == GS_ATTRACT; }
static void gm_key_lane(uint32_t lane, int32_t reg, uint32_t now, uint32_t st)
{
    if (gm_in_run(st)) {
        gm_press_lane(lane, reg, now, 1);
        return;
    }
    gm_gnome_lane = lane;                /* the title is an instrument too; the gnome follows the hand */
    gm_gnome_reg = reg;
    gm_air(music_lane_note(lane, reg));
}
static void gm_white(uint32_t w, uint32_t now, uint32_t st)
{
    if (GM_WKEY_LANE[w] == GN_NO_LANE)
        gm_air(music_lane_note(GM_WKEY_LETTER[w], -1));
    else
        gm_key_lane(GM_WKEY_LANE[w], GM_WKEY_REG[w], now, st);
}
static void gm_black(uint32_t b, uint32_t st)
{
    if (gm_in_run(st) && b < GN_STASH && gm_stash[b] != AB_NONE)
        gm_stash_use(b);
    else
        gm_spice(b);
}
static void gm_pending(uint32_t now, uint32_t st)
{
    uint32_t i;
    for (i = 0; i < gm_pend_n; i++) {
        if (gm_pend[i].kind)
            gm_spice(gm_pend[i].lane);  /* MIDI in never reaches the stash */
        else
            gm_key_lane(gm_pend[i].lane, (int32_t)gm_pend[i].reg - 1, now, st);
    }
    gm_pend_n = 0;
}

/* ---- after music_tick: the hits that fell inside this block */
static void gm_auto_tick(uint32_t before, uint32_t after)
{
    uint32_t i, bar;
    for (i = 0; i < GN_MAX_MUSH; i++) {
        gm_mush_t *m = &gm_mush[i];
        if (m->state != MS_FALLING || (int32_t)(after - m->hit) <= 0)
            continue;
        if (m->flags & GN_SPAWN_AUTO) {  /* its hit has passed: once */
            if (m->done)
                continue;
            m->done = 1;
            if (gm_last_melody_eaten) {
                band_note_on(PART_LEAD, m->note, 100, BN_NOTEOFF_SELF);
                game_event_push(GE_NOTE, m->lane, 0, 0);
            }
            continue;
        }
        if (m->species == SP_DEATHCAP || (int32_t)(m->hit - before) < 0)   /* a hit inside this block only */
            continue;
        if (gm_auto_left) {
            uint32_t q = Q_PERFECT;
            if (gm_auto_demo) {
                uint32_t r = gm_rnd() % 10u;
                if (r == 0u)
                    continue;            /* the demo's miss: nothing pressed */
                if (r <= 2u)
                    q = Q_GOOD;
            }
            gm_gnome_lane = m->lane;
            gm_gnome_reg = gm_note_reg(m);
            gm_last_press = m->hit;
            gm_eat(i, q, GM_REG_OWN, 0);
            if (gm_auto_left != GM_NONE)
                gm_auto_left--;
        } else if (gm_ability == AB_ARP && (m->species == SP_PORCINI || m->species == SP_AGARIC))
            gm_eat(i, Q_PERFECT, GM_REG_OWN, 1);
    }
    if (gm_auto_left && gm_boss.on && gm_boss.touch < 3u) {
        uint32_t tk = gm_boss.hit + 2u * GN_PPQ * gm_boss.touch;
        if ((int32_t)(tk - before) >= 0 && (int32_t)(after - tk) > 0)
            gm_boss_chomp();
    }
    if (gm_auto_demo) {
        bar = music_bar();
        if (bar != gm_demo_bar_last) {
            gm_demo_bar_last = bar;
            if ((bar & 7u) == 7u && gm_section == SEC_TUNE && gm_stash[0] != AB_NONE)
                gm_stash_use(0);
        }
    }
}

/* ---- the snapshot */
static void gm_snapshot(void)
{
    gn_snap_t *s = &gm_snap;
    uint32_t i, lane, now = music_now(), fall = music_fall();
    uint32_t next_hit[GN_LANES], next_i[GN_LANES];
    float nowf = music_now_f() + music_ms_ticks((float)GN_VIS_LEAD_MS);
    if (fall == 0u)
        fall = 2u * GN_PPQ;
    for (lane = 0; lane < GN_LANES; lane++)
        next_hit[lane] = GM_NONE;
    gm_seq++;
    GN_BARRIER();
    s->state = (uint8_t)gm_state_v;
    s->section = (uint8_t)gm_section;
    s->stage = (uint8_t)gm_stage;
    s->round = (uint8_t)(gm_round > 255u ? 255u : gm_round);
    s->jam = (uint8_t)gm_jam;
    s->practice = (uint8_t)gm_practice;
    s->trip_level = (uint8_t)gm_trip_level();
    s->gnome_lane = (uint8_t)gm_gnome_lane;
    s->gnome_reg = (int8_t)gm_gnome_reg;
    for (i = 0; i < GN_STASH; i++)
        s->stash[i] = gm_stash[i];
    s->ability = (uint8_t)gm_ability;
    s->ability_bars = (uint8_t)gm_ability_bars();
    s->boss_on = (uint8_t)gm_boss.on;
    s->boss_bites = (uint8_t)gm_boss.bites;
    for (i = 0; i < 3u; i++)
        s->boss_lanes[i] = gm_boss.lanes[i];
    s->boss_y256 = gm_boss.on ? gm_boss.y256 : 0;
    for (i = 0; i < GN_MAX_MUSH; i++) {
        gm_mush_t *m = &gm_mush[i];
        gn_mush_snap_t *d = &s->mush[i];
        if (m->state == MS_FALLING || m->state == MS_MISSED) {
            float y = 256.0f * (1.0f - ((float)m->hit - nowf) / (float)m->fall);
            m->y256 = (int16_t)fm_clampf(y, -32.0f, 400.0f);
        }
        d->lane = m->lane;
        d->species = m->species;
        d->state = m->state;
        d->chain = m->chain;
        d->flags = m->flags;
        d->y256 = m->state == MS_FREE ? 0 : m->y256;
        if ((m->state == MS_FALLING || m->state == MS_FROZEN) && !(m->flags & GN_SPAWN_AUTO)
            && m->lane < GN_LANES && m->hit < next_hit[m->lane]) {
            next_hit[m->lane] = m->hit;
            next_i[m->lane] = i;
        }
    }
    for (lane = 0; lane < GN_LANES; lane++) {
        uint32_t q = 255;
        s->lane_poison[lane] = 0;
        if (next_hit[lane] != GM_NONE) {
            q = (int32_t)(next_hit[lane] - now) <= 0 ? 0u : (next_hit[lane] - now) * 256u / fall;
            if (q > 254u)
                q = 254u;
            s->lane_poison[lane] = gm_mush[next_i[lane]].species == SP_DEATHCAP;
        }
        s->lane_next_q8[lane] = (uint8_t)q;
    }
    if (gm_boss.on && gm_boss.touch < 3u) {
        uint32_t tk = gm_boss.hit + 2u * GN_PPQ * gm_boss.touch;
        uint32_t q = (int32_t)(tk - now) <= 0 ? 0u : (tk - now) * 256u / fall;
        if (q > 254u)
            q = 254u;
        for (i = 0; i < 3u; i++)
            if (gm_boss.lanes[i] < GN_LANES)
                s->lane_next_q8[gm_boss.lanes[i]] = (uint8_t)q;
    }
    s->beat_q8 = (uint8_t)music_beat_q8();
    s->bar_beat = (uint8_t)gm_bar_beat();
    s->countin = (uint8_t)gm_countin;
    s->bpm = (uint16_t)music_bpm();
    s->bar = (uint16_t)music_bar();
    s->trip = (int16_t)gm_trip;
    s->score = gm_score;
    s->combo = gm_combo;
    s->hiscore = !gm_practice && gm_score > gm_hiscore_saved ? gm_score : gm_hiscore_saved;
    s->auto_left = gm_auto_left;
    s->st_eaten = (uint16_t)(gm_st_eaten > 65535u ? 65535u : gm_st_eaten);
    s->st_missed = (uint16_t)(gm_st_missed > 65535u ? 65535u : gm_st_missed);
    s->st_best_combo = (uint16_t)(gm_st_best_combo > 65535u ? 65535u : gm_st_best_combo);
    GN_BARRIER();
    gm_seq++;
    gm_snap_fall = fall;
}

/* ---- the render step */
void game_tick(uint32_t n)
{
    uint32_t keys, btn, kedge, bedge, now, w, b, st;
    gm_cmd();
    keys = plat_keys();
    btn = plat_buttons();
    kedge = keys & ~gm_keys_last;
    bedge = btn & ~gm_btn_last;
    gm_keys_last = keys;
    gm_btn_last = btn;
    st = gm_state_v;
    if (st == GS_ATTRACT && (kedge || bedge)) {
        gm_stop();
        st = GS_TITLE;
        kedge = 0;
        gm_pend_n = 0;
    }
    now = music_now();
    if (st != GS_PAUSE) {
        for (w = 0; w < GN_NWHITE; w++)
            if (kedge & (1u << GM_WHITE[w]))
                gm_white(w, now, st);
        for (b = 0; b < GN_NBLACK; b++)
            if (kedge & (1u << GM_BLACK[b]))
                gm_black(b, st);
        gm_pending(now, st);
    }
    gm_pend_n = 0;
    if (gm_in_run(st)) {
        gm_ghosts(now);
        gm_ability_tick(n);
        gm_boss_tick(now, keys);
        gm_table_tick(now);
        gm_decay(now);
        gm_trip_check();
    } else if (st == GS_PAUSE && gm_countin)
        gm_countin_tick(n);
    music_tick(n);
    if (gm_in_run(gm_state_v))
        gm_auto_tick(now, music_now());
    gm_snapshot();
}

/* ---- from the composer */
void game_spawn(uint32_t lane, uint32_t hit, uint32_t species, uint32_t note, uint32_t chain, uint32_t flags)
{
    uint32_t i;
    gm_mush_t *m;
    if (lane >= GN_LANES || !gm_in_run(gm_state_v))
        return;
    for (i = 0; i < GN_MAX_MUSH && gm_mush[i].state != MS_FREE; i++)
        ;
    if (i == GN_MAX_MUSH) {
        gm_cnt[GC_SPAWN_DROPPED]++;
        return;
    }
    m = &gm_mush[i];
    m->lane = (uint8_t)lane;
    m->species = (uint8_t)(species < SP_COUNT ? species : SP_PORCINI);
    m->chain = (uint8_t)chain;
    m->flags = (uint8_t)flags;
    m->note = (uint8_t)(note & 127u);
    m->done = 0;
    m->hold = 0;
    m->hit = hit;
    m->fall = music_fall();
    if (m->fall == 0u)
        m->fall = 2u * GN_PPQ;
    m->y256 = 0;
    m->state = MS_FALLING;
    if (!(flags & GN_SPAWN_AUTO))
        gm_cnt[GC_SPAWNED]++;
}
void game_boss(uint32_t hit, const uint8_t lanes[3])
{
    uint32_t k;
    if (!gm_in_run(gm_state_v))
        return;
    gm_boss.on = 1;
    gm_boss.hit = hit;
    gm_boss.bites = gm_boss.touch = gm_boss.landed = 0;
    gm_boss.y256 = 0;
    for (k = 0; k < 3u; k++) {
        gm_boss.lanes[k] = lanes[k] < GN_LANES ? lanes[k] : 0;
        gm_boss.ptick[k] = GM_NONE;
    }
}
void game_on_section(uint32_t section)
{
    if (section == SEC_COUNT) {
        if (gm_state_v == GS_ATTRACT) {
            gm_stop();
            return;
        }
        if (gm_state_v != GS_PLAY && gm_state_v != GS_PAUSE)
            return;
        gm_ability_end();
        if (gm_stage + 1u < GN_NSTAGES)
            gm_stage++;
        else
            gm_round++;
        game_event_push(GE_STAGE, gm_stage, 0, 0);
        music_stage(gm_stage, gm_round, gm_rnd());
        gm_beat_last = music_now() / GN_PPQ;
        return;
    }
    if (gm_state_v == GS_ATTRACT && section == SEC_BOSS) {   /* the attract is one TUNE section */
        gm_stop();
        return;
    }
    gm_section = section;
    if (section == SEC_INTRO) {
        if (gm_trip < GN_TRIP_START)
            gm_trip = GN_TRIP_START;
        gm_st_eaten = gm_st_missed = gm_st_best_combo = 0;
        gm_boss.on = 0;
    }
    game_event_push(GE_SECTION, section, gm_stage, 0);
}
void game_on_kick(void) { game_event_push(GE_KICK, gm_bar_beat(), 0, 0); }
void game_midi_key(uint32_t note, uint32_t vel, int on)
{
    uint32_t pc, b, best = 0, bestd = 99;
    int32_t reg;
    (void)vel;
    if (!on || gm_pend_n >= GM_PEND)
        return;
    note &= 127u;
    pc = note % 12u;
    reg = gm_clampi((int32_t)(note / 12u) - 5, -1, 1);
    if (GM_PC_LANE[pc] >= 0) {
        gm_pend[gm_pend_n].lane = (uint8_t)GM_PC_LANE[pc];
        gm_pend[gm_pend_n].reg = (uint8_t)(reg + 1);
        gm_pend[gm_pend_n].kind = 0;
        gm_pend_n++;
        return;
    }
    for (b = 0; b < GN_NBLACK; b++) {  /* the black key of that letter in the nearest register */
        uint32_t d;
        if (GM_WKEY_LETTER[GM_BLACK_LEFT[b]] != (uint8_t)GM_PC_LEFT[pc])
            continue;
        d = (uint32_t)(GM_BLACK_REG[b] > reg ? GM_BLACK_REG[b] - reg : reg - GM_BLACK_REG[b]);
        if (d < bestd) {
            bestd = d;
            best = b;
        }
    }
    gm_pend[gm_pend_n].lane = (uint8_t)best;
    gm_pend[gm_pend_n].reg = 1;
    gm_pend[gm_pend_n].kind = 1;
    gm_pend_n++;
}

/* ---- main-loop callers */
void game_init(uint32_t seed)
{
    uint32_t i;
    for (i = 0; i < GN_MAX_MUSH; i++)
        gm_mush[i].state = MS_FREE;
    for (i = 0; i < GN_STASH; i++)
        gm_stash[i] = AB_NONE;
    gm_rng = seed | 1u;
    gm_state_v = GS_TITLE;
    gm_section = SEC_TITLE;
    gm_beat_last = GM_NONE;
    gm_demo_bar_last = GM_NONE;
    gm_voice_last = 255u;
    gm_boss.on = 0;
}
uint32_t game_gnome_lane(void) { return gm_gnome_lane; }
uint32_t game_last_press(void) { return gm_last_press; }
uint32_t game_state(void) { return gm_state_v; }
uint32_t game_count(uint32_t which) { return which < GC_COUNT ? gm_cnt[which] : 0u; }
void game_auto(uint32_t n) { gm_auto_left = n; }
void game_auto_demo(int on) { gm_auto_demo = on != 0; }
int game_auto_on(void) { return gm_auto_left != 0u; }
void game_hiscore(uint32_t hs) { gm_hiscore_saved = hs; }
int game_snapshot(gn_snap_t *dst)
{
    static gn_snap_t copy;               /* main loop only; a torn read leaves dst as it was */
    uint32_t t, s;
    for (t = 0; t < 4u; t++) {
        s = gm_seq;
        GN_BARRIER();
        memcpy(&copy, &gm_snap, sizeof copy);
        GN_BARRIER();
        if (s == gm_seq && !(s & 1u)) {
            memcpy(dst, &copy, sizeof *dst);
            return 1;
        }
    }
    return 0;
}
int32_t game_next_hit_ms(uint32_t *lane)
{
    static gn_snap_t s;
    uint32_t i, fall = gm_snap_fall;
    float best = -1.0f, tick_ms;
    game_snapshot(&s);
    if (s.bpm == 0u || fall == 0u)
        return -1;
    tick_ms = 60000.0f / ((float)s.bpm * (float)GN_PPQ * (s.ability == AB_SLOW ? 0.5f : 1.0f));
    for (i = 0; i < GN_MAX_MUSH; i++) {
        float ms;
        if (s.mush[i].state != MS_FALLING || (s.mush[i].flags & GN_SPAWN_AUTO))
            continue;
        ms = (float)(256 - s.mush[i].y256) * (float)fall * (1.0f / 256.0f) * tick_ms + (float)GN_VIS_LEAD_MS;
        if (ms < 0.0f)
            continue;
        if (best < 0.0f || ms < best) {
            best = ms;
            *lane = s.mush[i].lane;
        }
    }
    return best < 0.0f ? -1 : (int32_t)best;
}
