/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann */
/* GNOMNOM's composer and transport (render context, included by app.c after tunes.c): an integer tick clock with
 * a sample accumulator, the tempo and its ramp, holds by reason, the sections and bars (the IDM 7/8 bar), the
 * backing band per genre (drums through a swing queue, bass, stabs, risers), the melody spawned a fall ahead on
 * the same tune data, the guide and ARP, the title's music box and the GAME OVER march. */

#define MU_Q 16                          /* the sample accumulator's fraction bits */
#define MU_QUEUE 12u
#define MU_NONE 0xFFFFFFFFu
#define MU_B(s) (1u << (s))
#define MU_B4(a, b, c, d) (MU_B(a) | MU_B(b) | MU_B(c) | MU_B(d))

enum { MQ_FREE, MQ_DRUM, MQ_BASS_OFF, MQ_STAB_OFF };
typedef struct { uint32_t tick; uint8_t kind, a, b; } mu_ev_t;
typedef struct { int32_t tb; uint32_t bstep, ni, nt; } mu_cur_t;   /* a cursor over the tune: the bar relative to
                                                                      * TUNE's first (negative in the INTRO), the step
                                                                      * in it, the next note and its onset in 16ths */
static struct {
    uint32_t now, acc, spt, step_tick, step_due;   /* ticks; samples into the tick and samples a tick, both 16.16 */
    float bpm, bpm0, bpm1, rate, build;
    uint32_t hold, rng, gen, running, run;
    uint32_t section, bar, bar_tick, over_gap, boss_due;
    uint32_t stage, round, genre, kit, theme, var;
    const gn_stage_t *row;
    const gn_tune_t *tn;
    uint8_t mode[7], sp_mode[7];
    uint32_t root, chord, fall, fall_steps;
    mu_cur_t bk, sp;                     /* the backing cursor and the spawn cursor, fall ahead of it */
    mu_ev_t q[MU_QUEUE];
    uint32_t last_mush, lane_last[GN_LANES], mel_last[GN_LANES];
    uint32_t chain_id, chain_end, chain_rest, agaric, stash, first_stash, dcap_mask;   /* chain_rest: the tune time
                                          * (16ths) before which no chain starts, a bar after the last */
    uint32_t arp, arp_i, hat_drop, bar_seed;
    uint32_t thin;                       /* the trip low, decided each bar: 1 the hats out, 2 the stabs too */
} mu;

static const uint8_t MU_VAR[8] = {0, 0, 1, 0, 0, 0, 2, 3};
static const uint8_t MU_AB_SPECIES[AB_COUNT] = GN_ABILITY_SPECIES;
static const float MU_LEAD_CUTOFF[LP_COUNT] = {0.5f, 0.55f, 0.5f, 0.7f};
static const float MU_STAB_CUTOFF[SS_COUNT] = {0.6f, 0.6f, 0.45f, 0.7f};
/* DESIGN.md section 4 step 2, bit i = step i: KICK SNARE HAT OHAT CLAP RIM CRASH TOM SHAKER; FOLK and the fills,
 * ghosts, crashes, dropouts and rolls are in code */
static const uint16_t MU_DRUM[GENRE_COUNT][4][DR_COUNT] = {
    {{0}, {0}, {0}, {0}},
    {{MU_B4(0, 4, 8, 12), 0, 0xAAAA, MU_B4(2, 6, 10, 14), MU_B(4) | MU_B(12), 0, 0, 0, 0},
     {MU_B4(0, 4, 8, 12) | MU_B(15), 0, 0xAAAA, MU_B4(2, 6, 10, 14), MU_B(4) | MU_B(12), 0, 0, 0, 0},
     {MU_B4(0, 4, 8, 12), 0, 0xAAAA, MU_B4(2, 6, 10, 14), MU_B(4) | MU_B(12) | MU_B(13), 0, 0, 0, 0},
     {MU_B4(0, 4, 8, 12), 0, 0xAAAA, MU_B(2) | MU_B(6) | MU_B(10), MU_B(4) | MU_B(12), 0, 0, 0, 0}},
    {{MU_B4(0, 4, 8, 12), 0, 0xFFFF, MU_B(6) | MU_B(14), MU_B(4) | MU_B(12), MU_B4(3, 7, 11, 15), 0, 0, 0xFFFF},
     {MU_B4(0, 4, 8, 12) | MU_B(15), 0, 0xFFFF, MU_B(6) | MU_B(14), MU_B(4) | MU_B(12), MU_B4(3, 7, 11, 15), 0, 0, 0xFFFF},
     {MU_B4(0, 4, 8, 12), 0, 0xFFFF, MU_B(6) | MU_B(14), MU_B(4) | MU_B(12) | MU_B(13), MU_B4(3, 7, 11, 15), 0, 0, 0xFFFF},
     {MU_B4(0, 4, 8, 12), 0, 0xFFFF, MU_B(6), MU_B(4) | MU_B(12), MU_B4(3, 7, 11, 15), 0, 0, 0xFFFF}},
    {{MU_B4(0, 4, 8, 12), 0, 0xFFFF, MU_B(10) | MU_B(14), MU_B(4) | MU_B(12), 0, 0, 0, 0},
     {MU_B4(0, 4, 8, 12), 0, 0xFFFF, MU_B(10) | MU_B(14), MU_B(4) | MU_B(12), 0, 0, 0, 0},
     {MU_B4(0, 4, 8, 12), 0, 0xFFFF, MU_B(10) | MU_B(14), MU_B(4) | MU_B(12), 0, 0, 0, 0},
     {MU_B(0) | MU_B(4) | MU_B(8), MU_B(13), 0xFFFF, MU_B(10) | MU_B(14), MU_B(4) | MU_B(12), 0, 0, 0, 0}},
    {{MU_B4(0, 4, 8, 12), MU_B(4) | MU_B(12), 0, MU_B4(2, 6, 10, 14), MU_B(4) | MU_B(12), 0, 0, 0, 0},
     {MU_B4(0, 4, 8, 12), MU_B(4) | MU_B(12), 0, MU_B4(2, 6, 10, 14), MU_B(4) | MU_B(12), 0, 0, 0, 0},
     {MU_B4(0, 4, 8, 12), MU_B(4) | MU_B(12), 0, MU_B4(2, 6, 10, 14), MU_B(4) | MU_B(12), 0, 0, MU_B(13) | MU_B(14) | MU_B(15), 0},
     {MU_B4(0, 4, 8, 12), MU_B(4) | MU_B(12), 0, MU_B4(2, 6, 10, 14), MU_B(4) | MU_B(12), 0, 0, 0, 0}},
    {{MU_B(0) | MU_B(6) | MU_B(10), MU_B(4) | MU_B(12), 0x5555, MU_B(7) | MU_B(14), 0, 0, 0, 0, 0},
     {MU_B(0) | MU_B(10), MU_B(4) | MU_B(12), 0x5555, MU_B(7) | MU_B(14), 0, 0, 0, 0, 0},
     {MU_B(0) | MU_B(7) | MU_B(10), MU_B(4) | MU_B(12), 0x5555, MU_B(7) | MU_B(14), 0, 0, 0, 0, 0},
     {MU_B(0) | MU_B(6) | MU_B(9), MU_B(4) | MU_B(12), 0x5555, MU_B(7) | MU_B(14), 0, 0, 0, 0, 0}},
    {{MU_B(0) | MU_B(7) | MU_B(10), MU_B(3) | MU_B(12), 0, 0, 0, 0, 0, 0, 0},
     {MU_B4(0, 5, 10, 13), MU_B(4) | MU_B(11), 0, 0, 0, 0, 0, 0, 0},
     {MU_B(0) | MU_B(3) | MU_B(10), MU_B(5) | MU_B(12), 0, 0, 0, 0, 0, 0, 0},
     {MU_B4(0, 8, 11, 14), MU_B(4) | MU_B(13), 0, 0, 0, 0, 0, 0, 0}},
    {{MU_B4(0, 4, 8, 12), MU_B(4) | MU_B(12), 0xFFFF, MU_B4(2, 6, 10, 14), MU_B(4) | MU_B(12), 0, 0, 0, 0},
     {MU_B4(0, 4, 8, 12), MU_B(4) | MU_B(12), 0xFFFF, MU_B4(2, 6, 10, 14), MU_B(4) | MU_B(12), 0, 0, 0, 0},
     {MU_B4(0, 4, 8, 12), MU_B(4) | MU_B(12), 0xFFFF, MU_B4(2, 6, 10, 14), MU_B(4) | MU_B(12), 0, 0, 0, 0},
     {MU_B4(0, 2, 4, 8) | MU_B(10) | MU_B(12) | 0xFF00, MU_B(4) | MU_B(12), 0xFFFF, MU_B4(2, 6, 10, 14), MU_B(4) | MU_B(12), 0, 0, 0, 0}},
};
static const uint16_t MU_GHOST[GENRE_COUNT][4] = {   /* snare ghosts */
    {0, 0, MU_B(11), 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0},
    {MU_B(9) | MU_B(15), MU_B(7) | MU_B(9), MU_B(9) | MU_B(15), 0}, {0, 0, 0, 0}, {0, 0, 0, 0},
};

