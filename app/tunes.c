/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann */
/* GNOMNOM's nine tunes as scale degrees with alterations (gn_note_t: degree, octave from the middle register, 16ths)
 * and the eight-stage table, const in flash. Transcribed from memory in each piece's own mode at its own rhythm so
 * the mushrooms land on the 16th grid; pieces in 2/4 take two bars per 4/4 bar. Included by app.c after gfx.c. */

#define TN(d, o, l) {(d), (o), (l)}
#define TR(l) {0, 0, (l)}
#define TS(d) GN_SHARP(d)
#define TF(d) GN_FLAT(d)
#define TN_N(a) (sizeof(a) / sizeof((a)[0]))

#define TN_MAJOR {0, 2, 4, 5, 7, 9, 11}
#define TN_MINOR {0, 2, 3, 5, 7, 8, 10}
#define TN_DORIAN {0, 2, 3, 5, 7, 9, 10}

/* In the Hall of the Mountain King: the theme and its answer a fifth up, then both an octave higher. The raised
 * tonic of the answer is the theme's sharpened fourth transposed. */
#define TN_MOUNTAIN_HALF(k) \
    TN(1, k, 2), TN(2, k, 2), TN(3, k, 2), TN(4, k, 2), TN(5, k, 2), TN(3, k, 2), TN(5, k, 2), TR(2), \
    TN(TS(4), k, 2), TN(3, k, 2), TN(TS(4), k, 2), TN(TS(4), k, 6), TR(4), \
    TN(1, k, 2), TN(2, k, 2), TN(3, k, 2), TN(4, k, 2), TN(5, k, 2), TN(3, k, 2), TN(5, k, 2), TN(1, k + 1, 2), \
    TN(7, k, 2), TN(5, k, 2), TN(3, k, 2), TN(5, k, 2), TN(7, k, 6), TR(2), \
    TN(5, k, 2), TN(6, k, 2), TN(7, k, 2), TN(1, k + 1, 2), TN(2, k + 1, 2), TN(7, k, 2), TN(2, k + 1, 2), TR(2), \
    TN(TS(1), k + 1, 2), TN(7, k, 2), TN(TS(1), k + 1, 2), TN(TS(1), k + 1, 6), TR(4), \
    TN(5, k, 2), TN(6, k, 2), TN(7, k, 2), TN(1, k + 1, 2), TN(2, k + 1, 2), TN(7, k, 2), TN(2, k + 1, 2), TN(5, k + 1, 2), \
    TN(4, k + 1, 2), TN(2, k + 1, 2), TN(7, k, 2), TN(2, k + 1, 2), TN(4, k + 1, 6), TR(2)
static const gn_note_t tn_mountain[] = { TN_MOUNTAIN_HALF(0), TN_MOUNTAIN_HALF(1) };
static const uint8_t tn_mountain_ch[16] = {1, 1, 1, 1, 5, 5, 5, 5, 1, 1, 1, 1, 5, 5, 5, 5};

/* Korobeiniki: the A strain, four bars of the B strain with passing notes, the A strain's first half again */
#define TN_KORO_A1 \
    TN(5, 0, 4), TN(2, 0, 2), TN(3, 0, 2), TN(4, 0, 4), TN(3, 0, 2), TN(2, 0, 2), \
    TN(1, 0, 4), TN(1, 0, 2), TN(3, 0, 2), TN(5, 0, 4), TN(4, 0, 2), TN(3, 0, 2), \
    TN(2, 0, 6), TN(3, 0, 2), TN(4, 0, 4), TN(5, 0, 4), \
    TN(3, 0, 4), TN(1, 0, 4), TN(1, 0, 6), TR(2)
