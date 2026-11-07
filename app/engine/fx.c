/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann */
/* GNOMNOM's master effects: DEEP FIELD's engine/fx.c adapted (gn.h for the rate and ENS_*, the delay's right leg
 * by dly_ratio instead of a constant, the hall's state on its own cache lines for the second core, fx_clear for
 * the panic). The stages: ensemble, tape delay, FDN hall with shimmer and freeze, lookahead limiter. */
#include <stdint.h>
#include "gn.h"
#include "fastmath.h"
#include "fx.h"

#define FS GN_FS
#define ENS_LEN FX_ENS_LEN
#define DLY_LEN FX_DLY_LEN
#define SHIM_LEN FX_SHIM_LEN
#define SHIM_W 2048.0f               /* the shimmer's grain: 46 ms, two heads half a grain apart */

/* The hall: a feedback delay network of eight lines (45 to 131 ms) through a Hadamard mix, stereo in and out, with
 * a 30 ms pre-delay (one block of it from running a block behind the bus), a diffusing allpass a side, a high-pass
 * on the way in (the sub stays dry), damping in each line and a slow wander on four of them. REV SIZE is the decay
 * time (0.6 to 8 s); freeze holds the network; the shimmer feeds its own octave back in. */
#define RV_PRE 1080u                     /* 24.5 ms here, 30 ms heard: the hall runs a block (256) behind the bus */
#define RV_MOD 24u                       /* room past each line's length for the wander */
static const uint32_t RV_AP[4] = {199u, 431u, 173u, 388u};
static const uint32_t RV_L[8] = {1979u, 2371u, 2811u, 3323u, 3863u, 4493u, 5107u, 5801u};
#define RV_TOTAL (2u * RV_PRE + 199u + 431u + 173u + 388u \
                  + 1979u + 2371u + 2811u + 3323u + 3863u + 4493u + 5107u + 5801u + 8u * RV_MOD)

typedef struct { int16_t *buf; uint32_t len, pos; } line_t;
typedef char fx_rv_check[RV_TOTAL == FX_RV_TOTAL ? 1 : -1];   /* fx.h's FX_BYTES counts the same lines */

static float *ens_l, *ens_r;
static uint32_t ens_pos;
static float ens_ph1, ens_ph2;
static float ens_dl = -1.0f, ens_dr, ens_g;   /* the read distances and the wet's gate, slewed: a mode change or the
                                               * switch off would jump the read point (a click) */
static uint32_t ens_shape = ENS_CHORUS1;      /* the last mode on: its shape stays while the ensemble fades out */

/* The delay: two legs on one 16-bit buffer (left, then right), the input into the left, each leg's repeat crossing
 * into the other with the feedback, the right leg dly_ratio times the left (1.382 in DEEP FIELD so the sides never
 * coincide; 1.0 here so both land on the grid); two allpasses a leg smear the repeats by DIFFUSE; wow and flutter, a
 * dark low-pass and a soft clip in each leg. */
#define DLY_HALF (DLY_LEN / 2u)          /* a leg: 0.418 s */
#define DLY_WIDTH 0.7f                   /* the two legs' spread: 0 mono, 1 hard left and right */
#define DLY_AP0 309u                     /* 7 and 11 ms on the left, 5 and 13 on the right */
#define DLY_AP1 485u
#define DLY_AP2 221u
#define DLY_AP3 573u
#define DLY_AP_SUM 794u                  /* a leg's allpasses delay it this much: its read is that much shorter */
static const uint32_t DLY_AP[4] = {DLY_AP0, DLY_AP1, DLY_AP2, DLY_AP3};
typedef char fx_ap_check[DLY_AP0 + DLY_AP1 + DLY_AP2 + DLY_AP3 == FX_DLY_AP
                         && DLY_AP0 + DLY_AP1 == DLY_AP_SUM && DLY_AP2 + DLY_AP3 == DLY_AP_SUM ? 1 : -1];