/* ---- small helpers */
static uint32_t mu_rnd(void)
{
    uint32_t x = mu.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    mu.rng = x;
    return x;
}
static uint32_t mu_rnd_n(uint32_t n) { return mu_rnd() % n; }
static uint32_t mu_chance(uint32_t pct) { return mu_rnd_n(100u) < pct; }
static uint32_t mu_clamp_note(int32_t n) { return (uint32_t)(n < 0 ? 0 : n > 127 ? 127 : n); }
static void mu_set_spt(void)
{
    mu.spt = (uint32_t)(GN_FS * 60.0f / (mu.bpm * (float)GN_PPQ * mu.rate) * 65536.0f);
}
static uint32_t mu_bar_steps(int32_t tb)
{
    if (mu.run && mu.genre == GENRE_IDM && tb >= 0 && tb < (int32_t)mu.tn->bars && (tb & 3) == 3)
        return 14u;
    return 16u;
}
static void mu_cur_advance(mu_cur_t *c)
{
    if (++c->bstep >= mu_bar_steps(c->tb)) {
        c->bstep = 0;
        c->tb++;
    }
}
static uint32_t mu_dropped(uint32_t t)   /* a tune time (16ths) that falls on a 7/8 bar's missing steps */
{
    return mu_bar_steps((int32_t)(t >> 4)) == 14u && (t & 15u) >= 14u;
}
/* the tune note starting at the cursor (0: none or a rest); the cursor's note index catches up on the way */
static const gn_note_t *mu_note_at(mu_cur_t *c)
{
    const gn_tune_t *t = mu.tn;
    uint32_t tt;
    if (c->tb < 0 || c->tb >= (int32_t)t->bars)
        return 0;
    tt = (uint32_t)c->tb * 16u + c->bstep;
    while (c->ni < t->nnotes && c->nt + t->notes[c->ni].dur <= tt) {
        c->nt += t->notes[c->ni].dur;
        c->ni++;
    }
    if (c->ni < t->nnotes && c->nt == tt && GN_DEG(t->notes[c->ni].deg) != 0u)
        return &t->notes[c->ni];
    return 0;
}

/* ---- modes, pitches, chords */
static void mu_mode_build(uint8_t *dst, const uint8_t *base, uint32_t rot)
{
    uint32_t i, r = rot % 7u;
    for (i = 0; i < 7u; i++)
        dst[i] = (uint8_t)((base[(i + r) % 7u] + 12u - base[r]) % 12u);
}
static uint32_t mu_rot(int32_t tb)       /* the round's rotation, and under CHAOS a step every four bars of TUNE */
{
    uint32_t r = mu.round;
    int32_t bars = (int32_t)mu.tn->bars;
    if (mu.run && mu.theme == VT_CHAOS && tb > 0)
        r += (uint32_t)((tb >= bars ? bars - 1 : tb) >> 2);
    return r;
}
static uint32_t mu_pitch(const gn_note_t *n, const uint8_t *mode)
{
    return mu_clamp_note((int32_t)mu.root + 12 * n->oct + mode[GN_DEG(n->deg) - 1u] + GN_ALT(n->deg));
}
static int32_t mu_deg_semis(uint32_t deg0)   /* semitones above the root of a 0-based degree, past 6 an octave up */
{
    return (int32_t)mu.mode[deg0 % 7u] + 12 * (int32_t)(deg0 / 7u);
}
static void mu_triad(int32_t out[3])     /* the bar's chord as semitones above the root, in the lane register */
{
    uint32_t r = (mu.chord & 15u) - 1u;
    out[0] = mu_deg_semis(r);
    out[1] = mu_deg_semis(r + 2u);
    out[2] = mu_deg_semis(r + 4u);
    if (mu.chord & GN_CH_MINOR)
        out[1] = out[0] + 3;
    else if (mu.chord & GN_CH_MAJOR)
        out[1] = out[0] + 4;
}
static uint32_t mu_chord_at(int32_t tb)
{
    const gn_tune_t *t = mu.tn;
    int32_t bars = (int32_t)t->bars;
    if (!mu.run)
        return t->chord[tb < 0 ? 0 : tb >= bars ? bars - 1 : tb];
    if (tb < 0 || tb >= bars + 4)
        return 1u;
    return t->chord[tb >= bars ? bars - 1 : tb];
}