static const gn_note_t tn_koro[] = {
    TN_KORO_A1,
    TR(2), TN(4, 0, 4), TN(6, 0, 2), TN(1, 1, 4), TN(7, 0, 2), TN(6, 0, 2),
    TN(5, 0, 6), TN(3, 0, 2), TN(5, 0, 4), TN(4, 0, 2), TN(3, 0, 2),
    TN(2, 0, 4), TN(2, 0, 2), TN(3, 0, 2), TN(4, 0, 4), TN(5, 0, 4),
    TN(3, 0, 4), TN(1, 0, 4), TN(1, 0, 6), TR(2),
    TN(5, 0, 4), TN(4, 0, 2), TN(3, 0, 2), TN(3, 0, 4), TN(2, 0, 2), TN(4, 0, 2),
    TN(4, 0, 4), TN(3, 0, 2), TN(2, 0, 2), TN(2, 0, 4), TN(1, 0, 2), TN(2, 0, 2),
    TN(3, 0, 4), TN(2, 0, 2), TN(1, 0, 2), TN(1, 0, 4), TN(TS(7), -1, 2), TN(1, 0, 2),
    TN(TS(7), -1, 4), TN(1, 0, 2), TN(2, 0, 2), TN(2, 0, 4), TN(3, 0, 2), TN(4, 0, 2),
    TN_KORO_A1
};
static const uint8_t tn_koro_ch[16] = {1, 1, 5 | GN_CH_MAJOR, 1, 4, 1, 5 | GN_CH_MAJOR, 1,
                                       1, 5 | GN_CH_MAJOR, 1, 5 | GN_CH_MAJOR, 1, 1, 5 | GN_CH_MAJOR, 1};

/* Pachelbel's Canon in C: the quarter-note theme with its stepwise eighths over one chord a bar, then the
 * eighth-note variations, the I V vi iii IV I IV V loop twice */
static const gn_note_t tn_canon[] = {
    TN(3, 1, 4), TN(2, 1, 2), TN(1, 1, 2), TN(2, 1, 4), TN(1, 1, 2), TN(7, 0, 2),
    TN(1, 1, 4), TN(7, 0, 2), TN(6, 0, 2), TN(7, 0, 4), TN(6, 0, 2), TN(5, 0, 2),
    TN(6, 0, 4), TN(5, 0, 2), TN(4, 0, 2), TN(5, 0, 4), TN(4, 0, 2), TN(3, 0, 2),
    TN(6, 0, 4), TN(5, 0, 2), TN(4, 0, 2), TN(7, 0, 4), TN(6, 0, 2), TN(5, 0, 2),
    TN(1, 1, 4), TN(7, 0, 2), TN(6, 0, 2), TN(7, 0, 4), TN(6, 0, 2), TN(5, 0, 2),
    TN(6, 0, 4), TN(5, 0, 2), TN(4, 0, 2), TN(5, 0, 4), TN(4, 0, 2), TN(3, 0, 2),
    TN(4, 0, 4), TN(3, 0, 2), TN(2, 0, 2), TN(3, 0, 4), TN(2, 0, 2), TN(1, 0, 2),
    TN(4, 0, 4), TN(3, 0, 2), TN(2, 0, 2), TN(2, 0, 4), TN(1, 0, 2), TN(7, -1, 2),
    TN(3, 1, 2), TN(2, 1, 2), TN(1, 1, 2), TN(7, 0, 2), TN(6, 0, 2), TN(5, 0, 2), TN(6, 0, 2), TN(7, 0, 2),
    TN(1, 1, 2), TN(7, 0, 2), TN(6, 0, 2), TN(5, 0, 2), TN(4, 0, 2), TN(3, 0, 2), TN(4, 0, 2), TN(2, 0, 2),
    TN(1, 1, 2), TN(3, 1, 2), TN(5, 1, 2), TN(4, 1, 2), TN(3, 1, 2), TN(1, 1, 2), TN(3, 1, 2), TN(2, 1, 2),
    TN(1, 1, 2), TN(6, 0, 2), TN(1, 1, 2), TN(5, 0, 2), TN(4, 0, 2), TN(6, 0, 2), TN(5, 0, 2), TN(4, 0, 2),
    TN(3, 0, 2), TN(1, 0, 2), TN(2, 0, 2), TN(7, -1, 2), TN(1, 0, 2), TN(3, 0, 2), TN(5, 0, 2), TN(4, 0, 2),
    TN(3, 0, 2), TN(2, 0, 2), TN(1, 0, 2), TN(2, 0, 2), TN(3, 0, 2), TN(4, 0, 2), TN(5, 0, 2), TN(6, 0, 2),
    TN(6, 0, 2), TN(5, 0, 2), TN(4, 0, 2), TN(3, 0, 2), TN(4, 0, 2), TN(5, 0, 2), TN(6, 0, 2), TN(7, 0, 2),
    TN(1, 1, 2), TN(7, 0, 2), TN(6, 0, 2), TN(5, 0, 2), TN(4, 0, 2), TN(5, 0, 2), TN(7, 0, 2), TN(2, 1, 2)
};
static const uint8_t tn_canon_ch[16] = {1, 5, 6, 3, 4, 1, 4, 5, 1, 5, 6, 3, 4, 1, 4, 5};

