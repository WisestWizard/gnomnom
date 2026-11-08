/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann */
/* GNOMNOM's band (its own -O2 unit): six LEAD voices, a mono BASS, three STAB voices, nine drums and a riser over
 * the three buses gn.h's band_render comment lays down (DRY, BAND-WET with the sidechain, ring mod and decimator,
 * LEAD joining after them, the stutter ring), then the master effects (fx.c) with the hall a block behind on the
 * second core, the DJ filter and the limiter. Every parameter moves on the control tick so nothing clicks. */
#include <stdint.h>
#include "gn.h"
#include "fastmath.h"
#include "plat.h"                        /* the second core only: plat_cpu2_* */
#ifndef FM1B_CPU2
#define FM1B_CPU2 0
#endif
#include "band.h"

#define FS GN_FS
#define CTL GN_CTL
#define MAXBLOCK BAND_MAXBLOCK
#define NT (MAXBLOCK / CTL)              /* control ticks a block */
#define NLEAD BAND_NLEAD
#define NSTAB BAND_NSTAB
#define RING BAND_RING_FRAMES
#define RING_FADE 88u                    /* the stutter's crossfades: 2 ms */
#define RING_SCALE 16384.0f              /* the ring holds +-2 */
#define MAX_INC 0.45f
#define K20 0.035617f                    /* the one-pole on the control tick: a 20 ms time constant */
#define K100 0.0072272f                  /* 100 ms: BP_PITCH */
#define FULL 8300000.0f                  /* 24-bit full scale at master 4096, a hair under 2^23 */
#define WET_CLIP 0.36f                   /* the wet bus into the effects, as DEEP FIELD's: room before the limiter */
#define MIDI_DRUM_TICKS 69u              /* 50 ms: a drum's note-off */

/* the parts' gains: each matched by RMS over its first 200 ms at full velocity (the harness measures them), the
 * kick by its peak; MIX_TRIM takes the whole band down 6 dB together, because the parts at their matched levels
 * sum 7 dB into the limiter, which would then duck the LEAD under every kick */
#define MIX_TRIM 0.5f
#define LEAD_G (2.36f * MIX_TRIM)
#define BASS_G (0.83f * MIX_TRIM)
#define STAB_G (1.33f * MIX_TRIM)
#define DRUM_G (1.0f * MIX_TRIM)
#define KICK_G 0.61f
#define SNARE_G 12.9f
#define HAT_G 175.0f
#define OHAT_G 62.8f
#define CLAP_G 20.3f
#define RIM_G 3.85f
#define CRASH_G 1.8f
#define TOM_G 1.06f
#define SHAKER_G 3.15f
#define RISER_G (0.67f * MIX_TRIM)
#define SUB_G (0.4f * MIX_TRIM)

typedef struct { float level; uint8_t stage; } bd_env_t;   /* 0 idle, 1 attack, 2 decay, 3 sustain, 4 release */

typedef struct {
    uint8_t active, gate, self, note, vel, patch, soft, pad;
    uint32_t age;
    float pitch, pitch_tgt, glide;       /* glide: semitones a tick, 0 none */
    float ph1, ph2, phm, phs;            /* the two oscillators, the BELL's modulator, the PLUCK's sub */
    float ic1, ic2;                      /* the SVF */
    float amp, click, sour, wob_ph;      /* sour: samples left of the poison detune */
    float w2;                            /* the second oscillator's weight 0..1, slewed: switching it clicks */
    bd_env_t aenv, fenv;
} bd_lead_t;

typedef struct {
    uint8_t active, gate, acc, note, vel, style, pad[2];
    uint32_t gate_left;                  /* samples to the self gate-off (ACID), 0 none */
    float pitch, pitch_tgt, glide, fold;
    float ph, phs;
    float y1, y2, y3, y4;                /* the ladder */
    float amp;
    bd_env_t aenv, fenv;
} bd_bass_t;

typedef struct {
    uint8_t active, gate, note, vel, style, self, pad[2];
    uint32_t age;
    float pitch, penv, ph1, ph2, php, amp;
    bd_env_t aenv;
} bd_stab_t;

typedef struct {
    uint8_t on, mode;
    uint32_t t, midi_left;
    float env, k, lvl, ph, hz, hz_end, kp, ic1, ic2, ic3, ic4, a1, a2, a3, hp, hpc, lp, drive, tt;
    float ph6[6], inc6[6];               /* the hat's squares */
} bd_drum_t;

typedef struct {
    uint8_t on, phase;
    uint32_t t, len;
    float ic1, ic2, ph, hz, env;
} bd_riser_t;

typedef struct {
    uint8_t on, ending;
    uint32_t len, left, pos, start, end, from, cont, w;
} bd_stut_t;

typedef struct {
    uint8_t phase;
    uint32_t t, atk_n, hold_n, rel_n;
    float g, step, floor;
} bd_duck_t;

typedef struct { float ph, hl, hr; } bd_dec_t;

typedef struct {                         /* what a control tick settles for every part (the smoothed parameters mapped) */
    uint32_t patch, bstyle, sstyle, kit;
    float pitch, tempo, step_s;
    float l_fc, l_kq, l_ad, l_ar, l_lvl, l_det;
    float b_fc, b_k, b_env, b_ad, b_ad_acc, b_lvl, b_drive;
    float s_fc, s_ad, s_lvl;
    float d_lvl;
    float ring_a, ring_cw, ring_sw;
    float crush_inc, crush_q, lcrush_inc, lcrush_q, dcrush_inc, dcrush_q;
    uint32_t crush_on, lcrush_on, dcrush_on;
    float filter, duck, tlp_a;           /* tlp_a: the trip's low-pass on BAND-WET, 0 open */
} bd_blk_t;

typedef struct {                         /* the second core's job: its own cache lines, nothing else on them */
    fx_params_t p;
    float *in_l, *in_r, *out_l, *out_r;
    uint32_t n;
} __attribute__((aligned(32))) bd_job_t;

typedef struct {
    bd_lead_t lead[NLEAD];
    bd_bass_t bass;
    bd_stab_t stab[NSTAB];
    bd_drum_t drum[DR_COUNT];
    bd_riser_t riser;
    bd_stut_t stut;
    bd_duck_t duck;
    bd_dec_t dec_wet, dec_lead, dec_drum;
    bd_blk_t k;
    float ring_c, ring_s;                /* the ring modulator's carrier, a rotating pair */
    float dj_ic1[2], dj_ic2[2];
    float tlp[4];                        /* the trip's low-pass: left, its second pole, right, its second pole */
    float st_fenv, st_ic1, st_ic2, st_pwm;   /* STAB: the shared filter's envelope and state, the hoover's PWM */
    float gain_now, pk[5];
    uint32_t rng, age, hall_n;
    fx_params_t fxp;
    float sb[CTL], wd[CTL], dk[CTL];     /* a tick's scratch: the stabs, the crushed drums, the duck curve */
    float dj_v[NT];
    int16_t *ring;
    float *dry_l, *dry_r, *wet_l, *wet_r, *lead_m, *hall_in_l, *hall_in_r, *hall_out_l, *hall_out_r;
    bd_job_t *job;
} bd_t;

static bd_t *bd;
static uint32_t bd_used;
static float bd_tgt[BP_COUNT], bd_cur[BP_COUNT];   /* outside the arena: band_set works whatever band_init did */
static void (*bd_midi)(uint32_t, uint32_t, uint32_t, int);
static struct {                          /* fixed envelope coefficients, from band_init */
    float aa1, aa2, aa20, ad120, ad300, ad400, ad1500, ar30, ar60, ar150, ar2000, click, penv, sfenv, sub_kp, sub_k;
} bdc;

typedef struct { float kick_dec, kick_lvl, kick_drive, snare_len, snare_lvl, hat_crush, hat_dec, bright, clap_lvl; } bd_kit_t;
static const bd_kit_t BD_KITS[KIT_COUNT] = {
    {0.120f, 0.70f, 0.0f, 0.200f, 0.9f, 0.0f, 0.8f, 0.85f, 0.5f},   /* BRUSH: soft and short */
    {0.320f, 1.0f, 0.0f, 0.120f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f},     /* HOUSE: classic */
    {0.420f, 1.0f, 1.0f, 0.130f, 1.0f, 0.0f, 1.0f, 1.25f, 0.8f},    /* TECHNO: hard kick, bright hats */
    {0.250f, 1.0f, 0.3f, 0.100f, 1.1f, 0.0f, 0.7f, 1.15f, 0.9f},    /* BREAKS: snappy snare, crisp hats */
    {0.180f, 1.0f, 0.5f, 0.060f, 1.3f, 1.0f, 0.6f, 1.0f, 0.7f},     /* GLITCH: crushed */
};
static const float BD_BASS_PRESET[BS_COUNT][5] = {   /* cutoff, reso, envmod, decay, drive: DESIGN.md section 4 */
    {0.5f, 0.1f, 0.0f, 0.4f, 0.0f},
    {0.35f, 0.3f, 0.3f, 0.3f, 0.0f},
    {0.25f, 0.8f, 0.75f, 0.35f, 0.2f},
    {0.5f, 0.5f, 0.4f, 0.35f, 0.7f},
};
static const float BD_LEAD_TRIM[LP_COUNT] = {1.43f, 1.0f, 0.556f, 0.776f};     /* PLUCK SAW SQUARE BELL to one level */
static const float BD_BASS_TRIM[BS_COUNT] = {1.78f, 1.0f, 1.44f, 0.79f};       /* SOFT HOUSE ACID HARD */
static const float BD_STAB_TRIM[SS_COUNT] = {1.0f, 1.0f, 0.485f, 0.425f};      /* OFF STAB PAD HOOVER */
static const uint8_t BD_DRUM_GM[DR_COUNT] = {36, 38, 42, 46, 39, 37, 49, 45, 70};
static const float BD_HAT_HZ[6] = {205.0f, 304.0f, 369.0f, 522.0f, 540.0f, 800.0f};

/* ------------------------------------------------------------------ helpers --- */
static inline float bd_frand(uint32_t *s)   /* -1..1 */
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}
static inline float bd_rnd01(uint32_t *s) { return 0.5f + 0.5f * bd_frand(s); }
static inline float bd_hz(float n) { return 440.0f * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)); }
static inline float bd_coef(float t, float k) { return 1.0f - fm_expf(-(float)CTL * k / (t * FS)); }   /* k time
                                                                             * constants of a tick one-pole in t s */