/* ---- the pending queue: swung drums, the bass and stab offs */
static void mu_q_run(void)
{
    uint32_t i;
    for (i = 0; i < MU_QUEUE; i++) {
        mu_ev_t *e = &mu.q[i];
        if (e->kind == MQ_FREE || (int32_t)(mu.now - e->tick) < 0)
            continue;
        switch (e->kind) {
        case MQ_DRUM:
            band_drum(e->a, e->b);
            if (e->a == DR_KICK)
                game_on_kick();
            break;
        case MQ_BASS_OFF: band_note_off(PART_BASS, e->a); break;
        default: band_note_off(PART_STAB, e->a); break;
        }
        e->kind = MQ_FREE;
    }
}
static void mu_q_clear(void)             /* the offs still owed go out now */
{
    uint32_t i;
    for (i = 0; i < MU_QUEUE; i++) {
        mu_ev_t *e = &mu.q[i];
        if (e->kind == MQ_BASS_OFF)
            band_note_off(PART_BASS, e->a);
        else if (e->kind == MQ_STAB_OFF)
            band_note_off(PART_STAB, e->a);
        e->kind = MQ_FREE;
    }
}
static int mu_q_push(uint32_t kind, uint32_t tick, uint32_t a, uint32_t b)
{
    uint32_t i;
    for (i = 0; i < MU_QUEUE; i++)
        if (mu.q[i].kind == MQ_FREE) {
            mu.q[i].kind = (uint8_t)kind;
            mu.q[i].tick = tick;
            mu.q[i].a = (uint8_t)a;
            mu.q[i].b = (uint8_t)b;
            return 1;
        }
    return 0;
}
static void mu_drum(uint32_t d, uint32_t vel, uint32_t delay)
{
    if (mu.thin && (d == DR_HAT || d == DR_OHAT || d == DR_SHAKER))
        return;
    if (delay && mu_q_push(MQ_DRUM, mu.now + delay, d, vel))
        return;
    band_drum(d, vel);
    if (d == DR_KICK)
        game_on_kick();
}
static void mu_bass_note(int32_t semis, uint32_t vel, uint32_t flags, uint32_t gate)
{
    uint32_t i, note = mu_clamp_note((int32_t)mu.root + semis - 24);
    for (i = 0; i < MU_QUEUE; i++)        /* a new note supersedes the off owed to the last */
        if (mu.q[i].kind == MQ_BASS_OFF)
            mu.q[i].kind = MQ_FREE;
    band_note_on(PART_BASS, note, vel, flags);
    if (gate)
        mu_q_push(MQ_BASS_OFF, mu.now + gate, note, 0);
}
static void mu_stab_note(int32_t semis, uint32_t vel, uint32_t gate)
{
    uint32_t note = mu_clamp_note((int32_t)mu.root + semis);
    if (mu.thin >= 2u)
        return;
    band_note_on(PART_STAB, note, vel, 0);
    if (gate)
        mu_q_push(MQ_STAB_OFF, mu.now + gate, note, 0);
}

/* ---- the transport */
static void mu_stop(void)
{
    if (mu.running)
        gn_midi_realtime(0xFC);
    mu_q_clear();
    mu.running = 0;
    mu.run = 0;
    mu.thin = 0;
    mu.arp = 0;
    mu.boss_due = 0;
    mu.over_gap = 0;
    mu.gen++;
}
static void mu_start(uint32_t section, const gn_tune_t *tn, float bpm, const uint8_t *mode)
{
    mu.gen++;
    mu_q_clear();
    mu.now = mu.acc = mu.step_tick = 0;
    mu.step_due = 1;
    mu.section = section;
    mu.bar = mu.bar_tick = 0;
    mu.tn = tn;
    mu.bpm = mu.bpm0 = mu.bpm1 = bpm;
    mu.build = 0.0f;
    mu.bk.tb = mu.sp.tb = 0;
    mu.bk.bstep = mu.bk.ni = mu.bk.nt = mu.sp.bstep = mu.sp.ni = mu.sp.nt = 0;
    mu.chord = 1;
    mu.arp = 0;
    mu.arp_i = 0;
    mu.boss_due = 0;
    mu.over_gap = 0;
    mu_mode_build(mu.mode, mode, 0);
    mu_set_spt();
    band_set(BP_TEMPO, bpm);
    if (!mu.running) {
        gn_midi_realtime(0xFA);
        gn_midi_realtime(0xF8);
    }
    mu.running = 1;
}
static void mu_over_start(void)
{
    mu.root = 60;
    mu.round = 0;
    mu.theme = VT_FOREST;
    mu.genre = GENRE_FOLK;
    mu_start(SEC_OVER, &GN_TUNES[TUNE_FUNERAL], 60.0f, GN_TUNES[TUNE_FUNERAL].mode);
    band_set(BP_LEAD_PATCH, (float)LP_BELL);
    band_set(BP_LEAD_CUTOFF, 0.6f);
    band_set(BP_LEAD_RELEASE, 0.7f);
    band_set(BP_LEAD_LEVEL, 0.8f);
    band_set(BP_KIT, (float)KIT_BRUSH);
    band_set(BP_DRUM_LEVEL, 0.6f);
    band_set(BP_REV_SEND, 0.6f);
    band_set(BP_REV_SIZE, 0.7f);
    band_set(BP_DLY_SEND, 0.1f);
}

/* ---- the step: sections */
static void mu_bar_index(void)           /* bars into the section, and the bar's first tick */
{
    int32_t tb = mu.bk.tb;
    switch (mu.section) {
    case SEC_INTRO: mu.bar = (uint32_t)(tb + (int32_t)mu.row->intro_bars); break;
    case SEC_BOSS: mu.bar = (uint32_t)(tb - (int32_t)mu.tn->bars); break;
    case SEC_CLEAR: mu.bar = (uint32_t)(tb - (int32_t)mu.tn->bars - 4); break;
    default: mu.bar = (uint32_t)tb; break;
    }
    mu.bar_tick = mu.step_tick;
}
static int mu_section_resolve(void)      /* 0: the step is over (the stage ended or the transport changed) */
{
    int32_t tb = mu.bk.tb, bars = (int32_t)mu.tn->bars;
    uint32_t next = mu.section, g = mu.gen;
    switch (mu.section) {
    case SEC_TITLE:
        if (tb >= bars) {
            mu.bk.tb = 0;
            mu.bk.bstep = mu.bk.ni = mu.bk.nt = 0;
        }
        return 1;
    case SEC_OVER:
        if (tb >= bars) {
            mu_stop();
            return 0;
        }
        return 1;
    case SEC_INTRO: if (tb >= 0) next = SEC_TUNE; break;
    case SEC_TUNE: if (tb >= bars) next = SEC_BOSS; break;
    case SEC_BOSS: if (tb >= bars + 4) next = SEC_CLEAR; break;
    default:
        if (tb >= bars + 6) {
            game_on_section(SEC_COUNT);
            if (g == mu.gen)             /* nobody took the next stage: stand */
                mu_stop();
            return 0;
        }
        break;
    }
    if (next != mu.section) {
        mu.section = next;
        mu.boss_due = next == SEC_BOSS;
        mu_bar_index();
        game_on_section(next);
        if (g != mu.gen)
            return 0;
    }
    return 1;
}
static void mu_bar_begin(void)
{
    int32_t tb = mu.bk.tb, tri[3];
    const uint8_t *base = mu.run ? mu.row->mode : mu.tn->mode;
    float hz;
    mu_bar_index();
    mu.chord = mu_chord_at(tb);
    mu_mode_build(mu.mode, base, mu_rot(tb));
    mu.var = MU_VAR[mu.bar & 7u];
    mu.bar_seed = mu_rnd();
    mu.hat_drop = 0;
    if (mu.genre == GENRE_IDM) {
        uint32_t i;
        for (i = 0; i < 16u; i++)
            if (mu_rnd_n(4u) == 0u)
                mu.hat_drop |= 1u << i;
    }
    mu_triad(tri);
    hz = 1.5f * 440.0f * fm_exp2f((float)((int32_t)mu.root + tri[0] - 69) * (1.0f / 12.0f));
    band_set(BP_RING_HZ, hz);
    {                                    /* the trip's pull, on the bar line with some hysteresis */
        float p = mu.run && mu.section != SEC_CLEAR ? band_get(BP_TRIP) : 0.0f;   /* never the fanfare */
        if (p <= -0.8f)
            mu.thin = 2;
        else if (p <= -0.5f)
            mu.thin = mu.thin == 2u && p <= -0.65f ? 2u : 1u;
        else if (p >= -0.35f)
            mu.thin = 0;
        else if (mu.thin == 2u)
            mu.thin = 1;
    }
}
static void mu_tempo(void)
{
    float b = mu.bpm0;
    if (mu.section == SEC_TUNE)
        b = mu.bpm0 + (mu.bpm1 - mu.bpm0) * mu.build;
    else if (mu.section == SEC_BOSS || mu.section == SEC_CLEAR)
        b = mu.bpm1;
    if (b != mu.bpm) {
        mu.bpm = b;
        mu_set_spt();
        band_set(BP_TEMPO, b);
    }
}