/* Brahms' Hungarian Dance No. 5: the theme, its repeated-note answer, the quick descents (two 2/4 bars a bar) */
#define TN_HUNG_THEME \
    TN(5, 0, 2), TN(1, 1, 6), TN(TS(7), 0, 2), TN(1, 1, 2), TN(2, 1, 2), TN(3, 1, 2)
static const gn_note_t tn_hung[] = {
    TN_HUNG_THEME,
    TN(2, 1, 2), TN(1, 1, 2), TN(TS(7), 0, 2), TN(5, 0, 6), TR(4),
    TN_HUNG_THEME,
    TN(2, 1, 2), TN(3, 1, 2), TN(2, 1, 2), TN(1, 1, 6), TR(4),
    TN(3, 1, 2), TN(3, 1, 2), TN(3, 1, 2), TN(2, 1, 2), TN(1, 1, 2), TN(TS(7), 0, 2), TN(1, 1, 2), TN(2, 1, 2),
    TN(3, 1, 2), TN(3, 1, 2), TN(3, 1, 2), TN(2, 1, 2), TN(1, 1, 6), TR(2),
    TN(4, 1, 2), TN(4, 1, 2), TN(4, 1, 2), TN(3, 1, 2), TN(2, 1, 2), TN(1, 1, 2), TN(2, 1, 2), TN(3, 1, 2),
    TN(5, 0, 2), TN(5, 0, 2), TN(5, 0, 2), TN(1, 1, 10),
    TN(5, 1, 2), TN(5, 1, 2), TN(5, 1, 2), TN(4, 1, 2), TN(4, 1, 2), TN(4, 1, 2), TN(3, 1, 2), TN(3, 1, 2),
    TN(2, 1, 2), TN(2, 1, 2), TN(1, 1, 2), TN(TS(7), 0, 2), TN(1, 1, 6), TR(2),
    TN(3, 1, 2), TN(3, 1, 2), TN(3, 1, 2), TN(2, 1, 2), TN(2, 1, 2), TN(2, 1, 2), TN(1, 1, 2), TN(1, 1, 2),
    TN(TS(7), 0, 2), TN(TS(7), 0, 2), TN(1, 1, 2), TN(2, 1, 2), TN(5, 0, 6), TR(2),
    TN_HUNG_THEME,
    TN(2, 1, 2), TN(1, 1, 2), TN(TS(7), 0, 2), TN(5, 0, 6), TR(4),
    TN(5, 1, 2), TN(4, 1, 2), TN(3, 1, 2), TN(2, 1, 2), TN(1, 1, 2), TN(TS(7), 0, 2), TN(1, 1, 2), TN(2, 1, 2),
    TN(3, 1, 2), TN(2, 1, 2), TN(1, 1, 12)
};
static const uint8_t tn_hung_ch[16] = {1, 5 | GN_CH_MAJOR, 1, 1, 3, 1, 4, 5 | GN_CH_MAJOR,
                                       1, 5 | GN_CH_MAJOR, 3, 5 | GN_CH_MAJOR, 1, 5 | GN_CH_MAJOR, 4, 1};

