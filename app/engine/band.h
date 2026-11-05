/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann */
/* The band's own declarations beyond gn.h: its sizes, the arena it carves (band_init lays it out as gn.h says:
 * the effects, the stutter ring, the bus and hall buffers on their own cache lines, the second core's job copy,
 * then the voices) and a diagnostic for the harness and the integrator. band.c includes this; nothing else needs to. */
#pragma once
#include <stdint.h>
#include "gn.h"
#include "fx.h"

#define BAND_MAXBLOCK 256u               /* frames a render call handles at once (APP_BLOCK; a longer call is split) */
#define BAND_NLEAD 6u
#define BAND_NSTAB 3u
#define BAND_RING_FRAMES 12288u          /* the stutter ring, mono int16: a 1/8 note down to 107 BPM */

uint32_t band_arena_used(void);          /* bytes band_init carved out of the arena (0 before it, or when it failed) */