/* ---- the backing: drums */
static uint32_t mu_dvel(uint32_t d, uint32_t s)
{
    uint32_t g = mu.genre, onbeat = (s & 3u) == 0u, hard = g == GENRE_RAVE || g == GENRE_HARD;
    switch (d) {
    case DR_KICK: return hard ? 127u : s == 15u ? 80u : onbeat ? 118u : 105u;
    case DR_SNARE: return onbeat ? 108u : 96u;
    case DR_HAT:
        if (g == GENRE_ACID)
            return (s & 3u) == 2u ? 96u : 55u;
        if (g == GENRE_IDM)
            return (s & 1u) ? 58u : 84u;
        if (g == GENRE_HARD)
            return (s & 1u) ? 60u : 76u;
        return (s & 1u) ? 50u : 72u;
    case DR_OHAT: return g == GENRE_RAVE ? 112u : 92u;
    case DR_CLAP: return g == GENRE_DEEP ? 70u : s == 13u ? 72u : 104u;
    case DR_RIM: return g == GENRE_DEEP ? 45u : 72u;
    case DR_CRASH: return 112u;
    case DR_TOM: return 86u + (s & 3u) * 10u;
    default: return g == GENRE_DEEP ? 40u : 60u;
    }
}
static void mu_drums_folk(uint32_t s)
{
    uint32_t last_intro = mu.section == SEC_INTRO && mu.bar + 1u == mu.row->intro_bars;
    uint32_t fill = mu.var == 3u || (mu.build >= 0.75f && (mu.bk.tb & 3) == 3);
    if ((s & 1u) == 0u)
        mu_drum(DR_SHAKER, (s & 3u) ? 50u : 65u, 0);
    if (mu.section == SEC_INTRO) {
        if (s == 4u || s == 12u)
            mu_drum(DR_RIM, 65, 0);
        if (last_intro && s >= 8u)       /* the snare roll into TUNE */
            mu_drum(DR_SNARE, 50u + (s - 8u) * 9u, 0);
        return;
    }
    if (mu.build >= 0.3f) {
        if (s == 0u || s == 8u)
            mu_drum(DR_KICK, 92, 0);
        if (s == 4u || s == 12u)
            mu_drum(DR_SNARE, 86, 0);
        if (mu.var == 2u && s == 11u)
            mu_drum(DR_SNARE, 50, 0);
    }
    if (mu.var == 1u && (s == 2u || s == 10u))
        mu_drum(DR_RIM, 65, 0);
    if (fill && s >= 12u)
        mu_drum(DR_SNARE, 60u + (s - 12u) * 17u, 0);
}
static void mu_drums(uint32_t s)
{
    uint32_t g = mu.genre, var = mu.var, bit = 1u << s, d, bar = mu.bar;
    uint32_t swing = (s & 1u) ? (g == GENRE_DEEP ? 4u : g == GENRE_BREAKS ? 3u : 0u) : 0u;
    const uint16_t *m = MU_DRUM[g][var];
    if (g == GENRE_FOLK) {
        mu_drums_folk(s);
        return;
    }
    for (d = 0; d < DR_COUNT; d++)
        if (m[d] & bit)
            mu_drum(d, mu_dvel(d, s), swing);
    if (MU_GHOST[g][var] & bit)
        mu_drum(DR_SNARE, 45, swing);
    switch (g) {
    case GENRE_HOUSE:
    case GENRE_DEEP:
        if (s == 0u && bar && (bar & 7u) == 0u)   /* the open hat the fill bar dropped becomes this crash */
            mu_drum(DR_CRASH, 112, 0);
        if (var == 3u && s >= 8u)
            mu_drum(DR_SNARE, 60u + (s - 8u) * 8u, swing);
        break;
    case GENRE_RAVE:
        if (s == 0u && (bar & 7u) == 0u)
            mu_drum(DR_CRASH, 118, 0);
        if (var == 3u && s >= 8u) {
            mu_drum(DR_SNARE, 60u + (s - 8u) * 8u, 0);
            if (s == 14u && mu_chance(mu.row->stutter))
                band_stutter(32, 4);
        }
        break;
    case GENRE_BREAKS:
        if (var == 3u && s >= 8u && (s >= 12u || (s & 1u) == 0u))
            mu_drum(DR_SNARE, 60u + (s - 8u) * 8u, swing);
        break;
    case GENRE_IDM:
        if (!(mu.hat_drop & bit))
            mu_drum(DR_HAT, mu_dvel(DR_HAT, s), 0);
        if ((mu.bar_seed & 1u) ? (s == 9u || s == 10u) : (s == 1u || s == 2u))
            mu_drum(DR_RIM, 72, 0);
        if (s == 12u && mu_bar_steps(mu.bk.tb) == 14u && mu_chance(mu.row->stutter)) {
            if (mu.bar_seed & 2u)
                band_stutter(32, 3);
            else
                band_stutter(16, 2);
        }
        break;
    case GENRE_HARD:
        if (s == 0u && (bar & 3u) == 0u)
            mu_drum(DR_CRASH, 118, 0);
        break;
    default:
        break;
    }
}