/* Beethoven's Fifth: the motif with its held notes, the rising entries, the climb, then the running eighths */
#define TN_FATE_RUN(a, ao, b, bo) TN(a, ao, 2), TN(a, ao, 2), TN(a, ao, 2), TN(b, bo, 2)
static const gn_note_t tn_fate[] = {
    TR(2), TN(5, 0, 2), TN(5, 0, 2), TN(5, 0, 2), TN(3, 0, 8),
    TR(2), TN(4, 0, 2), TN(4, 0, 2), TN(4, 0, 2), TN(2, 0, 8),
    TN(2, 0, 6), TR(4), TN(5, 0, 2), TN(5, 0, 2), TN(5, 0, 2),
    TN(3, 0, 4), TR(2), TN(6, 0, 2), TN(6, 0, 2), TN(6, 0, 2), TN(4, 0, 4),
    TR(2), TN(7, 0, 2), TN(7, 0, 2), TN(7, 0, 2), TN(5, 0, 2), TR(2), TN(5, 1, 2), TN(5, 1, 2),
    TN(5, 1, 2), TN(4, 1, 8), TR(2), TN(4, 1, 2), TN(4, 1, 2),
    TN(4, 1, 2), TN(2, 1, 10), TR(4),
    TR(2), TN(2, 1, 2), TN(2, 1, 2), TN(2, 1, 2), TN(7, 0, 8),
    TN_FATE_RUN(5, 0, 3, 0), TN_FATE_RUN(4, 0, 2, 0),
    TN_FATE_RUN(5, 0, 3, 0), TN_FATE_RUN(6, 0, 4, 0),
    TN_FATE_RUN(7, 0, 5, 0), TN_FATE_RUN(1, 1, 6, 0),
    TN_FATE_RUN(2, 1, 7, 0), TN_FATE_RUN(3, 1, 1, 1),
    TN_FATE_RUN(5, 1, 3, 1), TN_FATE_RUN(4, 1, 2, 1),
    TN_FATE_RUN(3, 1, 1, 1), TN_FATE_RUN(2, 1, 7, 0),
    TN_FATE_RUN(1, 1, 6, 0), TN_FATE_RUN(7, 0, 5, 0),
    TR(2), TN(5, 0, 2), TN(5, 0, 2), TN(5, 0, 2), TN(3, 0, 8)
};
static const uint8_t tn_fate_ch[16] = {1, 5 | GN_CH_MAJOR, 5 | GN_CH_MAJOR, 1, 3, 5 | GN_CH_MAJOR, 5 | GN_CH_MAJOR,
                                       5 | GN_CH_MAJOR, 1, 4, 6, 7, 1, 1, 6, 1};

/* Mozart's Rondo alla Turca: the sixteenth-note turns and their answers, an octave up the second time, the broken
 * chords, the opening again (two 2/4 bars a bar) */
#define TN_TURCA_A(k) \
    TN(2, k, 1), TN(1, k, 1), TN(TS(7), k - 1, 1), TN(1, k, 1), TN(3, k, 2), TR(2), \
    TN(4, k, 1), TN(3, k, 1), TN(2, k, 1), TN(3, k, 1), TN(5, k, 2), TR(2), \
    TN(6, k, 1), TN(5, k, 1), TN(TS(4), k, 1), TN(5, k, 1), TN(2, k, 1), TN(1, k, 1), TN(TS(7), k - 1, 1), TN(1, k, 1), \
    TN(2, k, 1), TN(1, k, 1), TN(TS(7), k - 1, 1), TN(1, k, 1), TN(3, k, 4), \
    TN(3, k, 1), TN(4, k, 1), TN(3, k, 1), TN(2, k, 1), TN(1, k, 2), TR(2), \
    TN(1, k, 1), TN(TS(7), k - 1, 1), TN(1, k, 1), TN(2, k, 1), TN(3, k, 2), TR(2), \
    TN(3, k, 1), TN(4, k, 1), TN(3, k, 1), TN(2, k, 1), TN(1, k, 1), TN(TS(7), k - 1, 1), TN(1, k, 1), TN(2, k, 1), \
    TN(1, k, 4), TR(4)
