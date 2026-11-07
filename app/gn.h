/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann */
/* GNOMNOM's shared declarations: the contract between tunes.c, music.c, game.c, ui.c and gfx.c (one unity unit:
 * app.c includes gn.h, then gfx.c, tunes.c, music.c, game.c, ui.c, in that order) and the engine units
 * engine/band.c and engine/fx.c (their own -O2 units; they include gn.h for GN_FS, ENS_* and the band's enums,
 * nothing of DEEP FIELD's). docs/DESIGN.md explains the game; this header is the law when the two disagree.
 *
 * Contexts. Render (the audio interrupt): game_*, music_*, band_*, gn_midi_*. Main loop: ui_*, gfx_*,
 * gn_settings_save, and the few game_* calls section "main-loop callers" allows. Nothing else crosses.
 *
 * The unity unit holds, before app.c: fm1-base's gfx.c (pal, palette_set, swap16, cv_*, glyph, text_w,
 * draw_text_box, palette_t, PALETTES, RGB, C_BLACK, C_WHITE, NEUTRALS, C_LINE, C_DIM, C_GRAY, C_AMB, C_HI), lcd.c
 * (lcd_*), announce.c (ann_*, ANN_*), libc (memcpy, memset, memcmp, strcmp...); and after it main_fm1.c (FS,
 * REND_FRAMES, HALF_WORDS). GNOMNOM's files define none of these names: palette indices are GN_C_*, the sample
 * rate is GN_FS, our palette is gn_pal. GN_TUNES and GN_STAGES (tunes.c) and gn_cmd, gn_set, gn_set_dirty (app.c)
 * are defined without static so the externs here match; every other file-level name in the unit is static and
 * carries its module's prefix (gx_, tn_, mu_, gm_, ui_). Every stage number in this header is the GN_STAGES
 * index 0..7 (endless keeps 7 and counts rounds 1, 2 ...); only the UI adds 1 when it prints STAGE n.
 *
 * Nothing here uses 64-bit integers (the silicon bug), doubles, libm or malloc. Render-context code keeps its
 * locals small (the interrupt stack is 7.9 KB) and never draws, logs, waits or touches the flash. */
#pragma once
#include <stdint.h>

#define GN_BARRIER() __asm__ volatile("" ::: "memory")   /* a compiler barrier: the ISR and the main loop never
                                                           * run at once on this core, so this is all the two
                                                           * sides need around shared words */

/* ---- time: ticks of the song */
#define GN_FS 44117.6f                   /* the codec's real rate: 192 MHz / 17 / 256 */
#define GN_PPQ 96u                       /* ticks a beat */
#define GN_STEP (GN_PPQ / 4u)            /* a 16th */
#define GN_BAR (GN_PPQ * 4u)             /* a 4/4 bar; the IDM 7/8 bar is music_bar_ticks() = GN_BAR - 2 * GN_STEP:
                                          * never use GN_BAR as a divisor of the song position */
#define GN_CTL 32u                       /* samples a control tick in the band */
#define GN_CLOCK_TICKS (GN_PPQ / 24u)    /* ticks between MIDI clocks */

/* ---- lanes and keys */
#define GN_LANES 7u                      /* C D E F G A B, left to right */
#define GN_NWHITE 16u
#define GN_NBLACK 11u
#define GN_STASH 3u                      /* the lowest three black keys, F#3 G#3 A#3 */
#define GN_NO_LANE 255u
/* white key w (0..15, F3..G5) -> lane, and the register it plays in. F3 G3 A3 B3 are no lane: they play their
 * letter's note in register -1 ("air", never an eat), so the left hand owns the stash and nothing dangerous */
#define GN_WKEY_LANE {255, 255, 255, 255, 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4}
#define GN_WKEY_REG {-1, -1, -1, -1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1}
#define GN_WKEY_LETTER {3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4}   /* the letter as a lane number, every key */
#define GN_WHITE_KEYS {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26}   /* plat.h PLAT_WHITE_KEYS */
#define GN_BLACK_KEYS {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25}
/* black key b (0..10): the white key (0..15) to its left, and its register; its note is music_spice_note(b) */
#define GN_BLACK_LEFT {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14}
#define GN_BLACK_REG {-1, -1, -1, 0, 0, 0, 0, 0, 1, 1, 1}

/* ---- species and abilities */
enum { SP_PORCINI, SP_AGARIC, SP_DEATHCAP, SP_MOREL, SP_CHANTERELLE, SP_SHIITAKE, SP_MEANIE, SP_GIANT, SP_FROST,
       SP_PIXIE, SP_PUFFBALL, SP_BOSS, SP_COUNT };
enum { AB_NONE, AB_ARP, AB_CHORD, AB_ECHO, AB_SLOW, AB_WIDE, AB_FREEZE, AB_SHIMMER, AB_BURP, AB_COUNT };
#define GN_SPECIES_ABILITY {AB_NONE, AB_NONE, AB_NONE, AB_ARP, AB_CHORD, AB_ECHO, AB_SLOW, AB_WIDE, AB_FREEZE, \
                            AB_SHIMMER, AB_BURP, AB_NONE}
#define GN_ABILITY_SPECIES {SP_PORCINI, SP_MOREL, SP_CHANTERELLE, SP_SHIITAKE, SP_MEANIE, SP_GIANT, SP_FROST, \
                            SP_PIXIE, SP_PUFFBALL}
/* bars each lasts: SLOW's are bars of the slowed clock (4 real bars); FREEZE's is real time at the tempo it froze
 * at; BURP is instant */
#define GN_ABILITY_BARS {0, 4, 8, 8, 2, 8, 1, 8, 0}
/* the stash is three fixed slots, one a black key (F#3 G#3 A#3): a pickup fills the lowest empty one, a full stash
 * gives up its oldest pickup in place, using one leaves its slot empty; the others never move */