/* ---- the backing: bass and stabs over the bar's chord */
static void mu_bass(uint32_t s)
{
    int32_t tri[3], semis;
    uint32_t onbeat = (s & 3u) == 0u, vel = onbeat ? 110u : 95u, i, k;
    static const uint8_t BRK[4] = {0, 6, 10, 13};
    mu_triad(tri);
    switch (mu.genre) {
    case GENRE_FOLK:
        if (s == 0u || s == 12u)
            mu_bass_note(tri[0], 96, 0, 0);
        else if (s == 4u)
            mu_bass_note(tri[0] + 12, 88, 0, 0);
        else if (s == 8u)
            mu_bass_note(tri[2], 88, 0, 0);
        else if (s == 14u) {             /* the approach: the degree under the next bar's root */
            uint32_t nr = (mu_chord_at(mu.bk.tb + 1) & 15u) - 1u;
            semis = mu_deg_semis((nr + 6u) % 7u) - (nr == 0u ? 12 : 0);
            mu_bass_note(semis, 84, 0, 0);
        }
        break;
    case GENRE_HOUSE:
        if ((s & 3u) == 2u)
            mu_bass_note(tri[0] + (s == 14u && (mu.bk.tb & 1) ? 12 : 0), 104, 0, 12);
        break;
    case GENRE_DEEP:
        if (s == 0u)
            mu_bass_note(tri[0], 110, 0, 3u * GN_STEP - 4u);
        else if (s == 6u)
            mu_bass_note(tri[2], 92, 0, 20);
        else if (s == 10u || s == 11u)
            mu_bass_note(tri[0], 96, 0, 12);
        else if (s == 14u)
            mu_bass_note(tri[0] + 12, 100, 0, 20);
        break;
    case GENRE_ACID: {
        uint32_t flags = 0;
        if (onbeat)
            semis = tri[0];
        else if ((s & 3u) == 3u) {
            semis = tri[0] + 12;
            flags = BN_ACCENT;
        } else if (s == 6u || s == 14u)
            semis = tri[0] + ((mu.bar_seed & 4u) ? 10 : 7);
        else
            break;
        if (mu_chance(20))
            break;
        if (mu_chance(20))
            flags |= BN_SLIDE;
        if (mu_chance(25))
            flags |= BN_ACCENT;
        mu_bass_note(semis, (flags & BN_ACCENT) ? 127u : 100u, flags, 0);
        break;
    }
    case GENRE_BREAKS:
    case GENRE_IDM:
        for (i = 0; i < 4u; i++) {
            k = BRK[i];
            if (mu.genre == GENRE_IDM && i == ((mu.bar_seed >> 4) & 3u)) {   /* one note a bar displaced by seed */
                if (mu.bar_seed & 64u)
                    k++;
                else if (k)
                    k--;
            }
            if (k == s)
                mu_bass_note(i == 3u ? tri[2] : tri[0], i ? 96u : 110u, 0, i == 3u ? 16u : GN_STEP);
        }
        break;
    default:                             /* RAVE and HARD: the root every 8th, the octave on 15 */
        if ((s & 1u) == 0u)
            mu_bass_note(tri[0], vel, 0, 20);
        else if (s == 15u)
            mu_bass_note(tri[0] + 12, 104, 0, 20);
        break;
    }
}
static void mu_stabs(uint32_t s)
{
    int32_t tri[3];
    uint32_t style = mu.row->stab, r = (mu.chord & 15u) - 1u;
    mu_triad(tri);
    switch (style) {
    case SS_STAB:
        if ((s & 3u) == 2u || (mu.var == 1u && (s == 7u || s == 15u))) {
            mu_stab_note(tri[1], 100, 0);
            mu_stab_note(tri[2], 100, 0);
            mu_stab_note(tri[0] + 12, 100, 0);
        }
        break;
    case SS_PAD:
        if (mu.genre == GENRE_FOLK) {    /* the low fifth, sustaining each bar from build 0.2 */
            if (s == 0u && (mu.build >= 0.2f || mu.section == SEC_BOSS)) {
                mu_stab_note(tri[0] - 24, 90, 15u * GN_STEP - 4u);
                mu_stab_note(tri[2] - 24, 90, 15u * GN_STEP - 4u);
            }
        } else if (s == 0u || s == 8u) { /* 3rd, 7th, 9th: the bass has the root */
            mu_stab_note(tri[1], 90, 6u * GN_STEP - 4u);
            mu_stab_note(mu_deg_semis(r + 6u), 90, 6u * GN_STEP - 4u);
            mu_stab_note(mu_deg_semis(r + 8u), 90, 6u * GN_STEP - 4u);
        }
        break;
    case SS_HOOVER:
        if (s == 0u || (mu.var == 2u && (s == 8u || s == 11u))) {
            mu_stab_note(tri[0], 110, 3u * GN_STEP - 4u);
            mu_stab_note(tri[2], 110, 3u * GN_STEP - 4u);
            mu_stab_note(tri[0] + 12, 110, 3u * GN_STEP - 4u);
        } else if (mu.genre == GENRE_HARD && (s & 3u) == 2u) {
            mu_stab_note(tri[0], 90, GN_STEP - 4u);
            mu_stab_note(tri[2], 90, GN_STEP - 4u);
            mu_stab_note(tri[0] + 12, 90, GN_STEP - 4u);
        }
        break;
    default:
        break;
    }
}

/* ---- the backing step: INTRO, TUNE, BOSS */
static void mu_backing(void)
{
    uint32_t s = mu.bk.bstep, sec = mu.section;
    const gn_note_t *mel = mu_note_at(&mu.bk);
    if (mu.genre == GENRE_FOLK && sec == SEC_TUNE && (s & 3u) == 0u) {
        band_set(BP_LEAD_CUTOFF, 0.3f + 0.5f * mu.build);
        band_set(BP_DRUM_LEVEL, 0.6f + 0.4f * mu.build);
    }
    mu_drums(s);
    mu_bass(s);
    mu_stabs(s);
    if (s == 0u && mu.stage + mu.round > 0u && sec == SEC_INTRO && mu.bar + 2u == mu.row->intro_bars)
        band_riser(32);
    if (s == 0u && sec == SEC_BOSS && mu.bar == 0u)
        band_riser(16);
    if (sec == SEC_TUNE && mel && mu.row->guide) {
        uint32_t p = mu_pitch(mel, mu.mode);
        band_note_on(PART_LEAD, p, 45, BN_SOFT | BN_NOTEOFF_SELF);
        if (mu.build >= 0.7f)
            band_note_on(PART_LEAD, p + 12u, 32, BN_SOFT | BN_NOTEOFF_SELF);
    }
    if (mu.arp && !mel && (sec == SEC_TUNE || sec == SEC_BOSS)) {
        int32_t tri[3];
        mu_triad(tri);
        band_note_on(PART_LEAD, mu_clamp_note((int32_t)mu.root + tri[mu.arp_i % 3u] + 12 * (int32_t)(mu.arp_i / 3u)),
                     70, BN_NOTEOFF_SELF);
        mu.arp_i = (mu.arp_i + 1u) % 6u;
    }
}
static void mu_clear_step(void)          /* the fanfare: the tune's first four notes as stabs, a crash */
{
    uint32_t s = mu.bk.bstep, i, k = 0, want = s >> 1;
    const gn_tune_t *t = mu.tn;
    if (mu.bar)
        return;
    if (s == 0u)
        band_drum(DR_CRASH, 120);
    if ((s & 3u) == 0u)
        mu_drum(DR_KICK, 100, 0);
    if (s >= 8u || (s & 1u))
        return;
    for (i = 0; i < t->nnotes && i < 16u; i++) {
        const gn_note_t *n = &t->notes[i];
        uint32_t d;
        int32_t p;
        if (GN_DEG(n->deg) == 0u)
            continue;
        if (k++ != want)
            continue;
        d = GN_DEG(n->deg) - 1u;
        p = (int32_t)mu_pitch(n, mu.mode);
        band_note_on(PART_LEAD, (uint32_t)p, 100, BN_NOTEOFF_SELF);
        p -= (int32_t)mu.root;
        mu_stab_note(p, 115, 2u * GN_STEP - 4u);
        mu_stab_note(p + mu_deg_semis(d + 2u) - mu_deg_semis(d), 115, 2u * GN_STEP - 4u);
        mu_stab_note(p + mu_deg_semis(d + 4u) - mu_deg_semis(d), 115, 2u * GN_STEP - 4u);
        return;
    }
}