static int16_t *dly;
static line_t dly_ap[4];
static uint32_t dly_pos;
static float dly_read, dly_lp[2], dly_dc_x[2], dly_dc_y[2], wow_ph, flut_ph;

typedef struct {                         /* everything the hall reads and writes while it runs, so the second core
                                          * shares no cache line with what this core writes meanwhile */
    line_t pre[2], ap[4], fd[8];
    int16_t *shim;                       /* the hall's output, read back an octave up (two crossfading heads) */
    uint32_t shim_pos, tail, live;
    float hp_l, hp_r, lp[8], mod_ph, shim_ph, shim_lp, frz_g, pk;   /* frozen, the cloud comes up to the dry's level */
} fx_hall_t;
static union {                           /* padded to whole lines: the static after it must not share the last one */
    fx_hall_t h;
    uint8_t lines[(sizeof(fx_hall_t) + 31u) & ~31u];
} hl_u __attribute__((aligned(32)));
#define hl hl_u.h

static float lim_gain = 1.0f, lim_min = 1.0f;
#define LIM_LA 64u                       /* the limiter's lookahead, 1.5 ms: a power of two */
static float lim_dl[LIM_LA], lim_dr[LIM_LA], lim_tgt = 1.0f;
static uint32_t lim_pos;
float fx_lim_min(void)
{
    float m = lim_min;
    lim_min = 1.0f;
    return m;
}
static uint32_t ens_tail, dly_tail;      /* blocks a stage still runs once nothing passes through it: with its
                                          * input and output silent that long, it holds only zeros and rests */
static uint32_t dly_live;                /* the delay's lines had something in them last block */
static float pk_ens, pk_dly;             /* each stage's output peak, decaying: a diagnostic (fx_peaks) */

static float block_peak(const float *l, const float *r, uint32_t n)
{
    float pk = 0.0f;
    uint32_t i;
    for (i = 0; i < n; i++) {
        float a = l[i] < 0.0f ? -l[i] : l[i], b = r[i] < 0.0f ? -r[i] : r[i];
        pk = a > pk ? a : pk;
        pk = b > pk ? b : pk;
    }
    return pk;
}
uint32_t fx_running(void) { return (ens_tail ? 1u : 0u) | (dly_tail ? 2u : 0u) | (hl.tail ? 4u : 0u); }
void fx_peaks(float *ens, float *dly_pk, float *rev)
{
    *ens = pk_ens;
    *dly_pk = pk_dly;
    *rev = hl.pk;
}

uint32_t fx_bytes(void) { return FX_BYTES; }

static inline float tri(float ph) { return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; }

/* a 16-bit delay line: write, read at an integer or fractional distance back */
static inline void line_put(line_t *d, float v)
{
    float c = v * 32767.0f;
    d->buf[d->pos] = (int16_t)(c > 32767.0f ? 32767.0f : c < -32767.0f ? -32767.0f : c);
    d->pos = d->pos + 1u == d->len ? 0u : d->pos + 1u;
}
static inline float line_at(const line_t *d, uint32_t back)   /* back >= 1, before this step's write */
{
    uint32_t i = d->pos + d->len - back;
    if (i >= d->len)
        i -= d->len;
    return (float)d->buf[i] * (1.0f / 32767.0f);
}
static inline float line_at_f(const line_t *d, float back)
{
    uint32_t b = (uint32_t)back;
    float f = back - (float)b, a = line_at(d, b), c = line_at(d, b + 1u);
    return a + (c - a) * f;
}
/* a Schroeder allpass on a line of length d->len: w[n] = x + g w[n-L], y = w[n-L] - g w[n] */
static inline float allpass(line_t *d, float x, float g)
{
    float v = line_at(d, d->len - 1u), w = x + g * v;
    line_put(d, w);
    return v - g * w;
}