/* ---- states and sections */
enum { GS_TITLE, GS_ATTRACT, GS_PLAY, GS_PAUSE, GS_OVER, GS_COUNT };
enum { SEC_TITLE, SEC_INTRO, SEC_TUNE, SEC_BOSS, SEC_CLEAR, SEC_OVER, SEC_COUNT };   /* SEC_COUNT to game_on_section:
                                                                                      * the stage is over */
enum { Q_NONE, Q_GOOD, Q_PERFECT };    /* an eat's timing */
#define GN_WIN_PERFECT_MS 45u
#define GN_WIN_GOOD_MS 110u
/* a clam: a lane key (or a MIDI note on a lane) that eats nothing while a melody mushroom in another lane is inside
 * its GOOD window, or within GN_WIN_GOOD_MS of the hit of one a key just ate in another lane (a slap of every key):
 * a sour note (BN_SOUR | BN_SOFT, vel 80) for the air note, the combo ends, a third of the stage's miss_trip, GE_CLAM.
 * Never under FREEZE, never for CHORD's or WIDE's extra lanes, the air keys F3..B3 or the black keys; a press with
 * nothing due anywhere stays free air */
#define GN_BOSS_CHORD_MS 90u             /* the three keys within this of each other */
#define GN_VIS_LEAD_MS 30u               /* the snapshot's positions run this far ahead of the song: the frame and
                                          * the panel are that late, so the eye and the ear agree */
#define GN_TRIP_MAX 1000
#define GN_TRIP_START 400                /* and the floor every stage starts from */
#define GN_NSTAGES 8u
#define GN_BPM_CAP 180u                  /* endless never passes this */
#define GN_OVER_GAP 40u                  /* blocks of silence at GAME OVER before the march: the settings save
                                          * (which stops the audio) happens in that gap */
#define GN_COUNTIN_BEATS 4u              /* a resume: four clicks, then the song */

/* ---- the stage table and the tunes (tunes.c, const in flash) */
enum { GENRE_FOLK, GENRE_HOUSE, GENRE_DEEP, GENRE_ACID, GENRE_RAVE, GENRE_BREAKS, GENRE_IDM, GENRE_HARD, GENRE_COUNT };
enum { KIT_BRUSH, KIT_HOUSE, KIT_TECHNO, KIT_BREAKS, KIT_GLITCH, KIT_COUNT };
enum { BS_SOFT, BS_HOUSE, BS_ACID, BS_HARD, BS_COUNT };
enum { SS_OFF, SS_STAB, SS_PAD, SS_HOOVER, SS_COUNT };
enum { LP_PLUCK, LP_SAW, LP_SQUARE, LP_BELL, LP_COUNT };
enum { VT_FOREST, VT_DUSK, VT_NIGHT, VT_RED, VT_KALEIDO, VT_MIRROR4, VT_EYES, VT_CHAOS, VT_COUNT };
enum { TUNE_MOUNTAIN, TUNE_KOROBEINIKI, TUNE_CANON, TUNE_HUNGARIAN, TUNE_FATE, TUNE_TURCA, TUNE_BUMBLEBEE,
       TUNE_CANCAN, TUNE_FUNERAL, TUNE_COUNT };

/* a melody note: deg bits 0..3 the degree 1..7 (0 a rest), bits 4..5 the alteration (GN_SHARP / GN_FLAT: the
 * note a semitone off its degree; the lane is still the degree, the pitch carries the alteration) */
#define GN_SHARP(d) (16u | (d))
#define GN_FLAT(d) (32u | (d))
#define GN_DEG(d) ((d) & 15u)
#define GN_ALT(d) (((d) & 16u) ? 1 : ((d) & 32u) ? -1 : 0)
typedef struct {
    uint8_t deg;
    int8_t oct;                          /* -1..2 octaves from the tune's middle register */
    uint8_t dur;                         /* in 16ths; the note's time is the sum of the durations before it */
} gn_note_t;
/* a bar's chord: bits 0..3 the root degree 1..7, bit 7 force minor, bit 6 force major (else the mode's triad) */
#define GN_CH_MINOR 128u
#define GN_CH_MAJOR 64u
typedef struct {
    const char *name;
    uint8_t mode[7];                     /* the tune's own mode: semitones above the root for degrees 1..7 */
    uint8_t bars;                        /* its length in 4/4 bars; the durations sum to bars * 16 */
    uint8_t nnotes;
    const gn_note_t *notes;
    const uint8_t *chord;                /* bars entries */
} gn_tune_t;
typedef struct {
    const char *name;                    /* the title card's second line */
    uint8_t tune;                        /* TUNE_* */
    uint8_t mode[7];                     /* the stage's mode (the lanes' notes); the tune is played in it */
    uint8_t root;                        /* MIDI note of degree 1 in register 0 (60: lane C is middle C) */
    uint16_t bpm0, bpm1;                 /* across the TUNE section */
    uint8_t genre, kit, bass, stab, lead, theme;   /* GENRE_*, KIT_*, BS_*, SS_*, LP_*, VT_* */
    uint8_t fall_q;                      /* the fall in quarter beats (8 = two beats); never under 8 */
    uint8_t density;                     /* the mushroom grid in 16ths: 1 every melody note, 2 only onsets on the
                                          * 8th grid, 4 only onsets on a beat; the rest are GN_SPAWN_AUTO notes */
    uint16_t min_gap_ms;                 /* a melody note closer than this to the previous mushroom (any lane), or
                                          * than 1.5x this to the previous one in its lane, is an AUTO note too;
                                          * over TUNE's first two bars the gap is 1.5x this, the next two 1.25x */
    uint8_t intro_bars;                  /* 2 or 4 */
    uint8_t guide;                       /* 1: a quiet LEAD plays every melody note at its hit whether eaten or not */
    uint8_t miss_trip, poison_trip;      /* what a miss and a death cap cost */
    uint8_t agaric_bars, stash_bars;     /* about one agaric / stashable every so many bars (0 none) */
    uint8_t first_stash;                 /* AB_*: the stage's first stashable is always this one (AB_NONE: by weight) */
    uint8_t stash_w[8];                  /* weights for AB_ARP..AB_BURP */
    uint8_t deathcap_q;                  /* death caps a bar, in quarters (4 = one a bar) */
    uint8_t ens, dly_send, rev_send, shimmer, crush, ring, stutter, duck;   /* the band's baseline, 0..100;
                                          * stutter: the chance a fill bar gets a retrigger roll */
} gn_stage_t;
extern const gn_tune_t GN_TUNES[TUNE_COUNT];
extern const gn_stage_t GN_STAGES[GN_NSTAGES];