/* ---- spawns: the spawn cursor runs fall ahead of the backing on the same tune */
static void mu_sp_bar(int32_t tb)
{
    const gn_stage_t *r = mu.row;
    uint32_t q = r->deathcap_q, n, i, steps = mu_bar_steps(tb);
    mu_mode_build(mu.sp_mode, r->mode, mu_rot(tb));
    mu.agaric = r->agaric_bars && ((uint32_t)tb % r->agaric_bars) == r->agaric_bars - 1u;
    mu.stash = r->stash_bars && ((uint32_t)tb % r->stash_bars) == r->stash_bars / 2u ? 2u : 0u;
    n = q / 4u + (mu_rnd_n(4u) < (q & 3u));
    mu.dcap_mask = 0;
    for (i = 0; i < n; i++)
        mu.dcap_mask |= 1u << mu_rnd_n(steps);
}
static uint32_t mu_gap_ticks(int32_t tb)   /* wider over TUNE's first four bars: an on-ramp after the INTRO */
{
    float ms = (float)mu.row->min_gap_ms * (tb < 2 ? 1.5f : tb < 4 ? 1.25f : 1.0f);
    return (uint32_t)(music_ms_ticks(ms) + 0.5f);
}
static uint32_t mu_is_mush(uint32_t bstep, uint32_t lane, uint32_t tick, uint32_t gap, uint32_t last,
                           const uint32_t *lane_last)
{
    return (bstep % mu.row->density) == 0u && tick - last >= gap && tick - lane_last[lane] >= gap + gap / 2u;
}
/* a run of six or more consecutive notes of two 16ths or less with no rest, capped at eight: its last index */
static void mu_chain_scan(const mu_cur_t *c)
{
    const gn_tune_t *t = mu.tn;
    uint32_t j = c->ni, tt = c->nt, cnt = 0, grid = 0;
    if (tt < mu.chain_rest)              /* a stream of sixteenths would chain end to end */
        return;
    while (cnt < 8u && j < t->nnotes && t->notes[j].dur <= 2u && GN_DEG(t->notes[j].deg) != 0u && !mu_dropped(tt)) {
        grid += (tt % mu.row->density) == 0u;
        tt += t->notes[j].dur;
        j++;
        cnt++;
    }
    if (cnt >= 6u && grid >= 4u) {       /* at density 4 a run is only two mushrooms: no chain, or one every 2 beats */
        mu.chain_end = c->ni + cnt - 1u;
        mu.chain_id = mu.chain_id % 255u + 1u;
    }
}
/* would any later note of the chain still be a mushroom after this one? (the same rules, walked ahead) */
static uint32_t mu_chain_more(const mu_cur_t *c, uint32_t tick, uint32_t gap)
{
    const gn_tune_t *t = mu.tn;
    uint32_t j, tt = c->nt, last = tick, ll[GN_LANES], i;
    for (i = 0; i < GN_LANES; i++)
        ll[i] = mu.lane_last[i];
    for (j = c->ni; j < mu.chain_end; ) {
        uint32_t lane, tk;
        tt += t->notes[j].dur;
        j++;
        lane = GN_DEG(t->notes[j].deg) - 1u;
        tk = tick + (tt - c->nt) * GN_STEP;
        if (mu_is_mush(tt & 15u, lane, tk, gap, last, ll))
            return 1;
    }
    return 0;
}
static void mu_spawn_note(const gn_note_t *n, const mu_cur_t *c, uint32_t tick)
{
    uint32_t lane = GN_DEG(n->deg) - 1u, note = mu_pitch(n, mu.sp_mode), species = SP_PORCINI, chain = 0, flags = 0;
    uint32_t gap = mu_gap_ticks(c->tb), mush;
    mush = mu_is_mush(c->bstep, lane, tick, gap, mu.last_mush, mu.lane_last);
    if (mu.chain_end == MU_NONE && n->dur <= 2u)
        mu_chain_scan(c);
    if (mu.chain_end != MU_NONE) {
        chain = mu.chain_id;
        if (c->ni >= mu.chain_end)
            mu.chain_end = MU_NONE;
    }
    if (mush) {
        if (mu.agaric) {
            species = SP_AGARIC;
            mu.agaric = 0;
        } else if (mu.stash && --mu.stash == 0u) {
            species = MU_AB_SPECIES[music_stash_pick()];
        }
        mu.last_mush = tick;
        mu.lane_last[lane] = tick;
        if (chain && (mu.chain_end == MU_NONE || !mu_chain_more(c, tick, gap))) {
            flags |= GN_SPAWN_CHAIN_END;
            mu.chain_end = MU_NONE;
        }
    } else {
        flags |= GN_SPAWN_AUTO;
    }
    if (chain && mu.chain_end == MU_NONE)
        mu.chain_rest = c->nt + 16u;
    mu.mel_last[lane] = tick;
    game_spawn(lane, tick, species, note, chain, flags);
}
static void mu_spawn_dcap(const mu_cur_t *c, uint32_t tick)
{
    const gn_tune_t *t = mu.tn;
    uint32_t busy = 0, i, j, tt = (uint32_t)c->tb * 16u + c->bstep, ot = c->nt, ok[GN_LANES], nf = 0;
    for (i = 0; i < GN_LANES; i++)
        if (tick - mu.mel_last[i] <= GN_PPQ)
            busy |= 1u << i;
    for (j = c->ni; j < t->nnotes && ot <= tt + 4u; j++) {   /* the beat ahead */
        if (ot >= tt && GN_DEG(t->notes[j].deg))
            busy |= 1u << (GN_DEG(t->notes[j].deg) - 1u);
        ot += t->notes[j].dur;
    }
    if (mu.now - game_last_press() < GN_PPQ && game_gnome_lane() < GN_LANES)
        busy |= 1u << game_gnome_lane();
    for (i = 0; i < GN_LANES; i++)
        if (!(busy & (1u << i)))
            ok[nf++] = i;
    if (!nf)
        return;
    i = ok[mu_rnd_n(nf)];
    game_spawn(i, tick, SP_DEATHCAP, music_lane_note(i, 0), 0, 0);
}
static void mu_spawn_step(void)
{
    mu_cur_t *c = &mu.sp;
    uint32_t tick = mu.step_tick + mu.fall;
    const gn_note_t *n;
    if (mu.run && c->tb >= 0 && c->tb < (int32_t)mu.tn->bars && (mu.section == SEC_INTRO || mu.section == SEC_TUNE)) {
        if (c->bstep == 0u)
            mu_sp_bar(c->tb);
        n = mu_note_at(c);
        if (n)
            mu_spawn_note(n, c, tick);
        if (mu.dcap_mask & (1u << c->bstep))
            mu_spawn_dcap(c, tick);
    }
    mu_cur_advance(c);
}