static const gn_note_t tn_turca[] = {
    TN_TURCA_A(0),
    TN_TURCA_A(1),
    TN(1, 0, 2), TN(3, 0, 2), TN(5, 0, 2), TN(1, 1, 2), TN(5, 0, 2), TN(3, 0, 2), TN(1, 0, 2), TN(5, -1, 2),
    TN(TS(7), -1, 2), TN(2, 0, 2), TN(5, 0, 2), TN(TS(7), 0, 2), TN(5, 0, 2), TN(2, 0, 2), TN(TS(7), -1, 2), TN(5, -1, 2),
    TN(1, 0, 2), TN(3, 0, 2), TN(5, 0, 2), TN(1, 1, 2), TN(5, 0, 2), TN(3, 0, 2), TN(1, 0, 2), TN(5, -1, 2),
    TN(2, 0, 2), TN(3, 0, 2), TN(2, 0, 2), TN(1, 0, 2), TN(TS(7), -1, 2), TN(1, 0, 2), TN(2, 0, 2), TN(3, 0, 2),
    TN_TURCA_A(0)
};
static const uint8_t tn_turca_ch[16] = {1, 1, 1, 5 | GN_CH_MAJOR, 1, 1, 1, 5 | GN_CH_MAJOR,
                                        1, 5 | GN_CH_MAJOR, 1, 5 | GN_CH_MAJOR, 1, 1, 1, 5 | GN_CH_MAJOR};

/* Flight of the Bumblebee: the chromatic descents and climbs, the buzz on two notes, all sixteenths */
#define TN_BEE_DOWN(k) \
    TN(5, k, 1), TN(TS(4), k, 1), TN(4, k, 1), TN(TS(3), k, 1), TN(3, k, 1), TN(2, k, 1), TN(TF(2), k, 1), TN(1, k, 1)
#define TN_BEE_UP(k) \
    TN(TS(7), k - 1, 1), TN(1, k, 1), TN(TF(2), k, 1), TN(2, k, 1), TN(3, k, 1), TN(TS(3), k, 1), TN(4, k, 1), TN(TS(4), k, 1)
#define TN_BEE_LOWDOWN \
    TN(TS(7), -1, 1), TN(7, -1, 1), TN(TS(6), -1, 1), TN(6, -1, 1), TN(5, -1, 1), TN(TS(4), -1, 1), TN(4, -1, 1), TN(TS(3), -1, 1)
#define TN_BEE_LOWUP \
    TN(3, -1, 1), TN(TS(3), -1, 1), TN(4, -1, 1), TN(TS(4), -1, 1), TN(5, -1, 1), TN(6, -1, 1), TN(TS(6), -1, 1), TN(7, -1, 1)
#define TN_BEE_BUZZ(a, ao, b, bo) \
    TN(a, ao, 1), TN(b, bo, 1), TN(a, ao, 1), TN(b, bo, 1), TN(a, ao, 1), TN(b, bo, 1), TN(a, ao, 1), TN(b, bo, 1)
#define TN_BEE_HALF \
    TN_BEE_DOWN(0), TN_BEE_UP(0), \
    TN_BEE_DOWN(0), TN_BEE_UP(0), \
    TN_BEE_DOWN(0), TN_BEE_LOWDOWN, \
    TN_BEE_LOWUP, TN_BEE_UP(0), \
    TN_BEE_BUZZ(1, 0, TS(7), -1), TN_BEE_UP(0), \
    TN_BEE_BUZZ(5, 0, TS(4), 0), TN(5, 0, 1), TN(6, 0, 1), TN(TS(6), 0, 1), TN(7, 0, 1), TN(TS(7), 0, 1), TN(1, 1, 1), TN(TF(2), 1, 1), TN(2, 1, 1), \
    TN(3, 1, 1), TN(2, 1, 1), TN(TF(2), 1, 1), TN(1, 1, 1), TN(TS(7), 0, 1), TN(7, 0, 1), TN(TS(6), 0, 1), TN(6, 0, 1), TN_BEE_DOWN(0)
static const gn_note_t tn_bee[] = {
    TN_BEE_HALF, TN_BEE_UP(0), TN_BEE_DOWN(0),
    TN_BEE_HALF, TN_BEE_UP(0), TN(5, 0, 1), TN(TS(4), 0, 1), TN(4, 0, 1), TN(TS(3), 0, 1), TN(3, 0, 1), TN(2, 0, 1), TN(1, 0, 2)
};
static const uint8_t tn_bee_ch[16] = {1, 1, 5 | GN_CH_MAJOR, 5 | GN_CH_MAJOR, 1, 1, 4, 5 | GN_CH_MAJOR,
                                      1, 1, 5 | GN_CH_MAJOR, 5 | GN_CH_MAJOR, 1, 4, 5 | GN_CH_MAJOR, 1};