static void dly_clear(void)
{
    uint32_t i, k;
    for (i = 0; i < 4u; i++)
        for (k = 0; k < DLY_AP[i]; k++)
            dly_ap[i].buf[k] = 0;
    for (i = 0; i < DLY_LEN; i++)
        dly[i] = 0;
    for (i = 0; i < ENS_LEN; i++)
        ens_l[i] = ens_r[i] = 0.0f;
    dly_lp[0] = dly_lp[1] = dly_dc_x[0] = dly_dc_x[1] = dly_dc_y[0] = dly_dc_y[1] = 0.0f;
    dly_tail = 0;
    dly_live = 0;
    ens_tail = 0;
}
void fx_clear(void) { dly_clear(); }

void fx_init(void *arena)
{
    uint8_t *a = (uint8_t *)arena;
    line_t *lines[14];
    uint32_t i, k;
    ens_l = (float *)a;
    a += ENS_LEN * sizeof(float);
    ens_r = (float *)a;
    a += ENS_LEN * sizeof(float);
    dly = (int16_t *)a;
    a += DLY_LEN * 2u;
    for (i = 0; i < 4u; i++) {
        dly_ap[i].buf = (int16_t *)a;
        dly_ap[i].len = DLY_AP[i];
        dly_ap[i].pos = 0;
        a += DLY_AP[i] * 2u;
    }
    a = (uint8_t *)(((uintptr_t)a + 31u) & ~(uintptr_t)31u);   /* the hall's lines from here: the second core's */
    hl.shim = (int16_t *)a;
    a += SHIM_LEN * 2u;
    for (i = 0; i < SHIM_LEN; i++)
        hl.shim[i] = 0;
    hl.shim_pos = 0;
    hl.shim_ph = hl.shim_lp = 0.0f;
    hl.frz_g = 1.0f;
    lines[0] = &hl.pre[0];
    lines[1] = &hl.pre[1];
    for (i = 0; i < 4u; i++)
        lines[2u + i] = &hl.ap[i];
    for (i = 0; i < 8u; i++)
        lines[6u + i] = &hl.fd[i];
    for (i = 0; i < 14u; i++) {
        line_t *d = lines[i];
        d->buf = (int16_t *)a;
        d->len = i < 2u ? RV_PRE : i < 6u ? RV_AP[i - 2u] : RV_L[i - 6u] + RV_MOD;
        d->pos = 0;
        a += d->len * 2u;
        for (k = 0; k < d->len; k++)
            d->buf[k] = 0;
    }
    for (i = 0; i < 8u; i++)
        hl.lp[i] = 0.0f;
    hl.hp_l = hl.hp_r = hl.mod_ph = hl.pk = 0.0f;
    hl.tail = hl.live = 0;
    dly_clear();
    ens_pos = dly_pos = 0;
    ens_ph1 = ens_ph2 = wow_ph = flut_ph = 0.0f;
    dly_read = 4410.0f;
    lim_gain = lim_tgt = 1.0f;
    for (i = 0; i < LIM_LA; i++)
        lim_dl[i] = lim_dr[i] = 0.0f;
    lim_pos = 0;
}

static inline float dly_tap(const int16_t *buf, float back, int32_t *live)   /* a leg read a fraction back */
{
    uint32_t b = (uint32_t)back, i0 = dly_pos + DLY_HALF - b, i1;
    if (i0 >= DLY_HALF)
        i0 -= DLY_HALF;
    i1 = i0 == 0u ? DLY_HALF - 1u : i0 - 1u;
    *live |= buf[i0] | buf[i1];
    return ((float)buf[i0] + ((float)buf[i1] - (float)buf[i0]) * (back - (float)b)) * (1.0f / 32767.0f);
}
static inline void dly_put(int16_t *buf, float c, int32_t *live)
{
    c *= 32767.0f;
    buf[dly_pos] = (int16_t)(c > 32767.0f ? 32767.0f : c < -32767.0f ? -32767.0f : c);
    *live |= buf[dly_pos];
}
static inline float dly_leg(uint32_t s, float c, float g, float lpc)   /* what goes into a leg: smeared, darker, softer */
{
    float y;
    c = allpass(&dly_ap[2u * s], c, g);
    c = allpass(&dly_ap[2u * s + 1u], c, g);
    dly_lp[s] += (c - dly_lp[s]) * lpc;             /* the repeats get darker */
    c = dly_lp[s] * 1.2f;                           /* and softer: a cubic clip */
    c = c > 1.5f ? 1.5f : c < -1.5f ? -1.5f : c;
    c = (c - c * c * c * (4.0f / 27.0f)) * 0.85f;
    y = c - dly_dc_x[s] + 0.995f * dly_dc_y[s];     /* no DC build-up */
    dly_dc_x[s] = c;
    dly_dc_y[s] = y;
    return y;
}