/* ---- one 16th */
static void mu_step(void)
{
    uint32_t g = mu.gen, s;
    int32_t tb;
    if (!mu_section_resolve())
        return;
    s = mu.bk.bstep;
    tb = mu.bk.tb;
    if (s == 0u)
        mu_bar_begin();
    mu.build = mu.section == SEC_TUNE ? (float)((uint32_t)tb * 16u + s) / (float)(mu.tn->bars * 16u)
             : mu.section == SEC_INTRO ? 0.0f : 1.0f;
    mu_tempo();
    if (mu.boss_due) {
        uint8_t lanes[3];
        mu.boss_due = 0;
        music_chord_lanes(lanes);
        game_boss(mu.step_tick + GN_BAR + 2u * GN_PPQ, lanes);
    }
    switch (mu.section) {
    case SEC_TITLE:
    case SEC_OVER: {
        const gn_note_t *mel = mu_note_at(&mu.bk);
        if (mel)
            band_note_on(PART_LEAD, mu_pitch(mel, mu.mode), mu.section == SEC_TITLE ? 84u : 72u, BN_NOTEOFF_SELF);
        if (mu.section == SEC_OVER && (s & 3u) == 0u)
            mu_drum(DR_KICK, 50, 0);
        break;
    }
    case SEC_CLEAR: mu_clear_step(); break;
    default: mu_backing(); break;
    }
    if (g != mu.gen)
        return;
    mu_spawn_step();
    mu_cur_advance(&mu.bk);
    mu.step_tick += GN_STEP;
}