/* ---- the band (engine/band.c): parts, drums, parameters */
enum { PART_LEAD, PART_BASS, PART_STAB, PART_COUNT };   /* LEAD has 6 voices, BASS 1, STAB 3 */
enum { DR_KICK, DR_SNARE, DR_HAT, DR_OHAT, DR_CLAP, DR_RIM, DR_CRASH, DR_TOM, DR_SHAKER, DR_COUNT };
enum { ENS_OFF, ENS_CHORUS1, ENS_CHORUS2, ENS_ENSEMBLE, ENS_COUNT };   /* BP_ENS's values; fx_params_t.ens_mode */
#define BN_ACCENT 1u                     /* a bass accent; a lead eat at full level */
#define BN_SLIDE 2u                      /* the bass glides from its last note over a 16th */
#define BN_SOFT 4u                       /* a lead "air" or guide note: quiet and short */
#define BN_SOUR 8u                       /* a lead poison note: detuned cluster */
#define BN_NOTEOFF_SELF 16u              /* the band releases the note itself after its decay (no note off coming) */
enum {
    BP_LEAD_PATCH, BP_LEAD_CUTOFF, BP_LEAD_RESO, BP_LEAD_DECAY, BP_LEAD_RELEASE, BP_LEAD_DETUNE, BP_LEAD_GLIDE,
    BP_LEAD_LEVEL, BP_LEAD_TONE,         /* TONE: K1's offset, -1..1 */
    BP_BASS_STYLE, BP_BASS_CUTOFF, BP_BASS_RESO, BP_BASS_ENVMOD, BP_BASS_DECAY, BP_BASS_DRIVE, BP_BASS_LEVEL,
    BP_STAB_STYLE, BP_STAB_CUTOFF, BP_STAB_DECAY, BP_STAB_LEVEL,
    BP_KIT, BP_DRUM_LEVEL, BP_DRUM_CRUSH, BP_DUCK,
    BP_ENS, BP_ENS_MIX, BP_DLY_SEND, BP_DLY_DIV, BP_DLY_FDBK, BP_REV_SEND, BP_REV_SIZE, BP_REV_DAMP, BP_SHIMMER,
    BP_FREEZE,
    BP_CRUSH, BP_RING, BP_RING_HZ, BP_FILTER, BP_PITCH, BP_TEMPO, BP_SPACE,   /* SPACE: K2's offset, -1..1 */
    BP_TRIP,                             /* the trip meter's pull on the band, -1..1, game.c's each block */
    BP_COUNT
};
/* units: 0..1 unless said. BP_LEAD_PATCH LP_*; BP_BASS_STYLE BS_*; BP_STAB_STYLE SS_*; BP_KIT KIT_*; BP_ENS ENS_*;
 * BP_DLY_DIV 0 off, 1 = 1/16, 2 = 1/8, 3 = dotted 1/8, 4 = 1/4, synced to BP_TEMPO (the un-slowed tempo: SLOW
 * leaves the delay alone; the line holds 0.418 s, so 3 is valid from 108 BPM and 4 from 144: the band clamps);
 * BP_RING_HZ in Hz (music.c sets it to 1.5 x the bar's chord root each bar); BP_FILTER -1 (a low-pass closing
 * to 150 Hz) .. 0 .. 1 (a high-pass opening to 4 kHz); BP_PITCH semitones, on LEAD, STAB and BASS (a bass note
 * under MIDI 33 after the shift is folded up an octave), never the drums (SLOW lengthens their decays x1.5
 * instead when BP_PITCH <= -6); BP_TEMPO bpm; BP_FREEZE 0/1; BP_TRIP -1..1: 0 from trip 400 to 700, -1 at 100 and below, 1 at 900 and above
 * (game.c slews it over about 0.8 s): under 0 a two-pole low-pass on BAND-WET (after the stutter, before LEAD
 * joins) closes from 20 kHz toward 700 Hz and music.c drops the hats and shaker from -0.5, the stabs from -0.8
 * (each decided at a bar line); over 0 the delay and hall sends rise by up to 0.08 and the shimmer by 0.12. LEAD,
 * BASS and the kick never change with it. Every change smooths over 20 ms, BP_PITCH over 100 */
/* the arena: fx_bytes() ~155.8 KB (ensemble, tape delay, hall, shimmer window), the stutter ring 12288 mono
 * int16 frames (24 KB: a 1/8 note down to 109 BPM; slower, the slice halves and the repeats double), hall in/out
 * 4 x 1 KB and mix buffers on their own 32-byte
 * lines, the voices' state; the oscillators compute their waves (fastmath.h), no tables */
#define BAND_ARENA_BYTES (216u * 1024u)
int band_init(void *arena, uint32_t bytes);   /* 0 ok; -1 too small: it then renders silence */
void band_note_on(uint32_t part, uint32_t note, uint32_t vel, uint32_t flags);
void band_note_off(uint32_t part, uint32_t note);
void band_drum(uint32_t drum, uint32_t vel);  /* 1..127 */
void band_riser(uint32_t steps);              /* a noise riser over so many 16ths at BP_TEMPO, then a sub drop */
void band_stutter(uint32_t div, uint32_t repeats);   /* repeat the last 1/div note (8, 16, 32, at the un-slowed
                                                       * BP_TEMPO) of the band bus repeats times; LEAD is not in it */