void fx_pre(float *l, float *r, uint32_t n, const fx_params_t *p)   /* the ensemble and the delay, in place */
{
    uint32_t i;
    float bus_pk = block_peak(l, r, n);  /* what the next stage gets: silence into a silent stage lets it rest */
    /* ensemble */
    uint32_t ens_on = !(p->bypass & FX_BYP_ENS) && p->ens_mode != ENS_OFF && p->ens_mix > 0.001f;
    if (p->ens_mode != ENS_OFF)
        ens_shape = p->ens_mode;
    if (bus_pk > 0.0f && ens_on)
        ens_tail = ENS_LEN / n + 2u;     /* its line holds the last 11.6 ms */
    else if (ens_tail)
        ens_tail--;
    if (ens_tail || (!ens_on && ens_g > 0.0f)) {
        float inc1 = 0.513f / FS, inc2 = 0.863f / FS, depth, base = 0.0035f * FS, wet = 0.8f * fm_sqrtf(p->ens_mix),
              dry = 1.0f - 0.4f * p->ens_mix, g_to = ens_on ? 1.0f : 0.0f;   /* the wet rises as a square root */
        depth = (ens_shape == ENS_CHORUS1 ? 0.0012f : ens_shape == ENS_CHORUS2 ? 0.0018f : 0.0015f) * FS;
        for (i = 0; i < n; i++) {
            float m, dl, dr, wl, wr;
            uint32_t b0;
            ens_ph1 += inc1;
            if (ens_ph1 >= 1.0f)
                ens_ph1 -= 1.0f;
            ens_ph2 += inc2;
            if (ens_ph2 >= 1.0f)
                ens_ph2 -= 1.0f;
            m = ens_shape == ENS_CHORUS1 ? tri(ens_ph1) : ens_shape == ENS_CHORUS2 ? tri(ens_ph2)
                                                                                   : 0.5f * (tri(ens_ph1) + tri(ens_ph2));
            ens_l[ens_pos] = l[i];
            ens_r[ens_pos] = r[i];
            dl = base + depth * m;
            dr = base - depth * m;
            if (ens_dl < 0.0f) {         /* from rest: the line is silent, no glide needed */
                ens_dl = dl;
                ens_dr = dr;
            }
            ens_dl += (dl - ens_dl) * 0.001f;   /* about 23 ms: a slight bend, never a jump */
            ens_dr += (dr - ens_dr) * 0.001f;
            dl = ens_dl;
            dr = ens_dr;
            ens_g += (g_to - ens_g) * 0.003f;
            if (!ens_on && ens_g < 0.0001f)
                ens_g = 0.0f;
            b0 = (uint32_t)dl;
            {
                uint32_t i0 = (ens_pos + ENS_LEN - b0) & (ENS_LEN - 1u), i1 = (i0 + ENS_LEN - 1u) & (ENS_LEN - 1u);
                float f = dl - (float)b0;
                wl = ens_l[i0] + (ens_l[i1] - ens_l[i0]) * f;
            }
            b0 = (uint32_t)dr;
            {
                uint32_t i0 = (ens_pos + ENS_LEN - b0) & (ENS_LEN - 1u), i1 = (i0 + ENS_LEN - 1u) & (ENS_LEN - 1u);
                float f = dr - (float)b0;
                wr = ens_r[i0] + (ens_r[i1] - ens_r[i0]) * f;
            }
            ens_pos = (ens_pos + 1u) & (ENS_LEN - 1u);
            l[i] = l[i] * (1.0f - (1.0f - dry) * ens_g) + wl * wet * ens_g;
            r[i] = r[i] * (1.0f - (1.0f - dry) * ens_g) + wr * wet * ens_g;
        }
        pk_ens = block_peak(l, r, n);
        bus_pk = pk_ens;
    } else {
        ens_dl = -1.0f;
        float inc1 = 0.513f / FS, inc2 = 0.863f / FS;
        for (i = 0; i < n; i++) {        /* at rest or off: its LFOs keep time, sample by sample, and the line empties
                                          * (switched on again, it does not replay what passed when it went off) */
            ens_ph1 += inc1;
            if (ens_ph1 >= 1.0f)
                ens_ph1 -= 1.0f;
            ens_ph2 += inc2;
            if (ens_ph2 >= 1.0f)
                ens_ph2 -= 1.0f;
            ens_l[ens_pos] = ens_r[ens_pos] = 0.0f;
            ens_pos = (ens_pos + 1u) & (ENS_LEN - 1u);
        }
        pk_ens = 0.0f;
    }
    /* the delay: mono in, two legs read at a slowly wandering distance, dark and soft in the feedback */
    if ((p->dly_send > 0.001f && bus_pk > 0.0f) || dly_live)
        dly_tail = DLY_LEN / n + 2u;     /* both legs' length: nothing in or out that long, and it is all zeros */
    else if (dly_tail)
        dly_tail--;
    if (dly_tail && !(p->bypass & FX_BYP_DLY)) {
        float tgt = p->dly_time_s * FS - (float)DLY_AP_SUM, slew = 0.0004f, fb = 0.95f * fm_sqrtf(p->dly_fdbk),
              lpc = 0.35f, send = p->dly_send, g = 0.6f * p->dly_diff, maxr = (float)(DLY_HALF - 128u),
              ratio = p->dly_ratio < 0.25f ? 0.25f : p->dly_ratio > 4.0f ? 4.0f : p->dly_ratio;
                                         /* the feedback as a square root: repeats from the first turn, 0.95 at full */
        float wf0, wf1, dwf;
        int32_t live = 0;
        wf0 = 1.0f + 0.0015f * fm_sin_turns(wow_ph) + 0.0003f * fm_sin_turns(flut_ph);   /* wow and flutter: the
                                                                              * ends of the block, a line between */
        wow_ph += 0.35f * (float)n / FS;
        if (wow_ph >= 1.0f)
            wow_ph -= 1.0f;
        flut_ph += 6.3f * (float)n / FS;
        if (flut_ph >= 1.0f)
            flut_ph -= 1.0f;
        wf1 = 1.0f + 0.0015f * fm_sin_turns(wow_ph) + 0.0003f * fm_sin_turns(flut_ph);
        dwf = (wf1 - wf0) / (float)n;
        if (tgt > maxr)                  /* wow and flutter read up to 0.18 % further back */
            tgt = maxr;
        if (tgt < 64.0f)
            tgt = 64.0f;
        for (i = 0; i < n; i++) {
            float in = (l[i] + r[i]) * 0.5f * send, backl, backr, outl, outr, c, m;
            dly_read += (tgt - dly_read) * slew;       /* a time change bends the pitch, like tape */
            wf0 += dwf;
            backl = dly_read * wf0;
            backr = dly_read * ratio;
            if (backr > maxr)
                backr = maxr;
            if (backr < 64.0f)
                backr = 64.0f;
            backr *= wf0;
            outl = dly_tap(dly, backl, &live);
            outr = dly_tap(dly + DLY_HALF, backr, &live);
            c = dly_leg(0, in + outr * fb, g, lpc);    /* the left leg: the input, and the right's repeat back */
            dly_put(dly, c, &live);
            c = dly_leg(1, outl * fb, g, lpc);         /* the right leg: the left's repeat crossing over */
            dly_put(dly + DLY_HALF, c, &live);
            dly_pos = dly_pos + 1u == DLY_HALF ? 0u : dly_pos + 1u;
            m = (outl + outr) * 0.5f;
            l[i] += m + (outl - m) * DLY_WIDTH;
            r[i] += m + (outr - m) * DLY_WIDTH;
        }
        dly_live = live != 0;
        for (i = 0; i < 2u; i++) {
            dly_lp[i] = fm_flush(dly_lp[i]);
            dly_dc_x[i] = fm_flush(dly_dc_x[i]);
            dly_dc_y[i] = fm_flush(dly_dc_y[i]);
        }
        pk_dly = block_peak(l, r, n);
    } else {
        float tgt = p->dly_time_s * FS - (float)DLY_AP_SUM, slew = 0.0004f;
        if (tgt > (float)(DLY_HALF - 128u))
            tgt = (float)(DLY_HALF - 128u);
        if (tgt < 64.0f)
            tgt = 64.0f;
        for (i = 0; i < n; i++)          /* at rest: the time still settles */
            dly_read += (tgt - dly_read) * slew;
        wow_ph += 0.35f * (float)n / FS;   /* wow and flutter keep their phase */
        if (wow_ph >= 1.0f)
            wow_ph -= 1.0f;
        flut_ph += 6.3f * (float)n / FS;
        if (flut_ph >= 1.0f)
            flut_ph -= 1.0f;
        pk_dly = 0.0f;
        dly_live = 0;
    }
}

