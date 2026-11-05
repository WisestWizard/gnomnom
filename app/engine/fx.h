/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann */
/* GNOMNOM's master effects, from DEEP FIELD's engine/fx.c and fx.h (which came from Pocket Poly's): ensemble
 * (Juno-style chorus, two LFOs, antiphase left and right), tape delay (two legs of 0.418 s on one 16-bit line, the
 * right leg dly_ratio times the left, wow and flutter, dark saturated feedback, two allpasses a leg), a hall (an
 * eight-line feedback delay network, 16-bit lines, pre-delay) with a shimmer and a freeze, a peak limiter. Stereo
 * float in place; memory from the arena. It includes gn.h for the rate and ENS_*, nothing of DEEP FIELD's. */
#pragma once
#include <stdint.h>

typedef struct {
    uint32_t ens_mode;                   /* ENS_* (gn.h) */
    float ens_mix;                       /* 0..1 */
    float dly_time_s;                    /* 0.05 .. 0.418: the left leg */
    float dly_ratio;                     /* the right leg's length as a multiple of the left: 1.0 puts both on the grid */
    float dly_fdbk;                      /* 0..1 */
    float dly_send;                      /* 0..1 */
    float dly_diff;                      /* 0..1: the repeats smeared into a cloud */
    float rev_decay;                     /* 0..1: the decay time, 0.6 to 8 s */
    float rev_damp;                      /* 0..1 */
    float rev_send;                      /* 0..1 */
    float shimmer;                       /* 0..1: the octave-up feedback into the hall */
    uint32_t freeze;                     /* the hall holds: no input, no decay, no damping */
    uint32_t bypass;                     /* FX_BYP_*: a stage skipped, its lines left as they are */
} fx_params_t;

#define FX_BYP_ENS 1u
#define FX_BYP_DLY 2u
#define FX_BYP_REV 4u
#define FX_BYP_SHIM 8u

#define FX_ENS_LEN 512u                  /* 11.6 ms a channel, float */
#define FX_DLY_LEN 36864u                /* two legs of 0.418 s, int16 */
#define FX_DLY_AP 1588u                  /* the legs' four allpasses, int16 */
#define FX_RV_TOTAL 33291u               /* the hall's fourteen lines, int16 (fx.c checks the sum) */
#define FX_SHIM_LEN 4096u                /* the shimmer's window, int16 */
#define FX_BYTES (FX_ENS_LEN * 2u * 4u + FX_DLY_LEN * 2u + FX_DLY_AP * 2u + FX_RV_TOTAL * 2u + FX_SHIM_LEN * 2u + 64u)

uint32_t fx_bytes(void);                 /* the arena the effects need (FX_BYTES) */
void fx_init(void *arena);
void fx_pre(float *l, float *r, uint32_t n, const fx_params_t *p);    /* the ensemble and the delay, in place */
void fx_hall(const float *l, const float *r, float *outl, float *outr, uint32_t n, const fx_params_t *p);
                                         /* the hall's wet of this block of input, written to outl/outr; it may run
                                          * on the second core, a block behind the bus: it touches only its own
                                          * lines and state, which sit on their own cache lines */
void fx_post(float *l, float *r, uint32_t n);   /* the limiter */
void fx_clear(void);                     /* the ensemble and the delay emptied (a panic); the hall is left to ring */
void fx_peaks(float *ens, float *dly, float *rev);   /* each stage's output peak in the last block it ran */
uint32_t fx_running(void);               /* the stages not at rest: 1 ensemble, 2 delay, 4 reverb */
float fx_lim_min(void);                 /* the limiter's deepest gain since the last call, 0..1 (1: idle) */