void band_set(uint32_t p, float v);
float band_get(uint32_t p);
/* Called by app_render AFTER game_tick, never by game.c: n interleaved stereo frames, 24-bit, full scale
 * +-8388607 at master_q12 4096, scaled by master_q12 / 4096. Buses: DRY (KICK, TOM, BASS: no ensemble, delay or
 * hall; the bass's own soft clip only); BAND-WET (STAB, SNARE, CLAP, HAT, OHAT, RIM, SHAKER, CRASH with fixed send
 * trims: snare and clap 0.6, hats 0.3, crash 0.8, stab 1.0) -> ring mod -> decimator; LEAD (centred, decimator at
 * 40 % of BP_CRUSH, no ring mod) joins BAND-WET after those; the stutter ring captures DRY + BAND-WET in mono and
 * replaces both while it repeats; then the wet bus -> fx_pre (ensemble, delay) -> the hall (its wet of the PREVIOUS
 * block from core 2) -> + DRY -> fx_post (the limiter). The sidechain duck (BP_DUCK) on every KICK: 2 ms to the
 * floor 1 - 0.7 x duck, hold 30 ms, release over a 16th at BP_TEMPO, on STAB and the wet drums only, never LEAD
 * or BASS.
 * Core 2 (the hall a block behind, drone.c's half_job pattern): the arena holds hall_in[2][256] (the wet bus of
 * the last block), hall_out[2][256] (its wet) and a job copy of fx_params_t, each 32-byte aligned and padded to
 * whole cache lines with nothing else on them. Each block: copy the live params to the job copy; plat_cpu2_sync();
 * on_two = FM1B_CPU2 && plat_cpu2_on() && plat_cpu2_run(job, 0) == 1; render the voices, drums, stutter, crush,
 * ring, fx_pre into the wet bus; if (!on_two) fx_hall(hall_in, hall_out) here, else if (plat_cpu2_wait() != 0) zero
 * hall_out (this block's wet is lost, as DEEP FIELD accepts), else plat_cpu2_sync(); then copy the wet bus to
 * hall_in (the next block's hall input), wet += hall_out, fx_post. job() reads only hall_in and the job copy (handed over as its argument) and
 * writes only hall_out and the hall's own state (fx.c's lines, on their own cache lines), calls
 * nothing of plat_*; the main core touches hall_in and hall_out only between wait and the next run. On the host
 * the job runs inline inside plat_cpu2_run, before this core's share, so job() needs nothing this core writes
 * after run. */
void band_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void band_all_off(void);
void band_panic(void);                        /* voices silent, the delay and the stutter cleared; the hall rings out */
float band_peak(uint32_t part);               /* 0..1, the last block: PART_LEAD/BASS/STAB, PART_COUNT the drums,
                                               * PART_COUNT + 1 the output after the limiter */
uint32_t band_voices(void);                   /* pitched voices sounding */
/* MIDI out of what the band plays, from the render: channel 0..15 (LEAD 0, BASS 1, STAB 2, drums 9 with GM notes
 * KICK 36 SNARE 38 HAT 42 OHAT 46 CLAP 39 RIM 37 CRASH 49 TOM 45 SHAKER 70), note, velocity 1..127, on. A drum is
 * on then off 50 ms later; a BN_NOTEOFF_SELF note sends its off when its voice ends. fn may be null. The band never
 * calls plat_* itself; app.c's gn_midi_note applies the room policy */
void band_midi_out(void (*fn)(uint32_t ch, uint32_t note, uint32_t vel, int on));

/* ---- the composer and transport (music.c, render context) */
#define HOLD_PAUSE 1u
#define HOLD_FREEZE 2u
void music_init(void);
void music_title(void);                       /* the music-box loop (TUNE_MOUNTAIN at 70 BPM, LEAD BELL only) */
void music_over(void);                        /* GN_OVER_GAP blocks of silence, the funeral march once, then silence */
void music_silence(void);                     /* nothing plays, the transport stands */
void music_stage(uint32_t stage, uint32_t round, uint32_t seed);   /* stage 0..7; round 0, endless 1, 2 ...; the
                                               * stage's INTRO: sets every BP_* its row implies, tempo bpm0 + 6 x
                                               * round capped at GN_BPM_CAP, calls game_on_section(SEC_INTRO) */
void music_tick(uint32_t n);                  /* n samples: steps, spawns, the band's events, F8 clocks */
uint32_t music_now(void);                     /* ticks: an integer clock, uint32_t ticks + samples into the tick
                                               * (never a float accumulator: the simulator reproduces exactly) */
float music_now_f(void);                      /* ticks with the fraction */
uint32_t music_bpm(void);                     /* the un-slowed tempo, rounded */
uint32_t music_section(void);                 /* SEC_* */
uint32_t music_bar(void);                     /* bars into the section */
uint32_t music_bar_ticks(void);               /* GN_BAR, or GN_BAR - 2 * GN_STEP in an IDM 7/8 bar (bars 4n + 3
                                               * of TUNE in GENRE_IDM: steps 14 and 15 are dropped from the drums,
                                               * the bass and the melody alike; a melody note starting there is
                                               * neither spawned nor played, one sustaining into it ends at step
                                               * 14; bar_beat still runs 0..3 with the last beat two steps long) */
uint32_t music_beat_q8(void);                 /* 0..255 through the beat */
uint32_t music_fall(void);                    /* GN_STAGES[s].fall_q * GN_PPQ / 4, in ticks, fixed for the stage */
float music_ms_ticks(float ms);               /* ms of REAL time as ticks now: ms * bpm * rate * GN_PPQ / 60000, so
                                               * under SLOW the 45 / 110 ms windows stay 45 / 110 ms at the ear */