static inline float bd_clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }
static inline float bd_softclip(float x)     /* cubic, smooth to +-1 at +-1.5 */
{
    x = x > 1.5f ? 1.5f : x < -1.5f ? -1.5f : x;
    return x - x * x * x * (4.0f / 27.0f);
}
static inline float bd_blep(float t, float dt)   /* the polyBLEP residual: two samples a discontinuity */
{
    if (t < dt) {
        t *= 1.0f / dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) * (1.0f / dt);
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
static inline float bd_saw(float ph, float dt) { return 2.0f * ph - 1.0f - bd_blep(ph, dt); }
static inline float bd_pulse(float ph, float dt, float pw)
{
    float t2 = ph + 1.0f - pw;
    if (t2 >= 1.0f)
        t2 -= 1.0f;
    return (ph < pw ? 1.0f : -1.0f) + bd_blep(ph, dt) - bd_blep(t2, dt) - (2.0f * pw - 1.0f);
}
static inline float bd_tri(float ph) { return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; }
static inline float bd_wrap(float ph) { return ph >= 1.0f ? ph - 1.0f : ph; }
static inline float bd_asin_turns(float x)   /* asin(x) in turns, |x| <= 1 (Abramowitz-Stegun 4.4.45) */
{
    float a = fm_fabsf(x), r;
    if (a > 1.0f)
        a = 1.0f;
    r = 1.5707964f - fm_sqrtf(1.0f - a) * (1.5707288f + a * (-0.2121144f + a * (0.0742610f - 0.0187293f * a)));
    return (x < 0.0f ? -r : r) * (1.0f / FM_TWO_PI);
}
/* the trapezoidal SVF (Zavalishin): coefficients for a cutoff and k = 1/Q, then one step */
static inline void bd_svf_coef(float fc, float kq, float *a1, float *a2, float *a3)
{
    float g;
    fc = fc < 20.0f ? 20.0f : fc > 0.45f * FS ? 0.45f * FS : fc;
    g = fm_tanf(FM_PI * fc * (1.0f / FS));
    *a1 = 1.0f / (1.0f + g * (g + kq));
    *a2 = g * *a1;
    *a3 = g * *a2;
}
static inline void bd_svf(float x, float *ic1, float *ic2, float a1, float a2, float a3, float *bp, float *lp)
{
    float v3 = x - *ic2, v1 = a1 * *ic1 + a2 * v3, v2 = *ic2 + a2 * *ic1 + a3 * v3;
    *ic1 = v1 + v1 - *ic1;
    *ic2 = v2 + v2 - *ic2;
    *bp = v1;
    *lp = v2;
}
static inline void bd_env_tick(bd_env_t *e, float a, float d, float s, float r)
{
    switch (e->stage) {
    case 1:
        e->level += (1.25f - e->level) * a;  /* aims past 1 so the attack ends with slope */
        if (e->level >= 1.0f) {
            e->level = 1.0f;
            e->stage = 2;
        }
        break;
    case 2:
        e->level += (s - e->level) * d;
        if (s < 0.0003f && e->level < 0.0003f) {   /* no sustain: the note ends on its own */
            e->level = 0.0f;
            e->stage = 0;
        } else if (fm_fabsf(e->level - s) < 0.001f) {
            e->level = s;
            e->stage = 3;
        }
        break;
    case 3:
        e->level += (s - e->level) * d;
        if (s < 0.0003f && e->level < 0.0003f) {
            e->level = 0.0f;
            e->stage = 0;
        }
        break;
    case 4:
        e->level -= e->level * r;
        if (e->level < 0.0003f) {
            e->level = 0.0f;
            e->stage = 0;
        }
        break;
    default:
        break;
    }
}
static inline void bd_midi_send(uint32_t ch, uint32_t note, uint32_t vel, int on)
{
    if (bd_midi)
        bd_midi(ch, note, vel, on);
}
static inline uint32_t bd_disc(uint32_t p) { return (uint32_t)(bd_tgt[p] + 0.5f); }
static inline uint32_t bd_is_disc(uint32_t p)
{
    return p == BP_LEAD_PATCH || p == BP_BASS_STYLE || p == BP_STAB_STYLE || p == BP_KIT || p == BP_ENS
           || p == BP_DLY_DIV || p == BP_FREEZE || p == BP_TEMPO;
}
static inline uint32_t bd_rwrap(uint32_t i) { return i >= RING ? i - RING : i; }
static inline int16_t bd_f2i16(float x)
{
    x *= RING_SCALE;
    return (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x);
}

/* ------------------------------------------------------------------ the control tick's parameters --- */
static void bd_tick_params(bd_t *b)
{
    bd_blk_t *k = &b->k;
    uint32_t p;
    float c, w;
    for (p = 0; p < BP_COUNT; p++) {
        float cur = bd_cur[p], tgt = bd_tgt[p];
        if (bd_is_disc(p)) {
            cur = tgt;
        } else {
            cur += (tgt - cur) * (p == BP_PITCH ? K100 : K20);
            if (fm_fabsf(tgt - cur) < 1e-5f)
                cur = tgt;
        }
        bd_cur[p] = cur;
    }
    k->patch = (uint32_t)bd_cur[BP_LEAD_PATCH];
    k->bstyle = (uint32_t)bd_cur[BP_BASS_STYLE];
    k->sstyle = (uint32_t)bd_cur[BP_STAB_STYLE];
    k->kit = (uint32_t)bd_cur[BP_KIT];
    k->pitch = bd_cur[BP_PITCH];
    k->tempo = bd_cur[BP_TEMPO];
    k->step_s = 15.0f / k->tempo;
    k->l_fc = 200.0f * fm_exp2f(bd_cur[BP_LEAD_CUTOFF] * 5.9069f + 2.0f * bd_cur[BP_LEAD_TONE]);   /* 200 Hz..12 kHz */
    k->l_kq = 1.0f / (0.5f * fm_exp2f(bd_cur[BP_LEAD_RESO] * 4.585f));                           /* Q 0.5..12 */
    k->l_ad = bd_coef(0.06f * fm_exp2f(bd_cur[BP_LEAD_DECAY] * 4.3219f), 4.0f);                   /* 60 ms..1.2 s */
    k->l_ar = bd_coef(0.03f * fm_exp2f(bd_cur[BP_LEAD_RELEASE] * 4.9069f), 4.0f);                 /* 30..900 ms */
    k->l_lvl = bd_cur[BP_LEAD_LEVEL] * LEAD_G;
    k->l_det = bd_cur[BP_LEAD_DETUNE] * 0.09f;
    k->b_fc = 60.0f * fm_exp2f(bd_cur[BP_BASS_CUTOFF] * 6.0589f);                                 /* 60 Hz..4 kHz */
    k->b_k = bd_cur[BP_BASS_RESO] * 3.9f;
    k->b_env = bd_cur[BP_BASS_ENVMOD];
    c = 0.04f * fm_exp2f(bd_cur[BP_BASS_DECAY] * 3.9069f);                                        /* 40..600 ms */
    k->b_ad = bd_coef(c, 4.0f);
    k->b_ad_acc = bd_coef(c * 0.6f, 4.0f);
    k->b_lvl = bd_cur[BP_BASS_LEVEL] * BASS_G;
    k->b_drive = bd_cur[BP_BASS_DRIVE];
    k->s_fc = 200.0f * fm_exp2f(bd_cur[BP_STAB_CUTOFF] * 5.3219f);                                /* 200 Hz..8 kHz */
    k->s_ad = bd_coef(0.1f * fm_exp2f(bd_cur[BP_STAB_DECAY] * 3.0f), 4.0f);                       /* 100..800 ms */
    k->s_lvl = bd_cur[BP_STAB_LEVEL] * STAB_G;
    k->d_lvl = bd_cur[BP_DRUM_LEVEL] * (DRUM_G / 0.7f);
    c = bd_cur[BP_DRUM_CRUSH];
    if (BD_KITS[k->kit].hat_crush > c)
        c = BD_KITS[k->kit].hat_crush;
    k->dcrush_on = c > 0.002f;
    k->dcrush_inc = fm_exp2f(c * -2.463f);                       /* the hold rate down to 8 kHz */
    k->dcrush_q = fm_exp2f(15.0f - 7.0f * c);                    /* 16 down to 8 bits */
    k->ring_a = bd_cur[BP_RING];
    w = FM_TWO_PI * bd_cur[BP_RING_HZ] * (1.0f / FS);
    k->ring_cw = fm_cosf(w);
    k->ring_sw = fm_sinf(w);
    c = bd_cur[BP_CRUSH];
    k->crush_on = c > 0.002f;
    k->crush_inc = fm_exp2f(c * -2.878f);                        /* 44.1 kHz down to 6 kHz, in log */
    k->crush_q = fm_exp2f(15.0f - 10.0f * c);                    /* 16 down to 6 bits */
    c *= 0.4f;
    k->lcrush_on = c > 0.002f;
    k->lcrush_inc = fm_exp2f(c * -2.878f);
    k->lcrush_q = fm_exp2f(15.0f - 10.0f * c);
    k->filter = bd_cur[BP_FILTER];
    k->duck = bd_cur[BP_DUCK];
    c = bd_cur[BP_TRIP];                 /* 20 kHz at 0 (a step nobody hears), 700 Hz at -1 */
    k->tlp_a = c < -0.001f ? 1.0f - fm_exp2f(-9.0647f * 20000.0f * fm_exp2f(4.836f * c) * (1.0f / FS)) : 0.0f;
}

static void bd_trip_lp(bd_t *b, float a, uint32_t m, float *l, float *r)
{
    float l1 = b->tlp[0], l2 = b->tlp[1], r1 = b->tlp[2], r2 = b->tlp[3];
    uint32_t j;
    if (a <= 0.0f) {                     /* open: the poles follow the bus, so closing again starts where it is */
        b->tlp[0] = b->tlp[1] = l[m - 1u];
        b->tlp[2] = b->tlp[3] = r[m - 1u];
        return;
    }
    for (j = 0; j < m; j++) {
        l1 += a * (l[j] - l1);
        l2 += a * (l1 - l2);
        r1 += a * (r[j] - r1);
        r2 += a * (r1 - r2);
        l[j] = l2;
        r[j] = r2;
    }
    b->tlp[0] = fm_flush(l1);
    b->tlp[1] = fm_flush(l2);
    b->tlp[2] = fm_flush(r1);
    b->tlp[3] = fm_flush(r2);
}

/* ------------------------------------------------------------------ LEAD --- */
static void bd_lead_release(bd_lead_t *v)
{
    if (!v->gate)
        return;
    v->gate = 0;
    v->aenv.stage = 4;
    v->fenv.stage = 4;
    bd_midi_send(0, v->note, 0, 0);
}

static void bd_lead_chunk(bd_lead_t *v, const bd_blk_t *k, uint32_t m, float *out)
{
    float pitch, inc1, inc2, fc, a1, a2, a3, tgt, step, d1 = 0.0f, d2 = 0.0f, mul = 1.0f, idx = 0.0f, ad;
    float ph1 = v->ph1, ph2 = v->ph2, phm = v->phm, phs = v->phs, ic1 = v->ic1, ic2 = v->ic2, amp = v->amp;
    float w2, w2_to, w2_step;
    uint32_t j, two, patch = v->patch;
    ad = v->soft ? bdc.ad120 : k->l_ad;
    bd_env_tick(&v->aenv, bdc.aa2, ad, 0.5f, k->l_ar);
    bd_env_tick(&v->fenv, bdc.aa2, ad, 0.0f, k->l_ar);
    if (v->self && v->gate && v->aenv.stage == 3)
        bd_lead_release(v);              /* BN_NOTEOFF_SELF: the decay done, the voice lets itself go */
    if (!v->aenv.stage) {
        bd_lead_release(v);              /* (gone while held: the off is owed) */
        v->active = 0;
        v->amp = 0.0f;
        return;
    }
    if (v->glide != 0.0f) {
        v->pitch += v->glide;
        if ((v->glide > 0.0f && v->pitch >= v->pitch_tgt) || (v->glide < 0.0f && v->pitch <= v->pitch_tgt)) {
            v->pitch = v->pitch_tgt;
            v->glide = 0.0f;
        }
    }
    pitch = v->pitch + k->pitch;
    if (patch == LP_BELL) {
        pitch += 0.02f * fm_sin_turns(v->wob_ph);
        v->wob_ph = bd_wrap(v->wob_ph + 5.0f * (float)CTL / FS);
        idx = (0.3f + 1.7f * v->fenv.level) * (1.0f / FM_TWO_PI);   /* the index, 2 -> 0.3, in turns */
    }
    if (v->sour > 0.0f) {                /* BN_SOUR: the pair pulled apart and twice as loud for 200 ms, then eased back */
        float s = v->sour * (1.0f / (0.05f * FS));
        if (s > 1.0f)
            s = 1.0f;
        v->sour -= (float)CTL;
        d1 = -0.4f * s;
        d2 = 0.7f * s;
        mul = 1.0f + s;
    }
    w2 = v->w2;
    w2_to = k->l_det > 0.0005f || d2 != 0.0f ? 1.0f : 0.0f;
    w2_to = w2 + (w2_to - w2) * 0.25f;   /* about 3 ms to settle */
    w2_to = w2_to < 0.001f ? 0.0f : w2_to > 0.999f ? 1.0f : w2_to;
    w2_step = (w2_to - w2) / (float)m;
    two = w2 > 0.0f || w2_to > 0.0f;
    inc1 = bd_hz(pitch + d1) * (1.0f / FS);
    inc2 = bd_hz(pitch + k->l_det + d2) * (1.0f / FS);
    inc1 = inc1 > MAX_INC ? MAX_INC : inc1;
    inc2 = inc2 > MAX_INC ? MAX_INC : inc2;
    fc = k->l_fc * (0.4f + 0.6f * v->fenv.level);
    bd_svf_coef(fc, k->l_kq, &a1, &a2, &a3);
    if (patch == LP_PLUCK) {             /* the pizzicato: a 2 ms snap down to the body */
        v->click = fm_flush(v->click * bdc.click);
        mul *= 0.55f + 0.45f * v->click;
    }
    tgt = v->aenv.level * (float)v->vel * (1.0f / 127.0f) * k->l_lvl * BD_LEAD_TRIM[patch] * mul;
    step = (tgt - amp) / (float)m;
    for (j = 0; j < m; j++) {
        float x, x2 = 0.0f, bp, y;
        ph1 = bd_wrap(ph1 + inc1);
        if (two)
            ph2 = bd_wrap(ph2 + inc2);
        switch (patch) {
        case LP_PLUCK:
            phs = bd_wrap(phs + 0.5f * inc1);
            x = bd_tri(ph1) + 0.5f * fm_sin_turns(phs);
            if (two)
                x2 = bd_tri(ph2);
            break;
        case LP_SQUARE:
            x = bd_pulse(ph1, inc1, 0.5f);
            if (two)
                x2 = bd_pulse(ph2, inc2, 0.5f);
            break;
        case LP_BELL: {
            float md;
            phm = bd_wrap(bd_wrap(phm + 3.5f * inc1));
            md = idx * fm_sin_turns(phm);
            x = fm_sin_turns(ph1 + md);
            if (two)
                x2 = fm_sin_turns(ph2 + md);
            break;
        }
        default:
            x = bd_saw(ph1, inc1);
            if (two)
                x2 = bd_saw(ph2, inc2);
            break;
        }
        w2 += w2_step;
        x = x * (1.0f - 0.29f * w2) + x2 * (0.71f * w2);   /* w2 1: both at 0.71, as the pair always summed */
        bd_svf(x, &ic1, &ic2, a1, a2, a3, &bp, &y);
        amp += step;
        out[j] += y * amp;
    }
    v->ph1 = ph1;
    v->ph2 = ph2;
    v->phm = phm;
    v->phs = phs;
    v->ic1 = fm_flush(ic1);
    v->ic2 = fm_flush(ic2);
    v->amp = amp;
    v->w2 = w2_to;
}

static bd_lead_t *bd_lead_pick(bd_t *b, uint32_t note)
{
    bd_lead_t *old = &b->lead[0];
    uint32_t i;
    for (i = 0; i < NLEAD; i++)          /* the same note again: its voice */
        if (b->lead[i].active && b->lead[i].note == note)
            return &b->lead[i];
    for (i = 0; i < NLEAD; i++)
        if (!b->lead[i].active)
            return &b->lead[i];
    for (i = 1; i < NLEAD; i++)
        if (b->lead[i].age < old->age)
            old = &b->lead[i];
    return old;
}

static void bd_lead_on(bd_t *b, uint32_t note, uint32_t vel, uint32_t flags)
{
    bd_lead_t *v, *held = 0;
    uint32_t i;
    float glide_s = 0.2f * bd_cur[BP_LEAD_GLIDE];
    for (i = 0; i < NLEAD; i++)          /* legato: a voice still held gives the glide its start */
        if (b->lead[i].active && b->lead[i].gate && (!held || b->lead[i].age > held->age))
            held = &b->lead[i];
    v = bd_lead_pick(b, note);
    if ((flags & BN_SOFT) && v->active && v->gate && v->note == note && !v->soft)
        return;                          /* the guide or an air note on an eaten note still sounding: it would
                                          * retrigger the eat at the soft level (an early eat lost its accent) */
    if (v->active && v->gate)
        bd_midi_send(0, v->note, 0, 0);  /* stolen, or the same note again: its off first */
    if (!v->active) {                    /* a fresh start; a retrigger keeps its phases and filter (no click) */
        v->ph1 = bd_rnd01(&b->rng);
        v->ph2 = bd_rnd01(&b->rng);
        v->phm = v->phs = 0.0f;
        v->ic1 = v->ic2 = 0.0f;
        v->amp = 0.0f;
        v->aenv.level = v->fenv.level = 0.0f;
        v->wob_ph = 0.0f;
        v->w2 = bd_cur[BP_LEAD_DETUNE] > 0.0005f || (flags & BN_SOUR) ? 1.0f : 0.0f;
    }
    v->active = 1;
    v->gate = 1;
    v->self = (flags & BN_NOTEOFF_SELF) != 0u;
    v->soft = (flags & BN_SOFT) != 0u;
    v->note = (uint8_t)note;
    v->vel = (uint8_t)(v->soft ? (vel + 1u) / 2u : vel);
    v->patch = (uint8_t)bd_disc(BP_LEAD_PATCH);
    v->age = ++b->age;
    v->pitch_tgt = (float)note;
    if (held && held != v && glide_s > 0.0005f) {
        v->pitch = held->pitch;
        v->glide = (v->pitch_tgt - v->pitch) / (glide_s * FS / (float)CTL);
    } else {
        v->pitch = v->pitch_tgt;
        v->glide = 0.0f;
    }
    v->aenv.stage = 1;
    v->fenv.stage = 1;
    v->click = v->patch == LP_PLUCK ? 1.0f : 0.0f;
    v->sour = (flags & BN_SOUR) ? 0.2f * FS : 0.0f;
    bd_midi_send(0, note, vel, 1);
}

/* ------------------------------------------------------------------ BASS --- */
static void bd_bass_release(bd_bass_t *v)
{
    if (!v->gate)
        return;
    v->gate = 0;
    v->gate_left = 0;
    v->aenv.stage = 4;
    v->fenv.stage = 4;
    bd_midi_send(1, v->note, 0, 0);
}

static void bd_bass_chunk(bd_bass_t *v, const bd_blk_t *k, uint32_t m, float *dl, float *dr, float *pk)
{
    float pitch, inc, fc, g1, kres, comp, envmod, tgt, step, dg, dn, peak = *pk;
    float ph = v->ph, phs = v->phs, y1 = v->y1, y2 = v->y2, y3 = v->y3, y4 = v->y4, amp = v->amp;
    uint32_t j, style = v->style, drive_on = k->b_drive > 0.01f;
    bd_env_tick(&v->aenv, bdc.aa1, style == BS_SOFT ? bdc.ad300 : bdc.ad1500, style == BS_SOFT ? 0.0f : 0.8f, bdc.ar30);
    bd_env_tick(&v->fenv, bdc.aa1, v->acc ? k->b_ad_acc : k->b_ad, 0.0f, k->b_ad);
    if (v->gate_left) {
        if (v->gate_left <= CTL)
            bd_bass_release(v);
        else
            v->gate_left -= CTL;
    }
    if (!v->aenv.stage) {
        bd_bass_release(v);
        v->active = 0;
        v->amp = 0.0f;
        return;
    }
    if (v->glide != 0.0f) {
        v->pitch += v->glide;
        if ((v->glide > 0.0f && v->pitch >= v->pitch_tgt) || (v->glide < 0.0f && v->pitch <= v->pitch_tgt)) {
            v->pitch = v->pitch_tgt;
            v->glide = 0.0f;
        }
    }
    pitch = v->pitch + k->pitch + v->fold;
    inc = bd_hz(pitch) * (1.0f / FS);
    inc = inc > MAX_INC ? MAX_INC : inc;
    envmod = k->b_env * (v->acc ? 1.4f : 1.0f);
    fc = k->b_fc * fm_exp2f(6.0f * envmod * v->fenv.level);
    fc = fc < 20.0f ? 20.0f : fc > 6000.0f ? 6000.0f : fc;   /* the ladder's unit delay: stable to here */
    g1 = 1.0f - fm_expf(-FM_TWO_PI * fc * (1.0f / FS));
    kres = k->b_k + (v->acc ? 0.3f : 0.0f);
    kres = kres > 3.95f ? 3.95f : kres;
    comp = 1.0f + 0.4f * kres;           /* the bass the feedback takes, given back */
    dg = 1.0f + 5.0f * k->b_drive;
    dn = 1.0f / (1.0f + 1.2f * k->b_drive);
    tgt = v->aenv.level * (float)v->vel * (1.0f / 127.0f) * k->b_lvl * BD_BASS_TRIM[style] * (v->acc ? 1.585f : 1.0f);
    step = (tgt - amp) / (float)m;
    for (j = 0; j < m; j++) {
        float x, u, y, a;
        ph = bd_wrap(ph + inc);
        phs = bd_wrap(phs + 0.5f * inc);
        if (style == BS_SOFT)
            x = bd_tri(ph) + 0.5f * fm_sin_turns(phs);
        else if (style == BS_HARD)
            x = bd_pulse(ph, inc, 0.5f) + 0.5f * bd_pulse(phs, 0.5f * inc, 0.5f);
        else
            x = bd_saw(ph, inc);
        u = x - kres * bd_softclip(y4);
        y1 += g1 * (u - y1);
        y2 += g1 * (y1 - y2);
        y3 += g1 * (y2 - y3);
        y4 += g1 * (y3 - y4);
        y = y4 * comp;
        if (drive_on)
            y = fm_tanhf(y * dg) * dn;
        amp += step;
        y *= amp;
        dl[j] += y;
        dr[j] += y;
        a = y < 0.0f ? -y : y;
        peak = a > peak ? a : peak;
    }
    v->ph = ph;
    v->phs = phs;
    v->y1 = fm_flush(y1);
    v->y2 = fm_flush(y2);
    v->y3 = fm_flush(y3);
    v->y4 = fm_flush(y4);
    v->amp = amp;
    *pk = peak;
}

static void bd_bass_on(bd_t *b, uint32_t note, uint32_t vel, uint32_t flags)
{
    bd_bass_t *v = &b->bass;
    uint32_t style = bd_disc(BP_BASS_STYLE), slide = (flags & BN_SLIDE) && v->active;   /* an ACID gate has ended by
                                                                                          * the next 16th: the slide
                                                                                          * still glides from it */
    float fold = 0.0f, step_s = 15.0f / bd_tgt[BP_TEMPO];
    while ((float)note + bd_tgt[BP_PITCH] + fold < 33.0f)   /* under MIDI 33 after the shift: up an octave */
        fold += 12.0f;
    if (v->active && v->gate)
        bd_midi_send(1, v->note, 0, 0);
    if (!v->active) {
        v->ph = bd_rnd01(&b->rng);
        v->phs = 0.0f;
        v->y1 = v->y2 = v->y3 = v->y4 = 0.0f;
        v->amp = 0.0f;
        v->aenv.level = v->fenv.level = 0.0f;
    }
    v->pitch_tgt = (float)note;
    if (slide) {                         /* legato: the previous gate extended, the pitch gliding over a 16th */
        v->glide = (v->pitch_tgt - v->pitch) / (step_s * FS / (float)CTL);
        if (v->aenv.stage == 4) {        /* already releasing: back to its attack from where it is, no click */
            v->aenv.stage = 1;
            v->fenv.stage = 1;
        }
    } else {
        v->pitch = v->pitch_tgt;
        v->glide = 0.0f;
        v->aenv.stage = 1;
        v->fenv.stage = 1;
        v->acc = (flags & BN_ACCENT) != 0u;
        v->vel = (uint8_t)vel;
    }
    v->fold = fold;
    v->note = (uint8_t)note;
    v->style = (uint8_t)style;
    v->active = 1;
    v->gate = 1;
    v->gate_left = style == BS_ACID ? (uint32_t)(0.55f * step_s * FS) : 0u;   /* a slid note gates itself too */
    bd_midi_send(1, note, vel, 1);
}

/* ------------------------------------------------------------------ STAB --- */
typedef struct { float aa, ad, sus, ar; uint8_t own_ad; } bd_senv_t;   /* own_ad: a fixed decay, not BP_STAB_DECAY */
static bd_senv_t bd_senv[SS_COUNT];

static void bd_stab_release(bd_stab_t *v)
{
    if (!v->gate)
        return;
    v->gate = 0;
    v->aenv.stage = 4;
    bd_midi_send(2, v->note, 0, 0);
}

static void bd_stab_chunk(bd_t *b, bd_stab_t *v, const bd_blk_t *k, uint32_t m, float *sb)
{
    const bd_senv_t *e = &bd_senv[v->style];
    float pitch, det, inc1, inc2, incp, pw = 0.5f, pl = 0.0f, tgt, step;
    float ph1 = v->ph1, ph2 = v->ph2, php = v->php, amp = v->amp;
    uint32_t j, hoover = v->style == SS_HOOVER;
    bd_env_tick(&v->aenv, e->aa, e->own_ad ? e->ad : k->s_ad, e->sus, e->ar);
    if (v->self && v->gate && v->aenv.stage == 3)
        bd_stab_release(v);              /* BN_NOTEOFF_SELF: PAD and HOOVER sustain, so nothing else would end it */
    if (!v->aenv.stage) {
        bd_stab_release(v);
        v->active = 0;
        v->amp = 0.0f;
        return;
    }
    v->penv = fm_flush(v->penv * bdc.penv);
    pitch = v->pitch + v->penv + k->pitch;
    det = hoover ? 0.4f : 0.07f;
    inc1 = bd_hz(pitch - det) * (1.0f / FS);
    inc2 = bd_hz(pitch + det) * (1.0f / FS);
    incp = 0.5f * bd_hz(pitch) * (1.0f / FS);
    inc1 = inc1 > MAX_INC ? MAX_INC : inc1;
    inc2 = inc2 > MAX_INC ? MAX_INC : inc2;
    if (hoover) {
        pw = 0.5f + 0.1f * fm_sin_turns(b->st_pwm);
        pl = 0.6f;
    }
    tgt = v->aenv.level * (float)v->vel * (1.0f / 127.0f) * k->s_lvl * BD_STAB_TRIM[v->style];
    step = (tgt - amp) / (float)m;
    for (j = 0; j < m; j++) {
        float x;
        ph1 = bd_wrap(ph1 + inc1);
        ph2 = bd_wrap(ph2 + inc2);
        x = bd_saw(ph1, inc1) + bd_saw(ph2, inc2);
        if (pl > 0.0f) {
            php = bd_wrap(php + incp);
            x += pl * bd_pulse(php, incp, pw);
        }
        amp += step;
        sb[j] += x * amp;
    }
    v->ph1 = ph1;
    v->ph2 = ph2;
    v->php = php;
    v->amp = amp;
}

static void bd_stab_on(bd_t *b, uint32_t note, uint32_t vel, uint32_t flags)
{
    bd_stab_t *v = 0;
    uint32_t i, style = bd_disc(BP_STAB_STYLE);
    if (style == SS_OFF)
        return;
    for (i = 0; i < NSTAB; i++)
        if (b->stab[i].active && b->stab[i].note == note)
            v = &b->stab[i];
    for (i = 0; !v && i < NSTAB; i++)
        if (!b->stab[i].active)
            v = &b->stab[i];
    if (!v) {
        v = &b->stab[0];
        for (i = 1; i < NSTAB; i++)
            if (b->stab[i].age < v->age)
                v = &b->stab[i];
    }
    if (v->active && v->gate)
        bd_midi_send(2, v->note, 0, 0);
    if (!v->active) {
        v->ph1 = bd_rnd01(&b->rng);
        v->ph2 = bd_rnd01(&b->rng);
        v->php = bd_rnd01(&b->rng);
        v->amp = 0.0f;
        v->aenv.level = 0.0f;
    }
    v->active = 1;
    v->gate = 1;
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->style = (uint8_t)style;
    v->self = (flags & BN_NOTEOFF_SELF) != 0u;
    v->age = ++b->age;
    v->pitch = (float)note;
    v->penv = style == SS_HOOVER ? 9.0f : 0.0f;
    v->aenv.stage = 1;
    b->st_fenv = 1.0f;
    bd_midi_send(2, note, vel, 1);
}

/* ------------------------------------------------------------------ DRUMS --- */
static void bd_duck_kick(bd_t *b)
{
    bd_duck_t *d = &b->duck;
    float duck = bd_cur[BP_DUCK];
    if (duck < 0.002f)
        return;
    d->floor = 1.0f - 0.7f * duck;
    d->atk_n = (uint32_t)(0.002f * FS);
    d->hold_n = (uint32_t)(0.030f * FS);
    d->rel_n = (uint32_t)(15.0f / bd_tgt[BP_TEMPO] * FS);
    d->step = (d->floor - d->g) / (float)d->atk_n;
    d->phase = 1;
    d->t = 0;
}

static void bd_duck_curve(bd_duck_t *d, float *dk, uint32_t m)
{
    uint32_t j;
    for (j = 0; j < m; j++) {
        switch (d->phase) {
        case 1:
            d->g += d->step;
            if (++d->t >= d->atk_n) {
                d->g = d->floor;
                d->phase = 2;
                d->t = 0;
            }
            break;
        case 2:
            if (++d->t >= d->hold_n) {
                d->phase = 3;
                d->t = 0;
                d->step = (1.0f - d->floor) / (float)d->rel_n;
            }
            break;
        case 3:
            d->g += d->step;
            if (++d->t >= d->rel_n) {
                d->g = 1.0f;
                d->phase = 0;
            }
            break;
        default:
            d->g = 1.0f;
            break;
        }
        dk[j] = d->g;
    }
}

static void bd_drum_hit(bd_t *b, uint32_t i, uint32_t vel)
{
    bd_drum_t *d = &b->drum[i];
    uint32_t kit = bd_disc(BP_KIT), slow = bd_tgt[BP_PITCH] <= -6.0f, j;
    const bd_kit_t *kt = &BD_KITS[kit];
    float v01 = (float)vel * (1.0f / 127.0f), dec = slow ? 1.5f : 1.0f, bright = kt->bright * (slow ? 0.7f : 1.0f), t;
    if (d->midi_left)
        bd_midi_send(9, BD_DRUM_GM[i], 0, 0);
    bd_midi_send(9, BD_DRUM_GM[i], vel, 1);
    d->midi_left = MIDI_DRUM_TICKS;
    d->mode = 0;
    switch (i) {
    case DR_KICK: {
        uint32_t gabber = kit == KIT_TECHNO && bd_cur[BP_DRUM_LEVEL] > 0.85f;
        float sweep = gabber ? 0.07f : 0.035f;
        if (d->on && d->env > 0.02f) {   /* retriggered: on from the phase that matches the sample sounding now */
            float cur = fm_sin_turns(d->ph) * d->env, ph = bd_asin_turns(cur);
            if (d->ph >= 0.25f && d->ph < 0.75f)   /* falling: the other half */
                ph = 0.5f - ph;
            d->ph = ph < 0.0f ? ph + 1.0f : ph;
        } else {
            d->ph = 0.0f;
        }
        d->hz = gabber ? 200.0f : 140.0f;
        d->hz_end = gabber ? 52.0f : 48.0f;
        d->kp = 1.0f - fm_expf(-3.0f / (sweep * FS));
        t = (gabber ? 0.38f : kt->kick_dec) * dec;
        d->lvl = v01 * kt->kick_lvl * KICK_G;
        d->drive = kt->kick_drive;
        d->mode = gabber ? 2u : kt->kick_drive > 0.01f ? 1u : 0u;
        d->hpc = 1.0f - fm_expf(-FM_TWO_PI * 3000.0f / FS);   /* the gabber's low-pass */
        bd_duck_kick(b);
        break;
    }
    case DR_SNARE: {
        uint32_t brush = kit == KIT_BRUSH;
        bd_svf_coef((brush ? 900.0f : 1800.0f) * bright, 1.25f, &d->a1, &d->a2, &d->a3);
        t = kt->snare_len * dec;
        d->lvl = v01 * kt->snare_lvl * SNARE_G;
        d->mode = brush;                 /* brushed: noise only (the tone keeps its phase on a roll) */
        break;
    }
    case DR_HAT:
    case DR_OHAT:
        bd_svf_coef(7000.0f * bright, 1.0f / 1.5f, &d->a1, &d->a2, &d->a3);
        d->hpc = 1.0f - fm_expf(-FM_TWO_PI * 6000.0f * bright / FS);
        for (j = 0; j < 6u; j++)
            d->inc6[j] = BD_HAT_HZ[j] * bright * (1.0f / FS);
        t = (i == DR_HAT ? 0.04f : 0.25f) * kt->hat_dec;
        d->lvl = v01 * (i == DR_HAT ? HAT_G : OHAT_G);
        break;
    case DR_CLAP:
        bd_svf_coef(1200.0f * bright, 1.0f, &d->a1, &d->a2, &d->a3);
        t = 0.110f;
        d->lvl = v01 * kt->clap_lvl * CLAP_G;
        d->tt = 0.0f;
        d->env = 0.0f;
        break;
    case DR_RIM:
        d->hz = kit == KIT_BRUSH ? 700.0f : 1000.0f;   /* a woodblock under the brushes */
        t = kit == KIT_BRUSH ? 0.04f : 0.05f;
        d->lvl = v01 * RIM_G;
        d->ph = 0.0f;
        break;
    case DR_CRASH:
        d->hpc = 1.0f - fm_expf(-FM_TWO_PI * 3000.0f / FS);
        t = 1.2f;
        d->lvl = v01 * CRASH_G;
        break;
    case DR_TOM:
        if (d->on && d->env > 0.02f) {
            float cur = fm_sin_turns(d->ph) * d->env, ph = bd_asin_turns(cur);
            if (d->ph >= 0.25f && d->ph < 0.75f)
                ph = 0.5f - ph;
            d->ph = ph < 0.0f ? ph + 1.0f : ph;
        } else {
            d->ph = 0.0f;
        }
        d->hz = 110.0f;
        d->hz_end = 70.0f;
        d->kp = 1.0f - fm_expf(-3.0f / (0.12f * FS));
        t = 0.2f;
        d->lvl = v01 * TOM_G;
        break;
    default:                             /* SHAKER */
        d->hpc = 1.0f - fm_expf(-FM_TWO_PI * 5000.0f / FS);
        t = 0.04f;
        d->lvl = v01 * SHAKER_G;
        break;
    }
    d->k = fm_expf(-6.908f / (t * FS));  /* -60 dB in t */
    if (i != DR_CLAP)
        d->env = 1.0f;
    d->t = 0;
    d->on = 1;
}

/* the drums for a tick: KICK and TOM into the dry bus, SNARE, CLAP and the hats (with their send trims) into the
 * crush group wd, RIM, SHAKER and CRASH straight into the wet bus */
static void bd_drums_chunk(bd_t *b, uint32_t m, float *dl, float *dr, float *wd, float *wl, float *wr)
{
    uint32_t i, j, rng = b->rng;
    float pk = b->pk[PART_COUNT], d_lvl = b->k.d_lvl;
    for (i = 0; i < DR_COUNT; i++) {
        bd_drum_t *d = &b->drum[i];
        float lvl, env, k;
        if (!d->on)
            continue;
        lvl = d->lvl * d_lvl;
        env = d->env;
        k = d->k;
        switch (i) {
        case DR_KICK:
        case DR_TOM:
            for (j = 0; j < m; j++) {
                float y, a;
                d->hz += (d->hz_end - d->hz) * d->kp;
                d->ph = bd_wrap(d->ph + d->hz * (1.0f / FS));
                y = fm_sin_turns(d->ph) * env;
                if (i == DR_KICK) {
                    if (d->t < 88u)      /* the 2 ms click */
                        y += bd_frand(&rng) * (1.0f - (float)d->t * (1.0f / 88.0f)) * 0.6f;
                    if (d->mode == 1u) {
                        y = bd_softclip(y * (1.0f + 1.5f * d->drive)) * 0.8f;
                    } else if (d->mode == 2u) {   /* the gabber kick: x8 into a hard clip, then 3 kHz */
                        y *= 8.0f;
                        y = y > 1.0f ? 1.0f : y < -1.0f ? -1.0f : y;
                        d->lp += (y - d->lp) * d->hpc;
                        y = d->lp;
                    }
                }
                y *= lvl;
                dl[j] += y;
                dr[j] += y;
                a = y < 0.0f ? -y : y;
                pk = a > pk ? a : pk;
                env *= k;
                d->t++;
            }
            break;
        case DR_SNARE:
            for (j = 0; j < m; j++) {
                float bp, lp, y, a;
                bd_svf(bd_frand(&rng), &d->ic1, &d->ic2, d->a1, d->a2, d->a3, &bp, &lp);
                y = bp * env;
                if (!d->mode) {
                    d->ph = bd_wrap(d->ph + 180.0f * (1.0f / FS));
                    y += fm_sin_turns(d->ph) * env * 0.5f;
                }
                y *= lvl * 0.6f;
                wd[j] += y;
                a = (y < 0.0f ? -y : y) * WET_CLIP;
                pk = a > pk ? a : pk;
                env *= k;
            }
            break;
        case DR_HAT:
        case DR_OHAT:
            for (j = 0; j < m; j++) {
                float sq = 0.0f, bp, lp, y, a;
                uint32_t q;
                for (q = 0; q < 6u; q++) {
                    d->ph6[q] = bd_wrap(d->ph6[q] + d->inc6[q]);
                    sq += d->ph6[q] < 0.5f ? 1.0f : -1.0f;
                }
                bd_svf(sq * (1.0f / 6.0f), &d->ic1, &d->ic2, d->a1, d->a2, d->a3, &bp, &lp);
                bd_svf(bp, &d->ic3, &d->ic4, d->a1, d->a2, d->a3, &bp, &lp);   /* twice: the squares' bodies out */
                d->hp += (bp - d->hp) * d->hpc;
                y = (bp - d->hp) * env * lvl * 0.3f;
                wd[j] += y;
                a = (y < 0.0f ? -y : y) * WET_CLIP;
                pk = a > pk ? a : pk;
                env *= k;
            }
            break;
        case DR_CLAP:
            for (j = 0; j < m; j++) {    /* three 10 ms bursts 11 ms apart, then the tail */
                float bp, lp, y, a, g;
                if (d->t < 1455u) {
                    g = d->tt < 441.0f ? 1.0f - d->tt * (1.0f / 441.0f) : 0.0f;
                    d->tt += 1.0f;
                    if (d->tt >= 485.0f)
                        d->tt -= 485.0f;
                    env = 1.0f;
                } else {
                    g = env;
                    env *= k;
                }
                bd_svf(bd_frand(&rng), &d->ic1, &d->ic2, d->a1, d->a2, d->a3, &bp, &lp);
                y = bp * g * lvl * 0.6f;
                wd[j] += y;
                a = (y < 0.0f ? -y : y) * WET_CLIP;
                pk = a > pk ? a : pk;
                d->t++;
            }
            break;
        case DR_RIM:
            for (j = 0; j < m; j++) {
                float y, a;
                d->ph = bd_wrap(d->ph + d->hz * (1.0f / FS));
                y = fm_sin_turns(d->ph) * env;
                if (d->t < 44u)
                    y += bd_frand(&rng) * (1.0f - (float)d->t * (1.0f / 44.0f)) * 0.4f;
                y *= lvl;
                wl[j] += y;
                wr[j] += y;
                a = (y < 0.0f ? -y : y) * WET_CLIP;
                pk = a > pk ? a : pk;
                env *= k;
                d->t++;
            }
            break;
        case DR_CRASH:
            for (j = 0; j < m; j++) {
                float n = bd_frand(&rng), y, a;
                d->hp += (n - d->hp) * d->hpc;
                y = (n - d->hp) * env * lvl * 0.8f;
                wl[j] += y;
                wr[j] += y;
                a = (y < 0.0f ? -y : y) * WET_CLIP;
                pk = a > pk ? a : pk;
                env *= k;
            }
            break;
        default:                         /* SHAKER: a 20 ms swell, then away */
            for (j = 0; j < m; j++) {
                float n = bd_frand(&rng), y, a, g;
                d->hp += (n - d->hp) * d->hpc;
                if (d->t < 882u) {
                    g = (float)d->t * (1.0f / 882.0f);
                    env = 1.0f;
                } else {
                    g = env;
                    env *= k;
                }
                y = (n - d->hp) * g * lvl;
                wl[j] += y;
                wr[j] += y;
                a = (y < 0.0f ? -y : y) * WET_CLIP;
                pk = a > pk ? a : pk;
                d->t++;
            }
            break;
        }
        d->env = env;
        d->ic1 = fm_flush(d->ic1);
        d->ic2 = fm_flush(d->ic2);
        d->ic3 = fm_flush(d->ic3);
        d->ic4 = fm_flush(d->ic4);
        d->hp = fm_flush(d->hp);
        d->lp = fm_flush(d->lp);
        if (env < 1e-4f && !(i == DR_CLAP && d->t < 1455u) && !(i == DR_SHAKER && d->t < 882u))
            d->on = 0;
    }
    b->rng = rng;
    b->pk[PART_COUNT] = pk;
}

/* ------------------------------------------------------------------ RISER --- */
static void bd_riser_chunk(bd_t *b, uint32_t m, float *wl, float *wr, float *dl, float *dr)
{
    bd_riser_t *r = &b->riser;
    uint32_t j, rng = b->rng;
    if (r->phase == 1u) {                /* noise through a band-pass climbing 300 Hz to 6 kHz, louder as it goes */
        float frac = (float)r->t / (float)r->len, a1, a2, a3, lvl;
        if (frac > 1.0f)
            frac = 1.0f;
        bd_svf_coef(300.0f * fm_exp2f(4.3219f * frac), 0.5f, &a1, &a2, &a3);
        lvl = (0.1f + 0.9f * frac * frac) * RISER_G;
        for (j = 0; j < m; j++) {
            float bp, lp;
            bd_svf(bd_frand(&rng), &r->ic1, &r->ic2, a1, a2, a3, &bp, &lp);
            bp *= lvl;
            wl[j] += bp;
            wr[j] += bp;
        }
        r->t += m;
        if (r->t >= r->len) {            /* then the sub drop: a sine falling 120 to 35 Hz */
            r->phase = 2;
            r->t = 0;
            r->hz = 120.0f;
            r->ph = 0.0f;
            r->env = 1.0f;
        }
        r->ic1 = fm_flush(r->ic1);
        r->ic2 = fm_flush(r->ic2);
    } else {
        for (j = 0; j < m; j++) {
            float y;
            r->hz += (35.0f - r->hz) * bdc.sub_kp;
            r->ph = bd_wrap(r->ph + r->hz * (1.0f / FS));
            y = fm_sin_turns(r->ph) * r->env * SUB_G;
            dl[j] += y;
            dr[j] += y;
            if (r->t > (uint32_t)(0.15f * FS))
                r->env *= bdc.sub_k;
            r->t++;
        }
        if (r->env < 0.002f)
            r->on = 0;
    }
    b->rng = rng;
}

/* ------------------------------------------------------------------ the buses --- */
static void bd_ring_chunk(bd_t *b, const bd_blk_t *k, uint32_t m, float *wl, float *wr)
{
    float c = b->ring_c, s = b->ring_s, cw = k->ring_cw, sw = k->ring_sw, a = k->ring_a, dry = 1.0f - a, r;
    uint32_t j;
    for (j = 0; j < m; j++) {
        float c2 = c * cw - s * sw;
        s = s * cw + c * sw;
        c = c2;
        r = dry + a * s;
        wl[j] *= r;
        wr[j] *= r;
    }
    r = fm_rsqrtf(c * c + s * s);        /* the pair kept on the circle */
    b->ring_c = c * r;
    b->ring_s = s * r;
}

static void bd_decim2(bd_dec_t *d, float *l, float *r, uint32_t m, float inc, float q)
{
    float ph = d->ph, hl = d->hl, hr = d->hr, iq = 1.0f / q;
    uint32_t j;
    for (j = 0; j < m; j++) {
        ph += inc;
        if (ph >= 1.0f) {
            ph -= 1.0f;
            hl = fm_floorf(l[j] * q + 0.5f) * iq;
            hr = fm_floorf(r[j] * q + 0.5f) * iq;
        }
        l[j] = hl;
        r[j] = hr;
    }
    d->ph = ph;
    d->hl = hl;
    d->hr = hr;
}
static void bd_decim1(bd_dec_t *d, float *x, uint32_t m, float inc, float q)
{
    float ph = d->ph, h = d->hl, iq = 1.0f / q;
    uint32_t j;
    for (j = 0; j < m; j++) {
        ph += inc;
        if (ph >= 1.0f) {
            ph -= 1.0f;
            h = fm_floorf(x[j] * q + 0.5f) * iq;
        }
        x[j] = h;
    }
    d->ph = ph;
    d->hl = h;
}

/* the stutter ring: live, it records DRY + BAND-WET in mono (the wet at the level the clip leaves it); repeating,
 * it replaces both with the slice, crossfading over 2 ms at every boundary from the slice's true continuation (the
 * live samples recorded just after it) so no edge clicks */
static void bd_stutter_chunk(bd_t *b, uint32_t m, float *dl, float *dr, float *wl, float *wr)
{
    bd_stut_t *s = &b->stut;
    int16_t *ring = b->ring;
    uint32_t j;
    for (j = 0; j < m; j++) {
        float live = 0.5f * (dl[j] + dr[j]) + 0.5f * WET_CLIP * (wl[j] + wr[j]);
        if (!s->on) {
            ring[s->w] = bd_f2i16(live);
            s->w = bd_rwrap(s->w + 1u);
            continue;
        }
        if (s->cont < RING_FADE) {       /* the slice's continuation: what the crossfades fade out of */
            ring[bd_rwrap(s->end + s->cont)] = bd_f2i16(live);
            s->cont++;
        }
        if (s->ending) {                 /* the last repeat done: back to live over 2 ms */
            float w = (float)s->pos * (1.0f / (float)RING_FADE),
                  old = (float)ring[bd_rwrap(s->from + s->pos)] * (1.0f / RING_SCALE) * (1.0f - w);
            dl[j] = old + dl[j] * w;
            dr[j] = old + dr[j] * w;
            wl[j] *= w;
            wr[j] *= w;
            ring[s->w] = bd_f2i16(live);
            s->w = bd_rwrap(s->w + 1u);
            if (++s->pos >= RING_FADE) {
                s->on = 0;
                s->ending = 0;
            }
            continue;
        }
        {
            float rep = (float)ring[bd_rwrap(s->start + s->pos)] * (1.0f / RING_SCALE);
            if (s->pos < RING_FADE) {
                float w = (float)s->pos * (1.0f / (float)RING_FADE);
                rep = (float)ring[bd_rwrap(s->from + s->pos)] * (1.0f / RING_SCALE) * (1.0f - w) + rep * w;
            }
            dl[j] = dr[j] = rep;
            wl[j] = wr[j] = 0.0f;
            if (++s->pos >= s->len) {
                s->pos = 0;
                s->from = s->end;
                if (--s->left == 0u) {
                    s->ending = 1;
                    s->w = bd_rwrap(s->end + RING_FADE);
                }
            }
        }
    }
}

static void bd_dj_chunk(bd_t *b, float f, uint32_t m, float *l, float *r)
{
    float a1, a2, a3, kq = 1.0f / 0.9f, fc;
    uint32_t j, hp;
    if (fm_fabsf(f) < 0.001f) {          /* flat: the filter stands by, its state following the signal */
        if (m) {
            b->dj_ic1[0] = b->dj_ic1[1] = 0.0f;
            b->dj_ic2[0] = l[m - 1u];
            b->dj_ic2[1] = r[m - 1u];
        }
        return;
    }
    hp = f > 0.0f;
    fc = hp ? 20.0f * fm_exp2f(7.644f * f) : 20000.0f * fm_exp2f(7.059f * f);   /* to 4 kHz up, 150 Hz down */
    bd_svf_coef(fc, kq, &a1, &a2, &a3);
    for (j = 0; j < m; j++) {
        float bp, lp;
        bd_svf(l[j], &b->dj_ic1[0], &b->dj_ic2[0], a1, a2, a3, &bp, &lp);
        l[j] = hp ? l[j] - kq * bp - lp : lp;
        bd_svf(r[j], &b->dj_ic1[1], &b->dj_ic2[1], a1, a2, a3, &bp, &lp);
        r[j] = hp ? r[j] - kq * bp - lp : lp;
    }
    b->dj_ic1[0] = fm_flush(b->dj_ic1[0]);
    b->dj_ic1[1] = fm_flush(b->dj_ic1[1]);
    b->dj_ic2[0] = fm_flush(b->dj_ic2[0]);
    b->dj_ic2[1] = fm_flush(b->dj_ic2[1]);
}

static void bd_fx_params(bd_t *b)        /* this block's effect settings from the settled parameters */
{
    fx_params_t *p = &b->fxp;
    uint32_t div = bd_disc(BP_DLY_DIV), ens = bd_disc(BP_ENS);
    float bpm = bd_tgt[BP_TEMPO], space = bd_cur[BP_SPACE], t, mix = bd_cur[BP_ENS_MIX];
    float hi = bd_cur[BP_TRIP] > 0.0f ? bd_cur[BP_TRIP] : 0.0f;   /* high on the trip: more space and sparkle */
    static const float DIV_BEATS[5] = {0.5f, 0.25f, 0.5f, 0.75f, 1.0f};
    if (div > 4u)
        div = 4u;
    t = DIV_BEATS[div] * 60.0f / bpm;    /* a division the line cannot hold steps down to one it can */
    while (t > 0.418f && div > 1u) {
        div--;
        t = DIV_BEATS[div] * 60.0f / bpm;
    }
    if (bd_disc(BP_STAB_STYLE) == SS_HOOVER && ens == ENS_OFF) {   /* the hoover wants the ensemble */
        ens = ENS_ENSEMBLE;
        mix = mix > 0.4f ? mix : 0.4f;
    }
    p->ens_mode = ens;
    p->ens_mix = mix;
    p->dly_time_s = t < 0.05f ? 0.05f : t > 0.418f ? 0.418f : t;
    p->dly_ratio = 1.0f;
    p->dly_fdbk = bd_cur[BP_DLY_FDBK];
    p->dly_send = bd_disc(BP_DLY_DIV) ? bd_clamp01(bd_cur[BP_DLY_SEND] + 0.4f * space + 0.08f * hi) : 0.0f;
    p->dly_diff = 0.35f;
    p->rev_decay = bd_cur[BP_REV_SIZE];
    p->rev_damp = bd_cur[BP_REV_DAMP];
    p->rev_send = bd_clamp01(bd_cur[BP_REV_SEND] + 0.4f * space + 0.08f * hi);
    p->shimmer = bd_clamp01(bd_cur[BP_SHIMMER] + 0.12f * hi);
    p->freeze = bd_disc(BP_FREEZE);
    p->bypass = 0;
}

#if FM1B_CPU2
static bd_job_t *bd_job_line;            /* its own line: core 2 never touches bd_t's hot lines. Not plat_cpu2_run's
                                          * arg, which is a uint32_t: a pointer does not survive it on a 64-bit host */
static void bd_job(uint32_t arg)         /* the second core: the hall on the last block's wet bus */
{
    bd_job_t *j = bd_job_line;
    (void)arg;
    fx_hall(j->in_l, j->in_r, j->out_l, j->out_r, j->n, &j->p);
}
#endif

static void bd_block(bd_t *b, int32_t *lr, uint32_t n, float gain)
{
    bd_blk_t *k = &b->k;
    uint32_t i, j, t, on_two = 0, nt = (n + CTL - 1u) / CTL;
    float *dl = b->dry_l, *dr = b->dry_r, *wl = b->wet_l, *wr = b->wet_r, *lm = b->lead_m, pk;
    b->job->p = b->fxp;
    b->job->n = b->hall_n;
#if FM1B_CPU2
    if (plat_cpu2_on()) {
        plat_cpu2_sync();
        bd_job_line = b->job;
        on_two = plat_cpu2_run(bd_job, 0) == 1;
    }
#endif
    for (i = 0; i < n; i++)
        dl[i] = dr[i] = wl[i] = wr[i] = lm[i] = 0.0f;
    for (i = 0; i < 4u; i++)
        b->pk[i] = 0.0f;
    for (i = 0, t = 0; i < n; i += CTL, t++) {
        uint32_t m = n - i < CTL ? n - i : CTL, any;
        float *cdl = dl + i, *cdr = dr + i, *cwl = wl + i, *cwr = wr + i, *clm = lm + i;
        bd_tick_params(b);
        b->dj_v[t] = k->filter;
        bd_duck_curve(&b->duck, b->dk, m);
        /* STAB: the voices into the scratch, the shared filter, the wet bus */
        any = 0;
        for (j = 0; j < m; j++)
            b->sb[j] = 0.0f;
        for (j = 0; j < NSTAB; j++)
            if (b->stab[j].active) {
                bd_stab_chunk(b, &b->stab[j], k, m, b->sb);
                any = 1;
            }
        b->st_fenv = fm_flush(b->st_fenv * bdc.sfenv);
        b->st_pwm = bd_wrap(b->st_pwm + 0.5f * (float)CTL / FS);
        if (any) {
            float a1, a2, a3;
            bd_svf_coef(k->s_fc * fm_exp2f(1.5f * b->st_fenv), 1.25f, &a1, &a2, &a3);
            pk = b->pk[PART_STAB];
            for (j = 0; j < m; j++) {
                float bp, y, a;
                bd_svf(b->sb[j], &b->st_ic1, &b->st_ic2, a1, a2, a3, &bp, &y);
                cwl[j] += y;
                cwr[j] += y;
                a = (y < 0.0f ? -y : y) * WET_CLIP;
                pk = a > pk ? a : pk;
            }
            b->pk[PART_STAB] = pk;
            b->st_ic1 = fm_flush(b->st_ic1);
            b->st_ic2 = fm_flush(b->st_ic2);
        }
        /* the drums: the crush group through its decimator, then everything on the wet bus ducked */
        for (j = 0; j < m; j++)
            b->wd[j] = 0.0f;
        bd_drums_chunk(b, m, cdl, cdr, b->wd, cwl, cwr);
        if (k->dcrush_on)
            bd_decim1(&b->dec_drum, b->wd, m, k->dcrush_inc, k->dcrush_q);
        for (j = 0; j < m; j++) {
            cwl[j] = (cwl[j] + b->wd[j]) * b->dk[j];
            cwr[j] = (cwr[j] + b->wd[j]) * b->dk[j];
        }
        if (b->riser.on)
            bd_riser_chunk(b, m, cwl, cwr, cdl, cdr);
        if (b->bass.active)
            bd_bass_chunk(&b->bass, k, m, cdl, cdr, &b->pk[PART_BASS]);
        for (j = 0; j < NLEAD; j++)
            if (b->lead[j].active)
                bd_lead_chunk(&b->lead[j], k, m, clm);
        /* BAND-WET -> ring mod -> decimator; the stutter over DRY + BAND-WET; LEAD (its own decimator) joins */
        if (k->ring_a > 0.001f)
            bd_ring_chunk(b, k, m, cwl, cwr);
        if (k->crush_on)
            bd_decim2(&b->dec_wet, cwl, cwr, m, k->crush_inc, k->crush_q);
        bd_stutter_chunk(b, m, cdl, cdr, cwl, cwr);
        bd_trip_lp(b, k->tlp_a, m, cwl, cwr);
        if (k->lcrush_on)
            bd_decim1(&b->dec_lead, clm, m, k->lcrush_inc, k->lcrush_q);
        pk = b->pk[PART_LEAD];
        for (j = 0; j < m; j++) {        /* the peaks in output terms: the wet bus is scaled by WET_CLIP */
            float a = (clm[j] < 0.0f ? -clm[j] : clm[j]) * WET_CLIP;
            pk = a > pk ? a : pk;
            cwl[j] += clm[j];
            cwr[j] += clm[j];
        }
        b->pk[PART_LEAD] = pk;
        for (j = 0; j < DR_COUNT; j++)   /* the drums' MIDI offs, 50 ms after the hit */
            if (b->drum[j].midi_left && --b->drum[j].midi_left == 0u)
                bd_midi_send(9, BD_DRUM_GM[j], 0, 0);
    }
    bd_fx_params(b);
    for (i = 0; i < n; i++) {            /* the wet bus kept within +-1 before the effects */
        wl[i] = bd_softclip(wl[i] * WET_CLIP);
        wr[i] = bd_softclip(wr[i] * WET_CLIP);
    }
    fx_pre(wl, wr, n, &b->fxp);
    if (!on_two) {
        fx_hall(b->hall_in_l, b->hall_in_r, b->hall_out_l, b->hall_out_r, b->hall_n, &b->job->p);
    }
#if FM1B_CPU2
    else if (plat_cpu2_wait() != 0) {    /* given up: this block's wet is lost, as DEEP FIELD accepts */
        for (i = 0; i < b->hall_n; i++)
            b->hall_out_l[i] = b->hall_out_r[i] = 0.0f;
    } else {
        plat_cpu2_sync();
    }
#endif
    for (i = 0; i < n; i++) {            /* this bus to the hall next block; the last block's wet joins now, then DRY */
        float hl = i < b->hall_n ? b->hall_out_l[i] : 0.0f, hr = i < b->hall_n ? b->hall_out_r[i] : 0.0f;
        b->hall_in_l[i] = wl[i];
        b->hall_in_r[i] = wr[i];
        wl[i] += hl + dl[i];
        wr[i] += hr + dr[i];
    }
    b->hall_n = n;
    for (i = 0, t = 0; i < n; i += CTL, t++)
        bd_dj_chunk(b, b->dj_v[t < nt ? t : nt - 1u], n - i < CTL ? n - i : CTL, wl + i, wr + i);
    fx_post(wl, wr, n);
    pk = 0.0f;
    for (i = 0; i < n; i++) {
        float a = wl[i] < 0.0f ? -wl[i] : wl[i], c = wr[i] < 0.0f ? -wr[i] : wr[i];
        pk = a > pk ? a : pk;
        pk = c > pk ? c : pk;
        b->gain_now += (gain - b->gain_now) * 0.004f;   /* ~5 ms: turning MASTER does not click */
        if (fm_fabsf(gain - b->gain_now) < 1.0f)       /* at MASTER 0 it would creep down into denormals */
            b->gain_now = gain;
        lr[2u * i] = (int32_t)(wl[i] * b->gain_now);
        lr[2u * i + 1u] = (int32_t)(wr[i] * b->gain_now);
    }
    b->pk[PART_COUNT + 1u] = pk;
}

/* ------------------------------------------------------------------ the contract --- */
static void bd_defaults(void)            /* musical on its own: HOUSE kit, SAW lead, HOUSE bass, STAB */
{
    uint32_t p;
    for (p = 0; p < BP_COUNT; p++)
        bd_tgt[p] = 0.0f;
    bd_tgt[BP_LEAD_PATCH] = LP_SAW;
    bd_tgt[BP_LEAD_CUTOFF] = 0.55f;
    bd_tgt[BP_LEAD_RESO] = 0.3f;
    bd_tgt[BP_LEAD_DECAY] = 0.4f;
    bd_tgt[BP_LEAD_RELEASE] = 0.35f;
    bd_tgt[BP_LEAD_LEVEL] = 0.8f;
    bd_tgt[BP_BASS_STYLE] = BS_HOUSE;
    bd_tgt[BP_BASS_CUTOFF] = BD_BASS_PRESET[BS_HOUSE][0];
    bd_tgt[BP_BASS_RESO] = BD_BASS_PRESET[BS_HOUSE][1];
    bd_tgt[BP_BASS_ENVMOD] = BD_BASS_PRESET[BS_HOUSE][2];
    bd_tgt[BP_BASS_DECAY] = BD_BASS_PRESET[BS_HOUSE][3];
    bd_tgt[BP_BASS_DRIVE] = BD_BASS_PRESET[BS_HOUSE][4];
    bd_tgt[BP_BASS_LEVEL] = 0.8f;
    bd_tgt[BP_STAB_STYLE] = SS_STAB;
    bd_tgt[BP_STAB_CUTOFF] = 0.6f;
    bd_tgt[BP_STAB_DECAY] = 0.35f;
    bd_tgt[BP_STAB_LEVEL] = 0.8f;
    bd_tgt[BP_KIT] = KIT_HOUSE;
    bd_tgt[BP_DRUM_LEVEL] = 0.7f;
    bd_tgt[BP_DUCK] = 0.35f;
    bd_tgt[BP_ENS] = ENS_OFF;
    bd_tgt[BP_ENS_MIX] = 0.5f;
    bd_tgt[BP_DLY_SEND] = 0.15f;
    bd_tgt[BP_DLY_DIV] = 2.0f;
    bd_tgt[BP_DLY_FDBK] = 0.4f;
    bd_tgt[BP_REV_SEND] = 0.25f;
    bd_tgt[BP_REV_SIZE] = 0.4f;
    bd_tgt[BP_REV_DAMP] = 0.4f;
    bd_tgt[BP_RING_HZ] = 196.0f;
    bd_tgt[BP_TEMPO] = 120.0f;
    for (p = 0; p < BP_COUNT; p++)
        bd_cur[p] = bd_tgt[p];
}

static uint8_t *bd_carve(uint8_t **a, uint32_t bytes)
{
    uint8_t *p = *a;
    *a = p + ((bytes + 31u) & ~31u);
    return p;
}

int band_init(void *arena, uint32_t bytes)
{
    uint8_t *a, *base, *end;
    bd_t *b;
    uint32_t i, need;
    bd = 0;
    bd_used = 0;
    bd_defaults();
    if (!arena)
        return -1;
    base = (uint8_t *)(((uintptr_t)arena + 31u) & ~(uintptr_t)31u);
    end = (uint8_t *)arena + bytes;
    need = ((fx_bytes() + 31u) & ~31u) + RING * 2u + 9u * MAXBLOCK * 4u + ((sizeof(bd_job_t) + 31u) & ~31u)
           + ((sizeof(bd_t) + 31u) & ~31u) + 32u;
    if (end < base || (uint32_t)(end - base) < need)
        return -1;
    a = base;
    fx_init(bd_carve(&a, fx_bytes()));
    b = (bd_t *)bd_carve(&a, sizeof(bd_t));
    {
        uint8_t *z = (uint8_t *)b;
        for (i = 0; i < sizeof(bd_t); i++)
            z[i] = 0;
    }
    b->ring = (int16_t *)bd_carve(&a, RING * 2u);
    for (i = 0; i < RING; i++)
        b->ring[i] = 0;
    b->dry_l = (float *)bd_carve(&a, MAXBLOCK * 4u);
    b->dry_r = (float *)bd_carve(&a, MAXBLOCK * 4u);
    b->wet_l = (float *)bd_carve(&a, MAXBLOCK * 4u);
    b->wet_r = (float *)bd_carve(&a, MAXBLOCK * 4u);
    b->lead_m = (float *)bd_carve(&a, MAXBLOCK * 4u);
    b->hall_in_l = (float *)bd_carve(&a, MAXBLOCK * 4u);
    b->hall_in_r = (float *)bd_carve(&a, MAXBLOCK * 4u);
    b->hall_out_l = (float *)bd_carve(&a, MAXBLOCK * 4u);
    b->hall_out_r = (float *)bd_carve(&a, MAXBLOCK * 4u);
    for (i = 0; i < MAXBLOCK; i++)
        b->dry_l[i] = b->dry_r[i] = b->wet_l[i] = b->wet_r[i] = b->lead_m[i] = b->hall_in_l[i] = b->hall_in_r[i]
            = b->hall_out_l[i] = b->hall_out_r[i] = 0.0f;
    b->job = (bd_job_t *)bd_carve(&a, sizeof(bd_job_t));
    {
        uint8_t *z = (uint8_t *)b->job;
        for (i = 0; i < sizeof(bd_job_t); i++)
            z[i] = 0;
    }
    b->job->in_l = b->hall_in_l;
    b->job->in_r = b->hall_in_r;
    b->job->out_l = b->hall_out_l;
    b->job->out_r = b->hall_out_r;
    b->rng = 0x474E4F4Du;
    b->ring_c = 1.0f;
    b->duck.g = 1.0f;
    for (i = 0; i < 6u; i++) {           /* the hats' squares out of step from the first hit: aligned they are one spike */
        b->drum[DR_HAT].ph6[i] = bd_rnd01(&b->rng);
        b->drum[DR_OHAT].ph6[i] = bd_rnd01(&b->rng);
    }
    bdc.aa1 = bd_coef(0.001f, 1.61f);
    bdc.aa2 = bd_coef(0.002f, 1.61f);
    bdc.aa20 = bd_coef(0.020f, 1.61f);
    bdc.ad120 = bd_coef(0.120f, 4.0f);
    bdc.ad300 = bd_coef(0.300f, 4.0f);
    bdc.ad400 = bd_coef(0.400f, 4.0f);
    bdc.ad1500 = bd_coef(1.5f, 4.0f);
    bdc.ar30 = bd_coef(0.030f, 4.0f);
    bdc.ar60 = bd_coef(0.060f, 4.0f);
    bdc.ar150 = bd_coef(0.150f, 4.0f);
    bdc.ar2000 = bd_coef(2.0f, 4.0f);
    bdc.click = fm_expf(-(float)CTL * 4.0f / (0.002f * FS));
    bdc.penv = fm_expf(-(float)CTL * 4.0f / (0.220f * FS));
    bdc.sfenv = fm_expf(-(float)CTL * 4.0f / (0.050f * FS));
    bdc.sub_kp = 1.0f - fm_expf(-1.0f / (0.1f * FS));
    bdc.sub_k = fm_expf(-1.0f / (0.08f * FS));
    bd_senv[SS_OFF].aa = bd_senv[SS_STAB].aa = bd_senv[SS_HOOVER].aa = bdc.aa1;
    bd_senv[SS_PAD].aa = bdc.aa20;
    bd_senv[SS_OFF].ar = bd_senv[SS_STAB].ar = bdc.ar60;
    bd_senv[SS_PAD].ar = bdc.ar2000;
    bd_senv[SS_HOOVER].ar = bdc.ar150;
    bd_senv[SS_PAD].ad = bdc.ad400;
    bd_senv[SS_PAD].own_ad = 1;
    bd_senv[SS_PAD].sus = 0.4f;
    bd_senv[SS_HOOVER].sus = 0.5f;
    bd_used = (uint32_t)(a - base);
    bd = b;
    return 0;
}
uint32_t band_arena_used(void) { return bd_used; }

void band_note_on(uint32_t part, uint32_t note, uint32_t vel, uint32_t flags)
{
    if (!bd)
        return;
    note = note > 127u ? 127u : note;
    vel = vel == 0u ? 1u : vel > 127u ? 127u : vel;
    switch (part) {
    case PART_LEAD: bd_lead_on(bd, note, vel, flags); break;
    case PART_BASS: bd_bass_on(bd, note, vel, flags); break;
    case PART_STAB: bd_stab_on(bd, note, vel, flags); break;
    default: break;
    }
}

void band_note_off(uint32_t part, uint32_t note)
{
    uint32_t i;
    if (!bd)
        return;
    switch (part) {
    case PART_LEAD:
        for (i = 0; i < NLEAD; i++)
            if (bd->lead[i].active && bd->lead[i].note == note)
                bd_lead_release(&bd->lead[i]);
        break;
    case PART_BASS:
        if (bd->bass.active && bd->bass.note == note)
            bd_bass_release(&bd->bass);
        break;
    case PART_STAB:
        for (i = 0; i < NSTAB; i++)
            if (bd->stab[i].active && bd->stab[i].note == note)
                bd_stab_release(&bd->stab[i]);
        break;
    default:
        break;
    }
}

void band_drum(uint32_t drum, uint32_t vel)
{
    if (!bd || drum >= DR_COUNT)
        return;
    bd_drum_hit(bd, drum, vel == 0u ? 1u : vel > 127u ? 127u : vel);
}

void band_riser(uint32_t steps)
{
    bd_riser_t *r;
    if (!bd)
        return;
    r = &bd->riser;
    r->on = 1;
    r->phase = 1;
    r->t = 0;
    r->len = (uint32_t)((float)(steps ? steps : 1u) * 15.0f / bd_tgt[BP_TEMPO] * FS);
    r->ic1 = r->ic2 = 0.0f;
}

void band_stutter(uint32_t div, uint32_t repeats)
{
    bd_stut_t *s;
    uint32_t len;
    if (!bd)
        return;
    s = &bd->stut;
    if (repeats == 0u) {                 /* cancelled: back to live, faded */
        if (s->on && !s->ending) {
            s->from = bd_rwrap(s->start + s->pos);
            s->pos = 0;
            s->ending = 1;
            s->w = bd_rwrap(s->end + RING_FADE);
        }
        return;
    }
    if (div == 0u)
        div = 16u;
    len = (uint32_t)(FS * 240.0f / (bd_tgt[BP_TEMPO] * (float)div) + 0.5f);   /* a 1/div note */
    while (len > RING - RING_FADE - 1u) {   /* too slow for the ring: the next division up stays on the grid, twice
                                             * as many repeats so the roll still lasts as long */
        len /= 2u;
        repeats *= 2u;
    }
    len = len < 2u * RING_FADE ? 2u * RING_FADE : len;
    if (s->on && !s->ending) {           /* retriggered mid-repeat: the slice is cut again from what the ring holds,
                                          * the fade out of what was playing */
        s->from = bd_rwrap(s->start + s->pos);
    } else {                             /* from the write head: the next live samples are the continuation */
        s->end = s->w;
        s->from = s->end;
        s->cont = 0;
    }
    s->start = bd_rwrap(s->end + RING - len);
    s->len = len;
    s->left = repeats;
    s->pos = 0;
    s->on = 1;
    s->ending = 0;
}

void band_set(uint32_t p, float v)
{
    if (p >= BP_COUNT || v != v)
        return;
    switch (p) {
    case BP_LEAD_PATCH: v = fm_floorf(fm_clampf(v, 0.0f, (float)(LP_COUNT - 1)) + 0.5f); break;
    case BP_BASS_STYLE: {
        uint32_t s = (uint32_t)(fm_clampf(v, 0.0f, (float)(BS_COUNT - 1)) + 0.5f);
        bd_tgt[BP_BASS_CUTOFF] = BD_BASS_PRESET[s][0];   /* the style brings its preset; the knobs set after it stick */
        bd_tgt[BP_BASS_RESO] = BD_BASS_PRESET[s][1];
        bd_tgt[BP_BASS_ENVMOD] = BD_BASS_PRESET[s][2];
        bd_tgt[BP_BASS_DECAY] = BD_BASS_PRESET[s][3];
        bd_tgt[BP_BASS_DRIVE] = BD_BASS_PRESET[s][4];
        v = (float)s;
        break;
    }
    case BP_STAB_STYLE: v = fm_floorf(fm_clampf(v, 0.0f, (float)(SS_COUNT - 1)) + 0.5f); break;
    case BP_KIT: v = fm_floorf(fm_clampf(v, 0.0f, (float)(KIT_COUNT - 1)) + 0.5f); break;
    case BP_ENS: v = fm_floorf(fm_clampf(v, 0.0f, (float)(ENS_COUNT - 1)) + 0.5f); break;
    case BP_DLY_DIV: v = fm_floorf(fm_clampf(v, 0.0f, 4.0f) + 0.5f); break;
    case BP_FREEZE: v = v >= 0.5f ? 1.0f : 0.0f; break;
    case BP_LEAD_TONE:
    case BP_SPACE:
    case BP_TRIP:
    case BP_FILTER: v = fm_clampf(v, -1.0f, 1.0f); break;
    case BP_PITCH: v = fm_clampf(v, -24.0f, 24.0f); break;
    case BP_TEMPO: v = fm_clampf(v, 40.0f, 300.0f); break;
    case BP_RING_HZ: v = fm_clampf(v, 20.0f, 8000.0f); break;
    default: v = bd_clamp01(v); break;
    }
    bd_tgt[p] = v;
}
float band_get(uint32_t p) { return p < BP_COUNT ? bd_tgt[p] : 0.0f; }

void band_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    float gain = (float)master_q12 * (1.0f / 4096.0f) * FULL;
    uint32_t i;
    if (!bd) {
        for (i = 0; i < 2u * n; i++)
            lr[i] = 0;
        return;
    }
    while (n) {
        uint32_t m = n > MAXBLOCK ? MAXBLOCK : n;
        bd_block(bd, lr, m, gain);
        lr += 2u * m;
        n -= m;
    }
}