/* Offenbach's Infernal Galop: the galloping strain and the scale runs (two 2/4 bars a bar) */
#define TN_CANCAN_A \
    TN(5, 0, 2), TN(5, 0, 2), TN(1, 1, 2), TN(1, 1, 2), TN(2, 1, 2), TN(1, 1, 2), TN(7, 0, 2), TN(1, 1, 2), \
    TN(6, 0, 2), TN(6, 0, 2), TN(2, 1, 2), TN(2, 1, 2), TN(3, 1, 2), TN(2, 1, 2), TN(1, 1, 2), TN(2, 1, 2), \
    TN(5, 0, 2), TN(5, 0, 2), TN(1, 1, 2), TN(1, 1, 2), TN(2, 1, 2), TN(1, 1, 2), TN(7, 0, 2), TN(1, 1, 2), \
    TN(6, 0, 2), TN(7, 0, 2), TN(1, 1, 2), TN(2, 1, 2), TN(3, 1, 2), TN(2, 1, 2), TN(1, 1, 2), TN(7, 0, 2), \
    TN(3, 1, 2), TN(2, 1, 2), TN(1, 1, 2), TN(7, 0, 2), TN(6, 0, 2), TN(5, 0, 2), TN(4, 0, 2), TN(3, 0, 2), \
    TN(2, 0, 2), TN(3, 0, 2), TN(4, 0, 2), TN(5, 0, 2), TN(6, 0, 2), TN(7, 0, 2), TN(1, 1, 2), TN(2, 1, 2), \
    TN(3, 1, 2), TN(2, 1, 2), TN(1, 1, 2), TN(7, 0, 2), TN(6, 0, 2), TN(5, 0, 2), TN(4, 0, 2), TN(3, 0, 2)
static const gn_note_t tn_cancan[] = {
    TN_CANCAN_A,
    TN(2, 0, 2), TN(2, 0, 2), TN(5, 0, 2), TN(5, 0, 2), TN(1, 0, 8),
    TN_CANCAN_A,
    TN(2, 0, 2), TN(2, 0, 2), TN(5, -1, 2), TN(5, 0, 2), TN(1, 0, 8)
};
static const uint8_t tn_cancan_ch[16] = {1, 5, 1, 5, 1, 5, 1, 1, 1, 5, 1, 5, 1, 5, 5, 1};

/* Chopin's funeral march: the first phrase, for GAME OVER */
static const gn_note_t tn_funeral[] = {
    TN(1, 0, 4), TN(1, 0, 4), TN(1, 0, 3), TN(1, 0, 1), TN(1, 0, 4),
    TN(3, 0, 6), TN(2, 0, 2), TN(2, 0, 6), TN(1, 0, 2),
    TN(1, 0, 4), TN(1, 0, 4), TN(1, 0, 3), TN(1, 0, 1), TN(1, 0, 4),
    TN(2, 0, 6), TN(1, 0, 2), TN(1, 0, 8)
};
static const uint8_t tn_funeral_ch[4] = {1, 6, 1, 1};

const gn_tune_t GN_TUNES[TUNE_COUNT] = {
    {"MOUNTAIN KING", TN_DORIAN, 16, TN_N(tn_mountain), tn_mountain, tn_mountain_ch},
    {"KOROBEINIKI", TN_MINOR, 16, TN_N(tn_koro), tn_koro, tn_koro_ch},
    {"CANON", TN_MAJOR, 16, TN_N(tn_canon), tn_canon, tn_canon_ch},
    {"HUNGARIAN DANCE", TN_MINOR, 16, TN_N(tn_hung), tn_hung, tn_hung_ch},
    {"FATE", TN_MINOR, 16, TN_N(tn_fate), tn_fate, tn_fate_ch},
    {"ALLA TURCA", TN_MINOR, 16, TN_N(tn_turca), tn_turca, tn_turca_ch},
    {"BUMBLEBEE", TN_MINOR, 16, TN_N(tn_bee), tn_bee, tn_bee_ch},
    {"CAN-CAN", TN_MAJOR, 16, TN_N(tn_cancan), tn_cancan, tn_cancan_ch},
    {"FUNERAL MARCH", TN_MINOR, 4, TN_N(tn_funeral), tn_funeral, tn_funeral_ch},
};