void music_rate(float r);                     /* 1 normal, 0.5 SLOW: ticks per sample halve; spawned hits stay valid.
                                               * BP_PITCH and BP_FILTER are game.c's to set, not music_rate's */
void music_hold(uint32_t reason, int on);     /* the transport stands while any HOLD_* is on: music_tick returns at
                                               * once (no steps, no spawns, music_now unchanged); FC is sent when the
                                               * first reason goes on, FB (continue) when the last lifts. band_render keeps
                                               * running (tails ring); HOLD_FREEZE also band_set(BP_FREEZE, 1) and
                                               * band_stutter(8, 8) so the music never stops; HOLD_PAUSE does not */
void music_arp(int on);                       /* ARP: on every 16th that has no melody note, LEAD plays the next tone
                                               * of the chord's arpeggio over two octaves (vel 70, BN_NOTEOFF_SELF) */
void music_bump(int32_t bpm);                 /* endless only: the agaric's +2 BPM, inside GN_BPM_CAP */
uint32_t music_lane_note(uint32_t lane, int32_t reg);   /* root + 12 * reg + mode[lane], reg clamped -1..1, 0..127;
                                               * mode is the stage's as rotated now (CHAOS), so it can change by bar */
uint32_t music_spice_note(uint32_t black);    /* black key 0..10: the pitch between its neighbouring lanes' notes:
                                               * pitch(left lane) + 1 if that is under pitch(right lane), else
                                               * pitch(right lane) + 1 (never a lane note), in GN_BLACK_REG's register */
void music_chord_lanes(uint8_t lanes[3]);     /* the current chord's root, 3rd and 5th as lanes (degree - 1, mod 7) */
uint32_t music_stash_pick(void);              /* the next stashable's AB_* (first_stash, then by weight; music's RNG) */

/* ---- spawns and the game (game.c, render context) */
#define GN_MAX_MUSH 24u
#define GN_SPAWN_AUTO 1u                      /* an AUTO note: no mushroom; at its hit tick game.c plays
                                               * band_note_on(PART_LEAD, note, 100, BN_NOTEOFF_SELF) if the previous
                                               * melody note of the tune (any lane) was eaten, else stays silent. It
                                               * holds a slot (MS_FALLING, this flag) so the UI draws its outline,
                                               * keys never eat it, it is never missed (no trip, combo or event
                                               * but GE_NOTE when it sounds) */
#define GN_SPAWN_CHAIN_END 2u                 /* the last of its chain */
#define GN_EATEN_HOLD 8u                      /* blocks an eaten slot stays MS_EATEN (two frames) before it frees */
#define GN_Y_GONE 320                         /* y256 past which a missed mushroom's slot frees */
/* Main-loop callers (app_expect, gn auto, the UI): game_state, game_count, game_auto, game_auto_on, game_auto_demo
 * read or write one 32-bit word each (volatile in game.c); game_next_hit_ms reads game.c's last snapshot, never
 * the live table; game_snapshot and game_event_pop are the UI's. game_init runs once from app_init before the audio
 * starts; a run's reset happens in the render on CMD_START with gn_cmd.seed. */
void game_init(uint32_t seed);
/* The render step, called by app_render ONLY (app.c runs midi_in() first, so MIDI keys arrived through
 * game_midi_key), then app_render calls band_render. In this order: 1 take gn_cmd (ack = req); 2 read plat_keys()
 * (a plain load) and diff against the last block's mask (buttons are the UI's: a key or a MIDI note ends the
 * attract here, a button through CMD_STOP, so it never acts on the title too); 3 judge the new presses, then the
 * misses, against music_now(), the clock at the START of this block (jitter +-2.9 ms, inside PERFECT); 4
 * abilities, trip, stash, the boss, the count-in; 5 music_tick(n), which advances the clock, steps the backing and
 * calls game_spawn / game_boss / game_on_section / game_on_kick (a spawn's hit is >= now + fall, fall >= 2 beats
 * and a block is under 2 ticks, so a spawn never lands inside its own window); 6 the autoplayer (hits inside this
 * block); 7 the snapshot. */
void game_tick(uint32_t n);
/* note: the melody's own MIDI note, GN_STAGES[s].root + 12 * oct + mode[deg - 1] + alteration (the STAGE's mode as
 * rotated now, never the tune's own), clamped 0..127. It sounds as given only when no key chose a register: an
 * AUTO note, the guide, an ARP auto-eat, an ECHO ghost eat, a BURP. A key eat in lane L by white key w sounds
 * note + 12 * (GN_WKEY_REG[w] - oct') where oct' is the register the note implies (note - root - mode[lane]) / 12
 * clamped -1..1: the tune's pitch class, with its alteration, in the register of the key pressed. A MIDI-in eat
 * takes its register from the note: 48..59 -> -1, 60..71 -> 0, 72..83 -> 1, others clamped to the nearest.
 * band_note_on by outcome (game.c): PERFECT (PART_LEAD, note, 127, BN_ACCENT | BN_NOTEOFF_SELF); GOOD (.., 100,
 * BN_NOTEOFF_SELF); air (music_lane_note(lane, reg) or the F3..B3 letter note in register -1, 60, BN_SOFT |
 * BN_NOTEOFF_SELF); poison (the mushroom's note, 110, BN_SOUR | BN_NOTEOFF_SELF); CHORD adds the eats and notes of
 * lanes +2 and +4 (mod 7, the next register up when it wraps); WIDE adds lanes -1 and +1 (no wrap). Only the
 * pressed lane itself can eat a death cap: the extra lanes of CHORD, WIDE and ECHO's ghosts skip them. */