/* the hall's wet for a block of input, written to outl/outr (the dry is not in it); its own function so the second
 * core can run it a block behind the bus */
void fx_hall(const float *l, const float *r, float *outl_b, float *outr_b, uint32_t n, const fx_params_t *p)
{
    uint32_t i;
    float bus_pk = block_peak(l, r, n);
    if ((p->rev_send > 0.001f && bus_pk > 0.0f) || hl.live || p->freeze)
        hl.tail = 80u;                   /* past the longest line (23 blocks): nothing in or out that long, all zeros */
    else if (hl.tail)
        hl.tail--;
    if (hl.tail && !(p->bypass & FX_BYP_REV)) {
        float live = 0.0f, g[8], gm = 0.0f;
        float rt60 = 0.6f + 7.4f * p->rev_decay * p->rev_decay, dampc = 1.0f - (0.03f + 0.55f * p->rev_damp),
              send = p->rev_send * 0.2f, hpc = 0.017f, s8 = 0.35355339f, out_g;
        float shim_g = 0.0f, shim_inc = 1.0f / SHIM_W, shim_lpc = 0.25f, frz_t = p->freeze ? 3.5f : 1.0f, wander[4];
        uint32_t shim_on = p->shimmer > 0.001f && !(p->bypass & FX_BYP_SHIM) && !p->freeze, k;
        hl.mod_ph += 0.45f * (float)n / FS;   /* the wander, once a block: at 0.45 Hz it moves a tenth of a sample
                                               * in a block, nothing a per-sample sine would add */
        if (hl.mod_ph >= 1.0f)
            hl.mod_ph -= 1.0f;
        for (k = 0; k < 4u; k++)
            wander[k] = 6.0f * fm_sin_turns(hl.mod_ph + 0.25f * (float)k);
        for (k = 0; k < 8u; k++) {       /* each line's gain for the same decay time, whatever its length */
            g[k] = fm_expf(-6.908f * (float)RV_L[k] / (rt60 * FS));
            gm += g[k] * 0.125f;
        }
        if (p->freeze) {                 /* frozen: the network rings on its own, nothing in, nothing lost to damping */
            for (k = 0; k < 8u; k++)
                g[k] = 0.9998f;
            gm = 0.9998f;
            dampc = 1.0f;
            send = 0.0f;
        }
        out_g = 0.8f + 4.0f * (1.0f - gm);   /* the lines' sum: 6 dB under the dry at F1's SPACE, 3 under at full (measured) */
        if (shim_on) {                   /* the octave up fed back, less the longer the hall already rings; the loop
                                          * stays bounded up to the cap (measured: a let-go chord dies away at every size) */
            shim_g = p->shimmer * 0.6f * (1.0f - gm);
            if (shim_g > 0.3f)
                shim_g = 0.3f;
        }
        for (i = 0; i < n; i++) {
            float xl = l[i] * send, xr = r[i] * send, al, ar, v[8], y[8], outl, outr, sh = 0.0f;
            if (shim_on) {               /* the last output an octave up: two heads a half grain apart, triangle windows */
                float w1 = hl.shim_ph, w2 = hl.shim_ph + 0.5f, d1, d2, g1, g2, sv;
                uint32_t b, i0, i1;
                if (w2 >= 1.0f)
                    w2 -= 1.0f;
                d1 = 1.0f + SHIM_W * (1.0f - w1);
                d2 = 1.0f + SHIM_W * (1.0f - w2);
                g1 = 1.0f - fm_fabsf(2.0f * w1 - 1.0f);
                g2 = 1.0f - g1;
                b = (uint32_t)d1;
                i0 = (hl.shim_pos + SHIM_LEN - b) & (SHIM_LEN - 1u);
                i1 = (i0 + SHIM_LEN - 1u) & (SHIM_LEN - 1u);
                sv = g1 * ((float)hl.shim[i0] + ((float)hl.shim[i1] - (float)hl.shim[i0]) * (d1 - (float)b));
                b = (uint32_t)d2;
                i0 = (hl.shim_pos + SHIM_LEN - b) & (SHIM_LEN - 1u);
                i1 = (i0 + SHIM_LEN - 1u) & (SHIM_LEN - 1u);
                sv += g2 * ((float)hl.shim[i0] + ((float)hl.shim[i1] - (float)hl.shim[i0]) * (d2 - (float)b));
                sv *= 1.0f / 32767.0f;
                hl.shim_lp += (sv - hl.shim_lp) * shim_lpc;   /* softened: the octaves pile up without glare */
                sh = hl.shim_lp * shim_g;
                hl.shim_ph += shim_inc;
                if (hl.shim_ph >= 1.0f)
                    hl.shim_ph -= 1.0f;
            }
            hl.hp_l += (xl - hl.hp_l) * hpc;   /* the sub stays dry: a high-pass near 120 Hz on the way in */
            hl.hp_r += (xr - hl.hp_r) * hpc;
            xl -= hl.hp_l;
            xr -= hl.hp_r;
            line_put(&hl.pre[0], xl);
            line_put(&hl.pre[1], xr);
            al = line_at(&hl.pre[0], RV_PRE - 1u);
            ar = line_at(&hl.pre[1], RV_PRE - 1u);
            al = allpass(&hl.ap[0], al, 0.65f);   /* one diffusing allpass a side: the network does the rest */
            ar = allpass(&hl.ap[2], ar, 0.65f);
            al += sh;
            ar += sh;
            for (k = 0; k < 8u; k++) {   /* the lines out, damped and decayed; the odd four wander a little */
                v[k] = (k & 1u) ? line_at_f(&hl.fd[k], (float)RV_L[k] + wander[k >> 1]) : line_at(&hl.fd[k], RV_L[k]);
                hl.lp[k] += (v[k] - hl.lp[k]) * dampc;
                v[k] = hl.lp[k] * g[k];
            }
            {                            /* the Hadamard mix, 8 x 8, scaled to keep the energy */
                float a0 = v[0] + v[1], a1 = v[0] - v[1], a2 = v[2] + v[3], a3 = v[2] - v[3];
                float a4 = v[4] + v[5], a5 = v[4] - v[5], a6 = v[6] + v[7], a7 = v[6] - v[7];
                float b0 = a0 + a2, b1 = a1 + a3, b2 = a0 - a2, b3 = a1 - a3;
                float b4 = a4 + a6, b5 = a5 + a7, b6 = a4 - a6, b7 = a5 - a7;
                y[0] = (b0 + b4) * s8;
                y[1] = (b1 + b5) * s8;
                y[2] = (b2 + b6) * s8;
                y[3] = (b3 + b7) * s8;
                y[4] = (b0 - b4) * s8;
                y[5] = (b1 - b5) * s8;
                y[6] = (b2 - b6) * s8;
                y[7] = (b3 - b7) * s8;
            }
            line_put(&hl.fd[0], y[0] + al);
            line_put(&hl.fd[1], y[1] + ar);
            line_put(&hl.fd[2], y[2] - al);
            line_put(&hl.fd[3], y[3] - ar);
            line_put(&hl.fd[4], y[4] + al);
            line_put(&hl.fd[5], y[5] + ar);
            line_put(&hl.fd[6], y[6] - al);
            line_put(&hl.fd[7], y[7] - ar);
            outl = out_g * (v[0] - v[2] + v[4] - v[6]);
            outr = out_g * (v[1] - v[3] + v[5] - v[7]);
            hl.frz_g += (frz_t - hl.frz_g) * 0.0002f;
            outl *= hl.frz_g;
            outr *= hl.frz_g;
            live += al * al + outl * outl + outr * outr;   /* anything at all, in or out: still ringing */
            {                            /* the output into the shimmer's line, whether it reads it or not */
                float c = (outl + outr) * 0.175f * 32767.0f;
                hl.shim[hl.shim_pos] = (int16_t)(c > 32767.0f ? 32767.0f : c < -32767.0f ? -32767.0f : c);
                hl.shim_pos = (hl.shim_pos + 1u) & (SHIM_LEN - 1u);
            }
            if (shim_on) {               /* and the octave up heard straight away, outside the loop */
                float so = hl.shim_lp * p->shimmer * 6.0f;
                outl += so;
                outr += so;
            }
            outl_b[i] = outl;
            outr_b[i] = outr;
        }
        for (k = 0; k < 8u; k++)
            hl.lp[k] = fm_flush(hl.lp[k]);
        hl.hp_l = fm_flush(hl.hp_l);
        hl.hp_r = fm_flush(hl.hp_r);
        hl.shim_lp = fm_flush(hl.shim_lp);
        hl.live = live > 0.0f;
        hl.pk = block_peak(outl_b, outr_b, n);
    } else {
        hl.mod_ph += 0.45f * (float)n / FS;   /* at rest, the wander keeps its phase */
        if (hl.mod_ph >= 1.0f)
            hl.mod_ph -= 1.0f;
        for (i = 0; i < n; i++)
            outl_b[i] = outr_b[i] = 0.0f;
        hl.pk = 0.0f;
        hl.live = 0;
    }
}