/* ---- the contract */
void music_init(void)
{
    uint32_t i;
    for (i = 0; i < MU_QUEUE; i++)
        mu.q[i].kind = MQ_FREE;
    mu.running = mu.run = mu.hold = mu.over_gap = mu.arp = 0;
    mu.section = SEC_TITLE;
    mu.rate = 1.0f;
    mu.bpm = mu.bpm0 = mu.bpm1 = 120.0f;
    mu.rng = 1;
    mu.root = 60;
    mu.chord = 1;
    mu.tn = &GN_TUNES[TUNE_MOUNTAIN];
    mu.row = &GN_STAGES[0];
    mu.fall = 2u * GN_PPQ;
    mu.fall_steps = mu.fall / GN_STEP;
    mu.chain_end = MU_NONE;
    mu_mode_build(mu.mode, mu.tn->mode, 0);
    mu_set_spt();
    mu.gen++;
}
void music_title(void)
{
    mu_stop();
    mu.root = 60;
    mu.round = 0;
    mu.theme = VT_FOREST;
    mu.genre = GENRE_FOLK;
    mu.row = &GN_STAGES[0];
    mu_start(SEC_TITLE, &GN_TUNES[TUNE_MOUNTAIN], 70.0f, GN_TUNES[TUNE_MOUNTAIN].mode);
    band_set(BP_LEAD_PATCH, (float)LP_BELL);
    band_set(BP_LEAD_CUTOFF, 0.7f);
    band_set(BP_LEAD_DECAY, 0.5f);
    band_set(BP_LEAD_RELEASE, 0.6f);
    band_set(BP_LEAD_DETUNE, 0.0f);
    band_set(BP_LEAD_LEVEL, 0.8f);
    band_set(BP_STAB_STYLE, (float)SS_OFF);
    band_set(BP_ENS, (float)ENS_CHORUS1);
    band_set(BP_ENS_MIX, 0.4f);
    band_set(BP_DLY_SEND, 0.2f);
    band_set(BP_DLY_DIV, 2.0f);
    band_set(BP_DLY_FDBK, 0.4f);
    band_set(BP_REV_SEND, 0.6f);
    band_set(BP_REV_SIZE, 0.7f);
    band_set(BP_REV_DAMP, 0.4f);
    band_set(BP_SHIMMER, 0.15f);
    band_set(BP_FREEZE, 0.0f);
    band_set(BP_CRUSH, 0.0f);
    band_set(BP_RING, 0.0f);
    band_set(BP_FILTER, 0.0f);
    band_set(BP_PITCH, 0.0f);
}
void music_over(void)
{
    mu_stop();
    mu.section = SEC_OVER;
    mu.over_gap = GN_OVER_GAP;
}
void music_silence(void)
{
    mu_stop();
    mu.section = SEC_TITLE;
}
void music_stage(uint32_t stage, uint32_t round, uint32_t seed)
{
    const gn_stage_t *r;
    float b0, b1;
    uint32_t i, k;
    stage = stage >= GN_NSTAGES ? GN_NSTAGES - 1u : stage;
    r = &GN_STAGES[stage];
    if (!mu.run)                         /* a run's first stage: the title's transport stops and the run starts */
        mu_stop();
    mu.rng = seed ? seed : 0x474E4F4Du;
    mu.stage = stage;
    mu.round = round;
    mu.row = r;
    mu.genre = round ? GENRE_HARD : r->genre;
    mu.kit = round ? KIT_GLITCH : r->kit;
    mu.theme = r->theme;
    b0 = (float)(r->bpm0 + 6u * round);
    b1 = (float)(r->bpm1 + 6u * round);
    b0 = b0 > (float)GN_BPM_CAP ? (float)GN_BPM_CAP : b0;
    b1 = b1 > (float)GN_BPM_CAP ? (float)GN_BPM_CAP : b1;
    mu.root = 60u + (5u * round) % 12u;
    {                                    /* endless: the tunes cycle 0..7 again on row 7's groove */
        const gn_stage_t *t = round ? &GN_STAGES[(round - 1u) % GN_NSTAGES] : r;
        mu_start(SEC_INTRO, &GN_TUNES[t->tune], b0, t->mode);
        mu.run = 1;
        mu.bpm1 = b1;
        mu_mode_build(mu.mode, t->mode, round);
    }
    mu.fall = (uint32_t)r->fall_q * GN_PPQ / 4u;
    mu.fall_steps = mu.fall / GN_STEP;
    mu.bk.tb = -(int32_t)r->intro_bars;
    mu.sp = mu.bk;
    for (k = 0; k < mu.fall_steps; k++)
        mu_cur_advance(&mu.sp);
    mu.last_mush = 0;
    for (i = 0; i < GN_LANES; i++)
        mu.lane_last[i] = mu.mel_last[i] = 0;
    mu.chain_end = MU_NONE;
    mu.chain_rest = 0;
    mu.agaric = mu.stash = mu.dcap_mask = 0;
    mu.first_stash = r->first_stash;
    band_set(BP_LEAD_PATCH, (float)r->lead);
    band_set(BP_LEAD_CUTOFF, mu.genre == GENRE_FOLK ? 0.3f : MU_LEAD_CUTOFF[r->lead]);
    band_set(BP_LEAD_RESO, 0.3f);
    band_set(BP_LEAD_DECAY, 0.4f);
    band_set(BP_LEAD_RELEASE, 0.35f);
    band_set(BP_LEAD_DETUNE, 0.0f);
    band_set(BP_LEAD_GLIDE, 0.0f);
    band_set(BP_LEAD_LEVEL, 0.8f);
    band_set(BP_BASS_STYLE, (float)r->bass);
    band_set(BP_BASS_LEVEL, 0.8f);
    band_set(BP_STAB_STYLE, (float)r->stab);
    band_set(BP_STAB_CUTOFF, mu.genre == GENRE_FOLK ? 0.2f : MU_STAB_CUTOFF[r->stab]);
    band_set(BP_STAB_DECAY, mu.genre == GENRE_HARD ? 0.0f : 0.35f);
    band_set(BP_STAB_LEVEL, mu.genre == GENRE_FOLK ? 0.2f : 0.8f);
    band_set(BP_KIT, (float)mu.kit);
    band_set(BP_DRUM_LEVEL, mu.genre == GENRE_FOLK ? 0.6f : mu.genre == GENRE_HARD ? 0.95f : 0.75f);
    band_set(BP_DRUM_CRUSH, mu.kit == KIT_GLITCH ? 0.6f : mu.kit == KIT_BREAKS ? 0.3f : 0.0f);
    band_set(BP_DUCK, (float)r->duck * 0.01f);
    band_set(BP_ENS, (float)(r->ens == 0u ? ENS_OFF : r->ens < 35u ? ENS_CHORUS1 : r->ens < 55u ? ENS_CHORUS2 : ENS_ENSEMBLE));
    band_set(BP_ENS_MIX, (float)r->ens * 0.01f);
    band_set(BP_DLY_SEND, (float)r->dly_send * 0.01f);
    band_set(BP_DLY_DIV, mu.genre == GENRE_DEEP || mu.genre == GENRE_BREAKS ? 3.0f : 2.0f);
    band_set(BP_DLY_FDBK, 0.4f);
    band_set(BP_REV_SEND, (float)r->rev_send * 0.01f);
    band_set(BP_REV_SIZE, mu.genre == GENRE_FOLK ? 0.5f : mu.genre == GENRE_HARD ? 0.6f : 0.4f);
    band_set(BP_REV_DAMP, 0.4f);
    band_set(BP_SHIMMER, (float)r->shimmer * 0.01f);
    band_set(BP_FREEZE, 0.0f);
    band_set(BP_CRUSH, (float)r->crush * 0.01f);
    band_set(BP_RING, (float)r->ring * 0.01f);
    band_set(BP_FILTER, 0.0f);
    band_set(BP_PITCH, 0.0f);
    game_on_section(SEC_INTRO);
}
void music_tick(uint32_t n)
{
    if (mu.hold)
        return;
    if (mu.over_gap) {
        if (--mu.over_gap == 0u)
            mu_over_start();
        return;
    }
    if (!mu.running)
        return;
    if (mu.step_due) {
        mu.step_due = 0;
        mu_step();
    }
    mu.acc += n << MU_Q;
    while (mu.running && mu.acc >= mu.spt) {
        mu.acc -= mu.spt;
        mu.now++;
        if ((mu.now % GN_CLOCK_TICKS) == 0u)
            gn_midi_realtime(0xF8);
        mu_q_run();
        if (mu.now == mu.step_tick)
            mu_step();
        if (mu.step_due) {               /* a stage that began inside this block: its first step */
            mu.step_due = 0;
            mu_step();
        }
    }
}
uint32_t music_now(void) { return mu.now; }
float music_now_f(void) { return (float)mu.now + (float)mu.acc / (float)mu.spt; }
uint32_t music_bpm(void) { return (uint32_t)(mu.bpm + 0.5f); }
uint32_t music_section(void) { return mu.section; }
uint32_t music_bar(void) { return mu.bar; }
uint32_t music_bar_ticks(void) { return mu_bar_steps(mu.bk.tb) * GN_STEP; }
uint32_t music_beat_q8(void)
{
    float f = (float)((mu.now - mu.bar_tick) % GN_PPQ) + (float)mu.acc / (float)mu.spt;
    return (uint32_t)(f * (256.0f / (float)GN_PPQ)) & 255u;
}
uint32_t music_fall(void) { return mu.fall; }
float music_ms_ticks(float ms) { return ms * mu.bpm * mu.rate * (float)GN_PPQ * (1.0f / 60000.0f); }
void music_rate(float r)
{
    mu.rate = r > 0.05f ? r : 1.0f;
    mu_set_spt();
}
void music_hold(uint32_t reason, int on)
{
    uint32_t was = mu.hold;
    if (on)
        mu.hold |= reason;
    else
        mu.hold &= ~reason;
    if (mu.running) {
        if (!was && mu.hold)
            gn_midi_realtime(0xFC);
        else if (was && !mu.hold)
            gn_midi_realtime(0xFB);      /* Continue: a slaved sequencer resumes in place, not from the top */
    }
    if (reason & HOLD_FREEZE) {
        band_set(BP_FREEZE, on ? 1.0f : 0.0f);
        band_stutter(8, on ? 8u : 0u);
    }
}
void music_arp(int on)
{
    mu.arp = on != 0;
    mu.arp_i = 0;
}
void music_bump(int32_t bpm)
{
    if (!mu.round || !mu.run)
        return;
    mu.bpm0 = fm_clampf(mu.bpm0 + (float)bpm, 40.0f, (float)GN_BPM_CAP);
    mu.bpm1 = fm_clampf(mu.bpm1 + (float)bpm, 40.0f, (float)GN_BPM_CAP);
}
uint32_t music_lane_note(uint32_t lane, int32_t reg)
{
    reg = reg < -1 ? -1 : reg > 1 ? 1 : reg;
    return mu_clamp_note((int32_t)mu.root + 12 * reg + mu.mode[lane % GN_LANES]);
}
uint32_t music_spice_note(uint32_t black)
{
    static const uint8_t left[GN_NBLACK] = GN_BLACK_LEFT;
    static const int8_t reg[GN_NBLACK] = GN_BLACK_REG;
    static const uint8_t letter[GN_NWHITE] = GN_WKEY_LETTER;
    uint32_t w, pl, pr;
    black = black >= GN_NBLACK ? GN_NBLACK - 1u : black;
    w = left[black];
    pl = music_lane_note(letter[w], reg[black]);
    pr = music_lane_note(letter[w + 1u], reg[black]);
    return mu_clamp_note((int32_t)(pl + 1u < pr ? pl + 1u : pr + 1u));
}
void music_chord_lanes(uint8_t lanes[3])
{
    uint32_t r = (mu.chord & 15u) - 1u;
    lanes[0] = (uint8_t)(r % 7u);
    lanes[1] = (uint8_t)((r + 2u) % 7u);
    lanes[2] = (uint8_t)((r + 4u) % 7u);
}
uint32_t music_stash_pick(void)
{
    const gn_stage_t *r = mu.row;
    uint32_t i, tot = 0, pick;
    if (mu.first_stash != AB_NONE) {
        i = mu.first_stash;
        mu.first_stash = AB_NONE;
        return i;
    }
    for (i = 0; i < 8u; i++)
        tot += r->stash_w[i];
    if (!tot)
        return AB_BURP;
    pick = mu_rnd_n(tot);
    for (i = 0; i < 8u; i++) {
        if (pick < r->stash_w[i])
            return AB_ARP + i;
        pick -= r->stash_w[i];
    }
    return AB_BURP;
}