void game_spawn(uint32_t lane, uint32_t hit, uint32_t species, uint32_t note, uint32_t chain, uint32_t flags);
/* The Big One. hit: the tick of chomp 1. music calls this at the BOSS section's first step with hit = that step +
 * GN_BAR + 2 * GN_PPQ (a one-bar riser while it appears at the top, its three lanes' keys bright and pulsing),
 * lanes = music_chord_lanes of the stage's final chord. It bobs down to the mouth line three times: its lower edge
 * touches the line (boss_y256 = 256) at hit, hit + 2 * GN_PPQ and hit + 4 * GN_PPQ and sits at 128 between; a
 * chomp = the three lanes' keys pressed within GN_BOSS_CHORD_MS of each other inside the GOOD window of a touch
 * (judged when the third key arrives, or when all three are held at the touch): +30 score (x the multipliers) and
 * +30 trip, GE_BOSS_CHOMP; the third chomp eats it (+200 score, GE_BOSS_EATEN, a crash, confetti). With fewer than
 * three bites it lands at hit + 6 * GN_PPQ (GE_BOSS_LANDED; trip costs (3 - bites) x 40 from stage 1 on (index
 * 1), nothing at stage index 0) and leaves a beat later; the stage advances either way. No other spawns fall
 * during the boss. */
void game_boss(uint32_t hit, const uint8_t lanes[3]);
void game_on_section(uint32_t section);       /* music tells the game where the song is; SEC_COUNT = the stage is
                                               * over: the game starts the next (music_stage) or ends the attract */
void game_on_kick(void);                      /* every kick: GE_KICK for the UI's pulse */
void game_midi_key(uint32_t note, uint32_t vel, int on);   /* MIDI in plays like a key: pitch class 0 2 4 5 7 9 11
                                               * are lanes C..B in the note's register, black pitch classes are
                                               * spice; on edges only */
uint32_t game_gnome_lane(void);               /* the lane last pressed (the composer keeps death caps out of it for a
                                               * beat after the press: game_last_press() is the tick of that press) */
uint32_t game_last_press(void);
uint32_t game_state(void);
enum { GC_EATEN, GC_MISSED, GC_POISONED, GC_PERFECT, GC_SPAWNED, GC_AIR, GC_CHAINS, GC_BOSS_CHOMPS, GC_EV_DROPPED,
       GC_SPAWN_DROPPED, GC_CLAMS, GC_COUNT };
uint32_t game_count(uint32_t which);          /* since the run began */
int32_t game_next_hit_ms(uint32_t *lane);     /* ms to the soonest hit ahead in any lane, -1 none (the simulator) */
void game_auto(uint32_t n);                   /* the autoplayer: the next n mushrooms (not death caps) eaten at their
                                               * hit ticks as PERFECT presses in their lane through the same eat path,
                                               * the boss chomped on its touches; 0xFFFFFFFF unlimited; 0 off */
void game_auto_demo(int on);                  /* with the autoplayer: an imperfect player (GOOD and PERFECT mixed,
                                               * about one miss in ten, a stash use every 8 bars): the attract */
int game_auto_on(void);
void game_hiscore(uint32_t hs);               /* the saved high score, for the snapshot and GE_HIGHSCORE */

/* ---- what crosses from the render to the UI */
enum { MS_FREE, MS_FALLING, MS_EATEN, MS_MISSED, MS_FROZEN };
/* mush[i] mirrors game.c's table slot i, so the UI follows a mushroom by slot across frames. A slot is MS_FALLING
 * from its spawn (y256 0 at the spawn row, 256 at the mouth line, linear in time, computed for now +
 * music_ms_ticks(GN_VIS_LEAD_MS), continuing past 256 below the line); MS_EATEN for GN_EATEN_HOLD blocks after an
 * eat, then MS_FREE; MS_MISSED from GN_WIN_GOOD_MS past its hit until y256 >= GN_Y_GONE, then MS_FREE; a death
 * cap that passes frees silently; MS_FROZEN while FREEZE holds (y256 stands). game_spawn with no free slot drops
 * the spawn (GC_SPAWN_DROPPED). */
typedef struct {
    uint8_t lane, species, state;
    uint8_t chain, flags;                /* GN_SPAWN_* */
    int16_t y256;
} gn_mush_snap_t;
typedef struct {
    uint8_t state, section, stage, round;   /* GS_*, SEC_*, 0..7, endless round (0 the first pass) */
    uint8_t jam, practice;               /* practice: the run started past stage 0: no high score */
    uint8_t trip_level;                  /* clamp(trip, 0, 999) / 100 */
    uint8_t gnome_lane;                  /* 0..6 */
    int8_t gnome_reg;                    /* -1..1 */
    uint8_t stash[GN_STASH];             /* AB_* by slot (black key), AB_NONE an empty slot */
    uint8_t ability, ability_bars;       /* the active AB_* and bars left */
    uint8_t boss_on, boss_bites, boss_lanes[3];
    int16_t boss_y256;                   /* 0 at the spawn row .. 256 its lower edge on the mouth line */
    uint8_t lane_next_q8[GN_LANES];      /* (hit - now) / fall x 256 of the nearest MS_FALLING non-AUTO mushroom in
                                          * the lane, 0 when its hit is past (inside the window), 255 none.
                                          * LEDs: bright under 128 (or under a beat, whichever is shorter), mid
                                          * otherwise while one falls, off when none */
    uint8_t lane_poison[GN_LANES];       /* 1: that nearest one is a death cap */
    uint8_t beat_q8, bar_beat;           /* through the beat; the beat in the bar 0..3 */
    uint8_t countin;                     /* a resume: 4 3 2 1 clicks, 0 none */
    uint16_t bpm, bar;
    int16_t trip;                        /* 0..GN_TRIP_MAX */
    uint32_t score, combo, hiscore, auto_left;
    uint16_t st_eaten, st_missed, st_best_combo;   /* this stage's counters (CLEAR shows them) */
    gn_mush_snap_t mush[GN_MAX_MUSH];
} gn_snap_t;
/* game.c keeps `static gn_snap_t gm_snap; static volatile uint32_t gm_seq;`. Writer, the last step of game_tick:
 * gm_seq++; GN_BARRIER(); fill gm_snap; GN_BARRIER(); gm_seq++ (odd while torn). Reader, MAIN LOOP ONLY:
 * for (t = 0; t < 4; t++) { s = gm_seq; GN_BARRIER(); memcpy(dst, &gm_snap, sizeof *dst); GN_BARRIER(); if (s ==
 * gm_seq && !(s & 1u)) return 1; } return 0 (dst keeps the caller's last good copy). The ISR is the only writer
 * and the UI never runs under it, so compiler barriers are all it takes */