void fx_post(float *l, float *r, uint32_t n)   /* the limiter: 1.5 ms of lookahead, so the gain ramps down ahead of
                                                * a peak instead of cutting into it (a cut is a click), 250 ms
                                                * release, so nothing the effects add can clip */
{
    uint32_t i;
    float rel = 1.0f / (0.25f * FS), thr = 0.92f, ka = 4.0f / (float)LIM_LA;   /* at the peak the gain is 98 % there,
                                                                               * so 0.92 keeps the output under -0.5 dBFS */
    for (i = 0; i < n; i++) {
        float pk = fm_fabsf(l[i]) > fm_fabsf(r[i]) ? fm_fabsf(l[i]) : fm_fabsf(r[i]);
        float need = pk > thr ? thr / pk : 1.0f, ol = lim_dl[lim_pos], or_ = lim_dr[lim_pos];
        if (need < lim_tgt)
            lim_tgt = need;
        else
            lim_tgt += (1.0f - lim_tgt) * rel;
        if (lim_gain > lim_tgt)
            lim_gain += (lim_tgt - lim_gain) * ka;
        else
            lim_gain = lim_tgt;
        if (lim_gain < lim_min)
            lim_min = lim_gain;
        lim_dl[lim_pos] = l[i];
        lim_dr[lim_pos] = r[i];
        lim_pos = (lim_pos + 1u) & (LIM_LA - 1u);
        l[i] = ol * lim_gain;
        r[i] = or_ * lim_gain;
    }
}