/* stash_w: ARP CHORD ECHO SLOW WIDE FREEZE SHIMMER BURP */
const gn_stage_t GN_STAGES[GN_NSTAGES] = {
    {"FOREST FLOOR", TUNE_MOUNTAIN, TN_DORIAN, 60, 88, 100, GENRE_FOLK, KIT_BRUSH, BS_SOFT, SS_PAD, LP_PLUCK, VT_FOREST,
     8, 1, 280, 2, 1, 30, 0, 8, 6, AB_BURP, {1, 4, 1, 4, 4, 1, 1, 2}, 0, 0, 10, 25, 0, 0, 0, 0, 0},
    {"SPORE DRIFT", TUNE_KOROBEINIKI, TN_MINOR, 60, 112, 118, GENRE_HOUSE, KIT_HOUSE, BS_HOUSE, SS_STAB, LP_SAW, VT_DUSK,
     8, 1, 220, 2, 1, 30, 80, 8, 6, AB_WIDE, {1, 4, 2, 3, 4, 1, 1, 2}, 4, 30, 20, 20, 0, 0, 0, 0, 35},
    {"CAP OPENING", TUNE_CANON, TN_MAJOR, 60, 124, 126, GENRE_DEEP, KIT_HOUSE, BS_HOUSE, SS_PAD, LP_SAW, VT_NIGHT,
     8, 1, 200, 4, 0, 45, 80, 8, 6, AB_NONE, {2, 3, 3, 3, 3, 1, 2, 2}, 4, 40, 30, 25, 0, 0, 0, 0, 60},
    {"RED SPOTS", TUNE_HUNGARIAN, TN_MINOR, 60, 128, 134, GENRE_ACID, KIT_TECHNO, BS_ACID, SS_OFF, LP_SAW, VT_RED,
     8, 1, 200, 4, 0, 45, 90, 8, 8, AB_NONE, {3, 2, 3, 2, 2, 2, 2, 2}, 6, 20, 35, 20, 0, 0, 0, 10, 30},
    {"THE WAVY BIT", TUNE_FATE, TN_MINOR, 60, 136, 140, GENRE_RAVE, KIT_TECHNO, BS_HARD, SS_HOOVER, LP_SQUARE, VT_KALEIDO,
     9, 1, 180, 4, 0, 60, 90, 8, 8, AB_NONE, {3, 2, 3, 2, 2, 3, 3, 2}, 8, 50, 30, 30, 10, 10, 0, 20, 50},
    {"KALEIDOSCOPE", TUNE_TURCA, TN_MINOR, 60, 138, 144, GENRE_BREAKS, KIT_BREAKS, BS_HOUSE, SS_STAB, LP_SAW, VT_MIRROR4,
     10, 2, 180, 4, 0, 60, 100, 8, 8, AB_NONE, {4, 1, 3, 1, 1, 3, 3, 3}, 8, 20, 40, 20, 0, 25, 0, 40, 20},
    {"SPIRAL EYES", TUNE_BUMBLEBEE, TN_MINOR, 60, 146, 152, GENRE_IDM, KIT_GLITCH, BS_ACID, SS_STAB, LP_BELL, VT_EYES,
     10, 4, 170, 4, 0, 60, 100, 8, 8, AB_NONE, {4, 1, 4, 1, 1, 3, 4, 3}, 10, 30, 50, 30, 20, 40, 25, 70, 30},
    {"ENLIGHTENMENT", TUNE_CANCAN, TN_MAJOR, 60, 158, 168, GENRE_HARD, KIT_TECHNO, BS_HARD, SS_HOOVER, LP_SQUARE, VT_CHAOS,
     10, 4, 170, 4, 0, 60, 100, 8, 8, AB_NONE, {4, 1, 3, 1, 1, 4, 4, 4}, 12, 60, 50, 45, 40, 30, 35, 60, 70},
};