int game_snapshot(gn_snap_t *dst);            /* UI: 1 a fresh consistent copy, 0 dst unchanged */

enum { GE_NONE, GE_EAT, GE_MISS, GE_POISON, GE_AGARIC, GE_STASH, GE_USE, GE_REFUSED, GE_CHAIN, GE_BOSS_CHOMP,
       GE_BOSS_EATEN, GE_BOSS_LANDED, GE_SECTION, GE_STAGE, GE_OVER, GE_KICK, GE_NOTE, GE_HIGHSCORE, GE_GHOST,
       GE_WOBBLE, GE_CLAM, GE_COUNT };
/* a, b, c: EAT lane, Q_*, species; MISS / POISON / NOTE / GHOST / AGARIC / CLAM lane; STASH slot 0..2, AB_*; USE AB_*;
 * REFUSED AB_* (a timed ability pressed while another runs: nothing happens but this); CHAIN chain id, length;
 * BOSS_CHOMP bites 1..3; SECTION SEC_*, stage 0..7; STAGE stage 0..7; KICK beat 0..3 (0 = the bar's first);
 * WOBBLE (JAM's refill), OVER, HIGHSCORE, BOSS_EATEN, BOSS_LANDED: none */
typedef struct { uint8_t type, a, b, c; } gn_event_t;
#define GN_EVENTS 64u
/* game.c: static gn_event_t gm_ev[GN_EVENTS]; static volatile uint32_t gm_ev_w, gm_ev_r; free-running counters,
 * slot = n & (GN_EVENTS - 1). push (render only): if (gm_ev_w - gm_ev_r >= GN_EVENTS) { GC_EV_DROPPED++; return; }
 * gm_ev[gm_ev_w & 63] = e; GN_BARRIER(); gm_ev_w++. pop (UI only): if (gm_ev_r == gm_ev_w) return 0; *e =
 * gm_ev[gm_ev_r & 63]; GN_BARRIER(); gm_ev_r++; return 1. Full: the NEWEST is dropped, never overwritten */
void game_event_push(uint32_t type, uint32_t a, uint32_t b, uint32_t c);
int game_event_pop(gn_event_t *e);

/* ---- what crosses from the UI to the render */
enum { CMD_NONE, CMD_START, CMD_PAUSE, CMD_RESUME, CMD_STOP, CMD_ATTRACT };
typedef struct {
    volatile uint32_t req;               /* UI: fill cmd, stage, jam, seed, then GN_BARRIER(), then req++; one
                                          * command a frame, and only while ack == req */
    volatile uint32_t ack;               /* render: ack = req as the first step of game_tick, after taking it */
    uint32_t cmd;                        /* CMD_*. START: a run at stage (0..7), jam 0/1, the RNG seed. ATTRACT: a
                                          * JAM run at stage 0 with game_auto(0xFFFFFFFF) and game_auto_demo(1) for
                                          * one TUNE section, then the title by itself; any key or button ends it
                                          * early. PAUSE: GS_PAUSE, HOLD_PAUSE on, band_all_off, keys not judged.
                                          * RESUME: GN_COUNTIN_BEATS clicks (DR_RIM) at the tempo with the hold kept,
                                          * then HOLD_PAUSE off. STOP: GS_TITLE, music_title */
    uint32_t stage, jam, seed;
    float tone, space;                   /* -1..1, written by the UI whenever a knob moves; the render reads them
                                          * every block and band_set()s BP_LEAD_TONE / BP_SPACE (idempotent) */
    uint32_t voice;                      /* LP_* or 255 the stage's own, read every block */
} gn_cmd_t;
extern gn_cmd_t gn_cmd;                       /* defined in app.c; every field a 32-bit word with one writer */

/* ---- the renderer (gfx.c, UI context): two 120x120 indexed layers doubled to the 240x240 screen */
#define GFX_W 120
#define GFX_H 120
#define GFX_FIELD_TOP 10
#define GFX_MOUTH 96
#define GFX_LANE_W 17
#define GFX_SCREEN 240u
#define GFX_STRIP_ROWS 24u               /* ten strips a frame, two buffers alternating */
#define GFX_BG 0                         /* the background layer: the full post pass (wave, zoom, bend, mirror, melt) */
#define GFX_FIELD 1                      /* the field layer, composited over it: crisp, index 0 transparent, only
                                          * field_wave, the palette's cycling, tint, strobe and invert touch it */
/* palette indices (the ART generator fills GN_PAL to this layout) */
enum { GN_C_BLACK, GN_C_WHITE, GN_C_LGREY, GN_C_GREY, GN_C_DGREY, GN_C_RED, GN_C_ORANGE, GN_C_YELLOW, GN_C_LIME,
       GN_C_GREEN, GN_C_TEAL, GN_C_CYAN, GN_C_SKY, GN_C_BLUE, GN_C_INDIGO, GN_C_VIOLET, GN_C_MAGENTA, GN_C_PINK,
       GN_C_BROWN, GN_C_TAN, GN_C_CREAM, GN_C_DRED, GN_C_DGREEN, GN_C_DBLUE, GN_C_NIGHT, GN_C_FIXED_END = 31 };