void band_all_off(void)
{
    uint32_t i;
    if (!bd)
        return;
    for (i = 0; i < NLEAD; i++)
        if (bd->lead[i].active)
            bd_lead_release(&bd->lead[i]);
    if (bd->bass.active)
        bd_bass_release(&bd->bass);
    for (i = 0; i < NSTAB; i++)
        if (bd->stab[i].active)
            bd_stab_release(&bd->stab[i]);
}

void band_panic(void)
{
    uint32_t i;
    if (!bd)
        return;
    band_all_off();
    for (i = 0; i < NLEAD; i++) {
        bd->lead[i].active = 0;
        bd->lead[i].aenv.stage = 0;
        bd->lead[i].aenv.level = bd->lead[i].amp = 0.0f;
    }
    bd->bass.active = 0;
    bd->bass.aenv.stage = 0;
    bd->bass.aenv.level = bd->bass.amp = 0.0f;
    for (i = 0; i < NSTAB; i++) {
        bd->stab[i].active = 0;
        bd->stab[i].aenv.stage = 0;
        bd->stab[i].aenv.level = bd->stab[i].amp = 0.0f;
    }
    for (i = 0; i < DR_COUNT; i++) {
        if (bd->drum[i].midi_left)
            bd_midi_send(9, BD_DRUM_GM[i], 0, 0);
        bd->drum[i].on = 0;
        bd->drum[i].midi_left = 0;
        bd->drum[i].env = 0.0f;
    }
    bd->riser.on = 0;
    bd->stut.on = bd->stut.ending = 0;
    bd->stut.cont = 0;
    for (i = 0; i < RING; i++)
        bd->ring[i] = 0;
    bd->duck.phase = 0;
    bd->duck.g = 1.0f;
    fx_clear();
}

float band_peak(uint32_t part)
{
    float p = bd && part <= PART_COUNT + 1u ? bd->pk[part] : 0.0f;
    return p > 1.0f ? 1.0f : p;
}

uint32_t band_voices(void)
{
    uint32_t i, c = 0;
    if (!bd)
        return 0;
    for (i = 0; i < NLEAD; i++)
        c += bd->lead[i].active != 0u;
    for (i = 0; i < NSTAB; i++)
        c += bd->stab[i].active != 0u;
    return c + (bd->bass.active != 0u);
}

void band_midi_out(void (*fn)(uint32_t ch, uint32_t note, uint32_t vel, int on)) { bd_midi = fn; }