#define GN_PAL_SKY 32u                   /* 32 steps, cycled by cycle_a */
#define GN_PAL_GROUND 64u                /* 32 steps, cycled by cycle_b */
#define GN_PAL_GNOME 96u                 /* 96..99 the hat (the UI rewrites them from HAT), then skin, beard, coat... */
#define GN_PAL_MUSH 128u                 /* 5 a species from here in SP_* order: cap, marking, stem, dark, light */
#define GN_PAL_FX 192u                   /* 32 steps of rainbow, cycled by cycle_fx */
#define GN_PAL_PROPS 224u
/* indices 0..31 are never rotated and a tint never blends them past 50 %: the death cap's skull and the agaric's
 * spots are drawn in them so the one sprite a player must read instantly survives every effect */
typedef struct {
    uint8_t w, h;
    const uint8_t *px;                   /* w * h palette indices, 0 transparent */
} gn_sprite_t;
typedef struct {
    uint8_t wave_amp;                    /* background: source pixels, 0..24 */
    uint8_t wave_freq;                   /* turns per 256 screen rows */
    uint16_t wave_phase;                 /* 0..65535 a turn */
    uint8_t zoom;                        /* 0..64: the background's scale swings 1 +- zoom/256 */
    uint16_t zoom_phase;
    int8_t bend;                         /* -32..32: offset grows with the square of the distance to the mouth */
    uint8_t mirror;                      /* 0, 1 left/right, 2 and top/bottom about the mouth line (background) */
    uint8_t melt;                        /* 0..16 source rows (background) */
    uint16_t melt_phase;
    uint8_t field_wave;                  /* the field layer's own gentle wave, 0..3 source pixels */
    uint8_t cycle_a, cycle_b, cycle_fx;  /* palette rotation of the sky, ground and effect ramps, 0..31; the UI steps
                                          * them one entry a frame at most (a 21 ms write tears on a sudden flip) */
    uint8_t invert, strobe;              /* this frame; the UI holds each for two frames */
    uint8_t tint_r, tint_g, tint_b, tint_amt;   /* a colour over everything, amt 0..255 (0..31 capped at half) */
} gfx_post_t;
#define GFX_FLIP_X 1u
#define GFX_DIM 2u                       /* through GN_DIM: a trail, a ghost */
#define GFX_GHOST 4u                     /* every other pixel */
#define GFX_X2 8u                        /* text at double size */
#define GFX_BIG 16u                      /* text: the 6x8 digits */
void gfx_init(void);                     /* idempotent: app_splash may run before app_init's gfx use */
void gfx_palette(const uint8_t *rgb, const uint8_t *dim);   /* GN_PAL (256 x 3) and GN_DIM (256) from gn_art.h */
void gfx_pal_set(uint32_t i, uint32_t r, uint32_t g, uint32_t b);
void gfx_target(int layer);              /* GFX_BG or GFX_FIELD: where the calls below draw (default GFX_FIELD) */
void gfx_clear(uint8_t c);               /* the target layer */
void gfx_pixel(int32_t x, int32_t y, uint8_t c);
void gfx_hline(int32_t x, int32_t y, int32_t w, uint8_t c);
void gfx_vline(int32_t x, int32_t y, int32_t h, uint8_t c);
void gfx_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t c);   /* filled */
void gfx_frame(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t c);  /* outline */
void gfx_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t c);
void gfx_circle(int32_t cx, int32_t cy, int32_t r, uint8_t c);         /* outline */
void gfx_sprite(const gn_sprite_t *s, int32_t x, int32_t y, uint32_t flags);
int32_t gfx_text(int32_t x, int32_t y, const char *s, uint8_t c, uint32_t flags);   /* returns the end x */
int32_t gfx_text_w(const char *s, uint32_t flags);
uint8_t *gfx_buf(int layer);             /* GFX_W * GFX_H, row-major: the background pass writes the BG directly */
int32_t gfx_sin(uint32_t turn_q8);       /* sin of turn/256, in -256..256 */
void gfx_present(const gfx_post_t *post);   /* expand both layers and send the whole screen */

/* ---- the UI (ui.c) */
void ui_init(void);
void ui_frame(void);
void ui_input_only(void);
void ui_say(const char *a, const char *b);
void ui_overload(void);
void ui_pick_stage(uint32_t stage);      /* the title's start pick, 0..7 (the simulator's gn stage) */
void app_splash(void);                   /* the boot screen (APP_SPLASH): the logo through gfx, before app_ui_init */

/* ---- glue (app.c): MIDI out, the settings */
void gn_midi_note(uint32_t ch, uint32_t note, uint32_t vel, int on);   /* a note-on is dropped when
                                               * plat_midi_out_room(TO_USB | TO_BLE) is 0 (nobody listens) or under
                                               * 8 (room kept for the offs owed); a note-off is always offered */
void gn_midi_realtime(uint32_t st);           /* F8 clock (music_tick, 24 a beat, at the SLOW rate too), FA at a start,
                                               * FB when the last hold lifts, FC at a hold or stop */
#define GN_SET_MAGIC 0x4D4F4E47u             /* "GNOM" */
typedef struct {
    uint32_t magic;
    uint32_t hiscore;                    /* runs started at stage 0 only */
    uint8_t furthest;                    /* the furthest stage index reached, 0..7: the title's start pick goes this far */
    uint8_t jam;                         /* JAM, off by default */
    uint8_t hat;                         /* the gnome's hat hue, 0..31 */
    uint8_t ble_off;                     /* Bluetooth switched off (REC held) */
    uint8_t voice;                       /* K3's LEAD patch, LP_* or 255 the stage's own */
    uint8_t flash_off;                   /* no strobe, no invert, half the wave (EDIT on the title) */
    uint8_t rsv[2];
} gn_set_t;
extern gn_set_t gn_set;                       /* the UI edits it, app.c loads and saves it */
extern uint8_t gn_set_dirty;
void gn_settings_save(void);                  /* main loop, on GAME OVER (in the GN_OVER_GAP), on HOME from the
                                               * pause and before PLAY leaves the title: never during play (a flash
                                               * write stops the sound) */
