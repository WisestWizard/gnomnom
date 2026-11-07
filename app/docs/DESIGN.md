# GNOMNOM — design and module contracts

A third firmware on fm1-base beside Pocket Poly (`app/`) and DEEP FIELD (`deepfield/`): a rhythm game where a
garden gnome eats mushrooms falling down seven lanes, every eaten mushroom plays a note of a famous tune on a real
synth, the backing band starts as a straight rendition and turns into house, acid, breaks and IDM as the stages
climb, and the screen gets wavy, colourful and strange at a bunch of points along the way. One player, the 27 keys
are the controller, the LEDs under the keys cue the lanes, the 240×240 screen is the view.

Kyle's brief (2026-11-03): concept 1 from the discussion (a lane game where shooting is playing), a gnome eating
mushrooms, the songs and the visuals get crazier, "really trippy and wavy and colorful at a bunch of points", and
the music "really start as the normal songs and then get crazy and more IDM / house / acid / etc as levels go
on". Defaults he accepted: catch on the beat (not a shooter), seven lanes by white-key letter, a trip meter as the
fail state plus a JAM mode, abilities stashed on the lowest black keys, chunky pixel art, name GNOMNOM, knobs
cosmetic. Revised 2026-11-03 night after four design reviews (hardware, game feel, music, concurrency): the field
is a crisp layer over a warped background, falls stay about a second, the band has three buses, the tunes carry
alterations, four tunes were swapped for ones that fit 4/4 at the tempo, and every shared protocol is spelled out.

This file is the contract between the modules in prose. `gn.h` is the same contract as C; when they disagree,
`gn.h` wins and this file gets fixed. Stage numbers below are the GN_STAGES index 0..7; the screen shows them 1..8.

## 1. The game

### Lanes, keys, notes

- Seven lanes, left to right, named by white-key letter: C D E F G A B. The white keys C4..B4 are the lanes in the
  middle register, C5..G5 the lanes C..G in the high register. A lane's note is a degree of the stage's seven-note
  scale on the stage's root (C4 = 60 unless a remix transposes): lane C = degree 1, D = 2 ... B = 7. The register
  the key was pressed in sets the octave, so the player shapes the melody's register. The E key plays E♭ in a minor
  stage: the key plays the degree, never a wrong note.
- F3 G3 A3 B3 are not lanes: they play their letter's note in the low register (air), so the left hand owns the
  stash keys between them and nothing dangerous. The lowest three black keys (F#3 G#3 A#3) are the stash slots 1..3;
  a lit slot holds an ability and pressing it uses it. The other eight black keys play "spice": the chromatic note
  between the neighbouring lanes' notes (`music_spice_note`), short, on LEAD. They never eat.
- A key press always sounds its lane's note on the LEAD part: a short soft note when nothing was eaten (air), a full
  accented note when something was. Idle play is still an instrument over the backing. Air never breaks the combo.
- A wrong key (a "clam"): a lane key or MIDI lane note that eats nothing while a melody mushroom in another lane is
  inside its GOOD window, or within 110 ms of the hit of one a key just ate in another lane (so a slap of every key
  is caught whichever key lands first). It plays a short sour note instead of the air note, ends the combo, costs a
  third of the stage's `miss_trip`, and shows OOPS over the lane. CHORD's and WIDE's extra lanes are judged first
  (their eat is the press's), FREEZE has no timing to be wrong about, and the air keys and black keys never clam.
  Measured at stage 1: a player who presses every mushroom's key on time and adds a neighbour each time keeps every
  eat but scores 29 %; slapping all seven keys every 125 ms (which used to clear the stage with a full combo) comes
  down in about two seconds; free notes between the mushrooms cost nothing.
- MIDI in (any channel) plays the same way: a note's pitch class picks the lane (C D E F G A B), its octave the
  register (48..59 low, 60..71 middle, 72..83 high), black pitch classes are spice.

### Mushrooms

Mushrooms fall from the top of the field to the gnome's mouth line. Each has a lane, a species, a hit tick (when
it reaches the mouth) and a fall (ticks from spawn to hit: `fall_q` quarter beats, 2 beats at stages 0..3 rising
to 2.5 beats at stages 5..7 so the approach stays between 1.3 and 0.9 s of real time as the tempo climbs). Its
position is linear in time: y256 = 256 × (1 − (hit − now) / fall), 0 at the spawn row, 256 at the mouth line,
beyond it below. After the hit it keeps falling and leaves the field.

Eating: a white-key press in lane L at tick t eats the mushroom in lane L whose hit tick is nearest to t within the
window, if any. GOOD = min(110 ms, 45 % of the gap to the nearer of the previous and next mushroom in the same
lane); PERFECT = min(45 ms, GOOD). One press eats at most one mushroom per lane. A death cap inside the window is
eaten too, but only by the pressed lane itself: the extra lanes of CHORD and WIDE and ECHO's ghosts skip death caps.
With nothing in the window the press is air.

Missing: a mushroom whose hit tick is more than GOOD behind `now` and not eaten is missed: the trip meter drops by
the stage's `miss_trip`, the combo ends, its melody note stays silent (a hole in the tune), the gnome winces, a grey
splat and a soft tom (DR_TOM at 40) mark it, and it falls off.

Species (`SP_*`) and what eating does:

| Species | Look | Eat |
| --- | --- | --- |
| PORCINI | brown cap | the melody note, +trip, score |
| AGARIC (fly agaric) | red cap, white spots (fixed indices, never cycled) | the note, +100 trip (at 1000: +50 score instead), a 4-bar visual burst (wave + colour cycle); in endless also +2 BPM |
| DEATHCAP | flat pale green-grey cap, visible stem, a skull (fixed indices) | poison: −`poison_trip`, a sour detuned cluster, the screen lurches green; never has a melody note; never at stage 0 |
| MOREL | purple honeycomb | the note; ARP into the stash |
| CHANTERELLE | gold, three caps | the note; CHORD into the stash |
| SHIITAKE | brown ringed | the note; ECHO into the stash |
| MEANIE (blue meanie) | blue bruised | the note; SLOW into the stash |
| GIANT (giant porcini) | big brown, 17 px | the note; WIDE into the stash |
| FROST (frost cap) | white crystal | the note; FREEZE into the stash |
| PIXIE (pixie dust) | sparkly pink | the note; SHIMMER into the stash |
| PUFFBALL | grey ball | the note; BURP into the stash |
| BOSS (The Big One) | three lanes wide, 51×24, three bite states | see Boss |

The stash's slots are fixed to their keys: a stashable fills the lowest empty slot, with the stash full it replaces
the oldest pickup in that pickup's own slot, and using one empties only its slot, so an ability never moves to
another key. Porcini are the tune: every
melody note of the stage's tune at its time, thinned by two rules into AUTO notes (`GN_SPAWN_AUTO`, no mushroom,
played by LEAD at its time if the previous melody note was eaten, never missed): the stage's `density` grid (1
every note, 2 only onsets on the 8th grid, 4 only onsets on the beat) and `min_gap_ms` (a note closer than that to
the previous mushroom in any lane, or than 1.5× that to the previous one in its lane). So Mountain King's 8ths at
stage 0 are all mushrooms, and Flight of the Bumblebee at stage 6 is a mushroom a beat with LEAD buzzing the 16ths
between when you eat. The other species replace a porcini (keeping its note) on a schedule: an agaric about every
`agaric_bars`, a stashable about every `stash_bars` (the stage's `first_stash` first: BURP at stage 0, WIDE at
stage 1, so the first two abilities a player meets are the legible ones; then by `stash_w`); death caps
`deathcap_q / 4` a bar from stage 1 on, at a random step, in a lane with no melody spawn within ±1 beat and not the
lane the player pressed in the last beat (skipped, not deferred, when no lane qualifies). Honey fungus chains: six
or more consecutive melody notes each two 16ths or shorter with no rest between (capped at eight, at least four of
them on the stage's density grid, and none starting within a bar of the last chain's end, so a stream of 16ths
chains every few bars, not end to end) share a `chain` id; eating every one pays a bonus and a drum fill.

### Abilities (the stash, `AB_*`, durations `GN_ABILITY_BARS`)

One timed ability at a time: pressing another timed one while one runs is refused (GE_REFUSED, a "WAIT" toast, the
slot keeps it); BURP is instant and always works.

- ARP (from MOREL), 4 bars: every porcini and agaric is eaten automatically at its hit tick (PERFECT, half score)
  and LEAD plays a 16th-note arpeggio of the chord between the melody notes. Lasers run up the lanes. Keys still
  play (air notes add nothing but sound).
- CHORD (CHANTERELLE), 8 bars: a press eats in its lane and in the lanes a third and a fifth up (lane+2, lane+4 mod
  7, the next register up when it wraps), the note sounds as a triad.
- ECHO (SHIITAKE), 8 bars: the tape delay comes up (BP_DLY_DIV 2, an 8th, feedback 0.6, send 0.35, so its
  repeats fall on 8ths); every eat spawns ghost eats in the same lane one and two beats later (a mushroom within
  the window then is eaten, GOOD).
- SLOW (MEANIE), 2 bars of the slowed clock (4 real bars): the transport runs at half rate, LEAD, STAB and BASS
  drop an octave (BP_PITCH −12, bass folded above MIDI 33), the drums lengthen, the filter closes (BP_FILTER −0.6),
  the screen melts and goes purple. The delay stays synced to the un-slowed tempo.
- WIDE (GIANT), 8 bars: a press eats in lane−1, lane, lane+1 (no wrap); LEAD gets unison detune; the gnome is wide.
- FREEZE (FROST), 1 bar of real time at the tempo it froze at: the transport stops, every mushroom hangs where it
  is, the hall freezes and the band stutters its last 8th for the bar so the music never stops; a press in a lane
  eats the lowest mushroom hanging in it (GOOD); then everything resumes with the hit ticks still valid.
- SHIMMER (PIXIE), 8 bars: shimmer reverb up (BP_SHIMMER 0.6), score ×2, the palette cycles in rainbow.
- BURP (PUFFBALL), instant: every mushroom on screen but the death caps is eaten (GOOD, half score; the death caps
  are blown away silently), a big STAB chord and a crash, a shockwave ring, two inverted frames.

### Trip meter, score, combo

- TRIP is 0..1000, starts at 400 and is lifted to at least 400 at every stage's INTRO; shown as a bar; the trip
  level is clamp(trip, 0, 999) / 100 (0..9) and sets the baseline visual and sound intensity within a stage.
  PERFECT +30, GOOD +20, air 0, miss −`miss_trip` (30, 30, 45, 45, 60, 60, 60, 60 by stage), death cap
  −`poison_trip` (0, 80, 80, 90, 90, 100, 100, 100), agaric +100, boss chomp +30, boss landed −(3 − bites) × 40
  from stage 1, a wrong key −`miss_trip` / 3. From stage 2 it decays trip_level / 3 a beat (nothing under 300, 3 a beat above 900), so holding
  the top takes playing. At or below 0 the gnome comes down: GAME OVER ("YOU CAME DOWN"). In JAM the meter runs as
  usual but at 0 it refills to 400 with a WOBBLE (a lurch and a sour cluster) instead of ending.
- The band hears the trip (`BP_TRIP`, slewed over 0.8 s): from 400 down to 100 a low-pass on the band bus closes
  from 20 kHz to 700 Hz, the hats and shaker drop out from about 250 and the stabs and pads from about 160 (each at a
  bar line), until the kick, the bass and the player's own notes are left; from 700 up to 900 the delay and hall
  sends rise by 0.08 and the shimmer by 0.12. Between 400 and 700 the band is as the stage sets it. The lead, the
  bass and the kick never change with it, so the player always hears what they play.
- Score: PERFECT 15, GOOD 10, ARP auto-eat and BURP eats 5, a chain complete +50, a boss chomp 30, boss eaten
  +200. Each is multiplied by the combo multiplier (1 + combo / 8, at most 4), by the stage number (index + 1;
  endless 8), and by 2 under SHIMMER. Combo counts consecutive eats; a miss, a death cap or a wrong key ends it, air
  does not.
  A full stage-7 run stays under 999,999, the HUD's six digits.
- The high score counts only runs started at stage 0; a run started later is PRACTICE (shown in the HUD, no high
  score). High score, furthest stage, hat, JAM, flash and Bluetooth settings are saved at GAME OVER (in the
  GN_OVER_GAP silence before the march), on HOME from the pause, and before PLAY leaves the title: never during
  play (a flash write silences the audio).

### Stages and sections

A run is eight stages then endless. Each stage: INTRO (`intro_bars`: 2 at stages 0..1, 4 from stage 2 where the
groove change is the point; the title card shows for its first 2 bars; the first mushrooms spawn `fall` before
TUNE's first beat), TUNE (one pass of the tune's `bars`), BOSS (4 bars), CLEAR (2 bars: a fanfare and the stage's
counters EATEN / MISSED / BEST COMBO, which stay through the next INTRO's first 2 bars), then the next stage.
Endless: stage 7's genre and kit, the tunes cycling 0..7 again, +6 BPM a tune capped at 180, the mode rotating a
step and the root up a fourth each round, until the player comes down. Tempo within a stage ramps from `bpm0` to
`bpm1` across TUNE (Mountain King accelerates as written), and a `build` 0..1 across TUNE lets the arrangement
grow (stage 0: the filter opens, the brush snare joins, a low pad sustains, the climax doubles the melody an octave
up).

| # (index) | Name | Tune (public domain) | Genre | BPM | Scale (root C) | Density / gap | Visual theme |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 (0) | FOREST FLOOR | In the Hall of the Mountain King (Grieg 1875) | FOLK: pizzicato PLUCK lead, soft root-motion bass, brush kit, a low pad with the build; a quiet guide LEAD carries the tune | 88→100 | dorian (its ♯4 kept by alteration) | 1 / 280 | daylight forest, green and brown, calm |
| 2 (1) | SPORE DRIFT | Korobeiniki (Russian folk) | HOUSE: four-on-the-floor kick, offbeat open hats, clap on 2 and 4, piano stabs, filtered bass; the guide LEAD still on | 112→118 | natural minor | 1 / 220 | dusk, saturating colours, first slow wave |
| 3 (2) | CAP OPENING | Canon in D (Pachelbel, c. 1680), in C | DEEP HOUSE: shuffled hats, sidechain pump on 7th/9th pad chords, bass with filter movement | 124→126 | major | 1 / 250 | night, colour cycling on the sky, spores drifting |
| 4 (3) | RED SPOTS | Hungarian Dance No. 5 (Brahms 1869) | ACID: 303 bass with accents and slides, resonance sweeps, a saw lead through a wet filter | 128→134 | natural minor | 1 / 200 | wave on both axes of the background, red and orange, lanes breathing |
| 5 (4) | THE WAVY BIT | Symphony No. 5, first movement (Beethoven 1808) | RAVE: hard kick, hoover stabs, open hats, risers into the drops | 136→140 | natural minor | 1 / 180 | kaleidoscope on, heavy wave, palette rotating |
| 6 (5) | KALEIDOSCOPE | Rondo alla Turca (Mozart 1783) | BREAKS: broken beats, snare rolls, bit-crushed hats, syncopated bass | 138→144 | natural minor | 2 / 180 | four-way mirror, bending lanes, trails |
| 7 (6) | SPIRAL EYES | Flight of the Bumblebee (Rimsky-Korsakov 1900) | IDM: stutter retriggers, a 7/8 bar every fourth bar, ring modulation on the wet bus, glitch kit; LEAD buzzes the chromatic 16ths between the beat mushrooms | 146→152 | natural minor (chromatic by alteration) | 2 / 170 | eyes in the sky, a strobe on beat 1, zoom pulses |
| 8 (7) | ENLIGHTENMENT | Infernal Galop, the Can-Can (Offenbach 1858) | HARD: 160 BPM gabber kick, everything on, shimmer | 158→168 | major, the mode rotating every 4 bars | 2 / 170 | full chaos in the background, the field still crisp |

The trip is "wavy and colourful at a bunch of points": besides the per-stage baseline, every fly agaric is a 4-bar
burst, every ability has its own look (ARP lasers, SLOW melt, FREEZE crystals and blue, SHIMMER rainbow, BURP
shockwave and invert, WIDE the gnome swollen, CHORD three beams, ECHO ghost gnomes), a combo of 16 puts a rainbow
trail on the gnome, the boss pulses the whole background on the beat, and the LEDs under the keys always tell the
truth even when the background is a kaleidoscope.

### Boss

The Big One takes the BOSS section's 4 bars. Bar 1: a riser while it appears at the top across three lanes (the
stage's final chord: degrees 1 3 5 as lanes) and those lanes' keys light bright and pulse on the beat. Bars 2..3:
it bobs down three times so its lower edge touches the mouth line on beats 2, 4 and 6 of the descent (`hit`, hit +
2 beats, hit + 4 beats) and sits halfway between; a chomp is the three lanes' keys pressed within 90 ms of each
other inside the GOOD window of a touch: +30 score (× the multipliers), +30 trip, a STAB triad and a LEAD chord, a
bite out of the sprite; the third chomp eats it (+200, a crash, confetti). With fewer than three bites it lands at
beat 8 (−(3 − bites) × 40 trip from stage 1) and leaves a beat later. Bar 4: the outcome. No other mushrooms fall
during the boss; the backing keeps going; the HUD shows CHOMP! on the touches.

### Game over, title, attract

- TITLE: the logo, "PRESS PLAY", HIGH SCORE, "STAGE n" start pick (OCT−/OCT+ or SELECT, up to the furthest reached;
  past stage 1 it says PRACTICE), JAM on/off (SEL), FLASH on/off (EDIT: no strobe or invert, half the wave). The
  gnome idles on the forest floor, a music-box Mountain King plays (LEAD BELL only, 70 BPM, reverb up). After 20 s
  without input the attract runs: one TUNE section of stage 0 in JAM played by an imperfect autoplayer (GOOD and
  PERFECT mixed, about one miss in ten, a stash use every 8 bars) with "DEMO" over the HUD; any key or button
  returns to the title.
- PLAY starts a run at the picked stage (the settings save first if dirty). In play, PLAY pauses: the transport
  holds, sounding notes release, the field dims, "PAUSED". PLAY again resumes with a count-in: four RIM clicks at the
  tempo with the HUD counting 4 3 2 1, then the song. HOME in play pauses; HOME from the pause ends the run to the
  title (saving). On GAME OVER: "YOU CAME DOWN", the gnome on his back, the score, NEW HIGH SCORE when it is, the
  stage reached, PLAY again (the same start stage) or HOME.
- REC held a second toggles Bluetooth (saved at the next save, as above), as the template and the other two
  firmwares do.

### Knobs and other buttons

K1 TONE (LEAD cutoff offset, −1..+1), K2 SPACE (delay and reverb send offsets), K3 VOICE (LEAD patch 0..3: PLUCK,
SAW, SQUARE, BELL; the stage picks a default, the knob overrides for the run), K4 HAT (the gnome's hat hue, 0..31,
saved). SELECT on the title: the start stage. ALGO, PRESET, FX, ENV, LFO, GLO, ARP, SEQ, SAVE: nothing (a tap says
the button's name, as the template does; SAVE saves when something is dirty, on the title only). MASTER: volume.

### Lights

During a run a lane's keys (middle and high register) are off when nothing falls in it, mid from the spawn, and
bright for the last half of the fall or the last beat, whichever is shorter (`lane_next_q8` < 128). A death cap
approaching blinks the lane bright/off at 4 Hz during its last beat. The stash's three black keys are bright while
loaded. PLAY is lit green in a run, red while TRIP is under 200, off on the title. During the boss its three lanes
pulse with the beat. On the title the buttons glow and OCT−/OCT+ are mid (they pick the stage).

## 2. Architecture

```
app.c        the fm1-base contract (app_init / app_render / app_frame ...), MIDI in/out glue, the store, the
             host simulator's checks; includes gn.h, gfx.c, tunes.c, music.c, game.c, ui.c (one unity unit, -Os)
game.c       the rules, render context: reads the keys each block, judges eats, the trip meter, score, the
             stash and abilities, the boss, the count-in, the autoplayer; spawns come from music.c; events out
music.c      the transport and the composer, render context: an integer tick clock, tempo, sections; the tune's
             melody as spawns ahead of time; the backing band per genre (drums, bass, stabs, risers); remixes
tunes.c      the nine tunes as scale degrees with alterations and the stage table (const, in flash)
ui.c         main-loop context: states (title, play, pause, over, attract), the HUD, sprites and particles from
             the snapshot and the event ring, the post-effects vector, the lights, the screen reader, the splash
gfx.c        the renderer: two 120×120 indexed layers (background, field) doubled to 240×240 through a palette;
             the background gets per-row affine post-effects (wave, zoom, bend, mirror, melt), the field stays
             crisp; palette cycling, strobe, invert, tint; sprites, text
engine/band.c   the synth (its own -O2 unit): LEAD ×6, BASS, STAB ×3, DRUMS, RISER, three buses, the master chain
engine/fx.c     the master effects, from DEEP FIELD's: ensemble, tape delay (a ratio parameter added), FDN hall
                with shimmer and freeze, limiter (-O2); it includes gn.h, nothing of DEEP FIELD's
tools/gen_art.py   the pixel art as text in the script, written to build/gen/gn_art.h (the sprites and the
                   palette) and build/gen/art.png for review (fm1.toml `gen`)
```

Contexts:

- Render (the audio interrupt, every APP_BLOCK = 256 frames = 5.8 ms): `app_render` runs `midi_in()`, then
  `game_tick(n)` (which calls `music_tick(n)`), then `band_render`. The keys are read in `game_tick` (`plat_keys()`
  is `fm1_in.notes | remote.keys`, a plain load; the analog rows are never driven, so the FX-button jolt of the
  Pocket Poly days does not apply), so a press is judged within one block against the song clock (jitter ±2.9 ms)
  and its note starts in the same block. MIDI out from here is allowed (plat.h). No drawing, flash, `plat_log`,
  waiting, large locals (the interrupt stack is 7.9 KB) or 64-bit multiplies (the silicon bug).
- Main loop (`app_frame`, as often as the loop allows, at most every 16 ms): the UI copies the snapshot, pops the
  events, draws, lights, speaks, reads the knobs and buttons, and sends commands to the game. Shared state crosses
  only through `gn_snap_t` (a seqlock with compiler barriers: gn.h), the event ring (single producer, single
  consumer, free-running counters, the newest dropped when full: gn.h) and `gn_cmd_t` (a request/ack pair of 32-bit
  words plus knob values the render reads every block: gn.h). The device's main loop and the host simulator both
  drive keys only through their own channels; in a play session nothing is injected over USB (Kyle's rule).
- Core 2 (fm1.toml cpu2 = true): the hall of the previous block, as gn.h's `band_render` comment lays out (DEEP
  FIELD's `half_job` pattern, buffers on their own cache lines, the one-core fallback). Nothing else runs there.

### Time

`GN_PPQ` 96 ticks a beat, a 16th = 24, a 4/4 bar = 384, the IDM 7/8 bar = 336 (`music_bar_ticks`). music.c keeps
`uint32_t now_ticks` and an integer sample accumulator: per block `acc += n` (scaled by the rate) and while `acc ≥
samples_per_tick` a tick passes; `samples_per_tick = GN_FS × 60 / (bpm × GN_PPQ)` is recomputed only when the tempo
or the rate changes. So the clock is exact and the simulator reproduces a run. Everything timed (spawns, windows,
sections, abilities) is in ticks; the ms windows are converted at the current tempo and rate each block so they
stay 45 / 110 ms at the ear under SLOW. SLOW halves the rate; FREEZE and pause hold the clock (`music_hold` with a
reason each, so one lifting does not release the other).

### Memory and CPU budget

| | Budget | Plan |
| --- | --- | --- |
| Pool | 344 KB, keep 8 KB spare | band arena 216 KB; gfx: two 14.4 KB layers, two 240×24 RGB565 strips (23 KB), palette 0.5 KB; the platform's canvas 240×32 (15.4 KB: the splash is ours, `APP_SPLASH`); Bluetooth task stacks 24.6 KB; announce 2.4 KB; the deep USB audio ring 4 KB → about 315 KB, 29 KB spare. The art and the tunes are in flash |
| RAM (.bss) | 96 KB, keep 8 KB spare | the snapshot ×2, the event ring, game and music state: under 12 KB of ours over the platform's ~73 KB |
| Render CPU | < 60 % core 1 with the hall on core 2 (DEEP FIELD's effects alone were about 40 % before the hall moved) | a dozen simple voices add 10–15 points; drums, stutter, decimator and ring mod are cheap |
| Frame | a full 240×240 transfer is 21 ms at the measured 44 MHz | the UI draws both layers (~2 ms), expands and sends ten 240×24 strips alternating two buffers so the CPU fills one while the DMA sends the other; the frame is DMA-bound at ~22 ms (~43 fps). fm1-base calls `app_frame` again as soon as 16 ms have passed, so the loop settles at the DMA rate; the watchdog (8 s) and the Bluetooth poll already tolerate 30 ms frames under DEEP FIELD |
| Tearing | the panel refreshes at 60 Hz with no TE line; a 21 ms write is overtaken by its scan about every 80 ms, a seam that drifts down the screen and shows on whole-screen single-frame changes | hold `invert` and `strobe` for two frames, step the palette cycles at most one entry a frame, keep the HUD rows static between frames, and finger-test invert / strobe / cycling on the unit before the visual vocabulary is final; the fallback is sending only strips whose rows changed |
| Latency | the frame plus the panel put the picture 40–60 ms behind the song, the audio about 10 ms | the snapshot's positions are computed for now + `GN_VIS_LEAD_MS` (30 ms); the LEDs and the audio are the primary cue |
| Flash (app slot) | 581 KB | code ~150 KB + Bluetooth 132 KB + fonts FONT_S, FONT_B, FONT_M (APP_BIG_FONT FONT_M) + art ~20 KB + tunes ~4 KB |

## 3. Visuals

### Two layers and the post pass

`gfx` keeps two 120×120 8-bit indexed layers: the background (sky, ground, props, plasma) and the field (lane
guides, mushrooms, gnome, HUD, particles; index 0 transparent). `gfx_present()` expands each screen row (0..239)
into 240 RGB565 pixels through the frame's palette LUT, in ten strips of 24 rows, blitting each strip while filling
the next. Per screen row y the background is sampled through an affine: `src_x = (x − 120) × scale[y] + 120 +
offset[y]` in 8.8 fixed point (scale 128 = the plain doubling), wrapping at the buffer edge, from source row `y / 2
+ drip[y]`; the field is then composited over it from source row y / 2 with only `field_wave` (0..3 px) as an
offset. The post vector (`gfx_post_t`):

- `wave_amp` (≤ 24), `wave_freq`, `wave_phase`: offset[y] = amp × sin(y × freq + phase) (the wavy background)
- `zoom`, `zoom_phase`: scale[y] breathes 1 ± zoom/256
- `bend`: offset[y] += bend × (y − 2 × mouth)² / 4096 (the background curving into the gnome)
- `mirror`: 1 left/right about the centre, 2 also top/bottom about the mouth line (kaleidoscope)
- `melt`, `melt_phase`: drip[y] = melt × sin(y × 3 + phase) (rows smearing down: SLOW)
- `field_wave`: a gentle wobble of the field itself at the top levels, never more than 3 px
- `cycle_a`, `cycle_b`, `cycle_fx`: palette rotation of the sky ramp (32..63), the ground ramp (64..95) and the
  effect ramp (192..223); the UI advances them at most one entry a frame
- `invert`: the output colours inverted (BURP, held two frames)
- `strobe`: the sky ramp white this frame (beat 1 of each bar at VT_EYES and VT_CHAOS, held two frames; other
  kicks only pulse the sky 30 % brighter through the tint); off under FLASH OFF
- `tint_r/g/b`, `tint_amt`: a colour blended over the palette (FREEZE blue, SLOW purple, poison green, the level-0
  grey); indices 0..31 take at most half of it so the death cap's skull and the agaric's spots always read

The field layer is the one the player reads; the background is where the trip lives. All of it is per-row
arithmetic and a 256-entry LUT, under 2 ms a frame.

### Palette layout (the ART generator owns the colours, `gn_art.h`; the indices are gn.h's GN_C_* and GN_PAL_*)

| Indices | Use |
| --- | --- |
| 0 | transparent in sprites; black on screen |
| 1–31 | fixed, never rotated, tinted at most half: white, greys, red, orange, yellow, lime, green, teal, cyan, sky, blue, indigo, violet, magenta, pink, brown, tan, cream, dark red/green/blue, night (GN_C_*) |
| 32–63 | sky ramp, 32 steps around a loop (night blue, violet, magenta, orange, gold, pale yellow, pale green, teal, back), cycled by `cycle_a` |
| 64–95 | ground ramp, 32 steps (soil, moss, grass, yellow-green, ochre, rust, purple-brown, back), cycled by `cycle_b` |
| 96–127 | the gnome: 96–99 hat (rewritten from HAT), 100–102 skin, 103–105 beard, 106–109 coat, 110–111 boots, 112–114 eyes, the rest nose, belt, buckle, cheeks |
| 128–191 | mushrooms: 12 species × 5 (cap, marking, stem, dark, light) in SP_* order from 128 |
| 192–223 | effect ramp, 32 steps of bright rainbow, cycled by `cycle_fx` (lasers, trails, sparkles, the logo, the trip bar) |
| 224–255 | props: trees, grass, the toadstool house, stars, eyes, clouds, confetti |

`GN_PAL[256 * 3]` as RGB888 bytes; `GN_DIM[256]` the index of a darker colour for each (trails and ghosts).
Sprites are `gn_sprite_t { uint8_t w, h; const uint8_t *px; }` with global palette indices, 0 transparent.
The generator keeps the art as text (one character per pixel with a per-sprite legend), writes the header and a
PNG contact sheet (`build/gen/art.png`, 4× scaled) for review. `gn_art.h` is included once, by `ui.c`, and
declares everything `static const`. Its inventory, by name (sizes in buffer pixels, w×h):

| Name | Size | What |
| --- | --- | --- |
| `GN_GNOME[GNOME_FRAMES]`, enum `GNOME_IDLE0, GNOME_IDLE1, GNOME_OPEN, GNOME_BITE, GNOME_DASH, GNOME_TRIP0, GNOME_TRIP1, GNOME_WINCE, GNOME_DOWN, GNOME_FRAMES` | 16×20 (DOWN 20×16) | idle breathing ×2, mouth open (a mushroom coming), the bite, the dash between lanes, spiral-eyed ×2 (trip level ≥ 6), the wince (a miss), passed out (game over) |
| `GN_GNOME_WIDE` | 24×20 | the WIDE ability |
| `GN_MUSH[SP_COUNT]` | 13×13 (GIANT 17×17; the BOSS entry a 13×13 cap) | one per species, distinct silhouette and colour; the death cap flat and wide with a visible stem and a skull in fixed indices |
| `GN_BOSS[3]` | 51×24 | The Big One whole, one bite taken, two bites taken |
| `GN_ICON[AB_COUNT]` | 7×7 | the stash icons (AB_NONE an empty slot outline) |
| `GN_LOGO` | 96×20 | GNOMNOM, in the effect ramp so it cycles, outlined in GN_C_NIGHT |
| `GN_TREE[2]` | 20×30 | two trees for the tree line |
| `GN_HOUSE` | 24×22 | the toadstool house |
| `GN_EYE[2]` | 9×5 | an eye open and closed (stage 7's sky) |
| `GN_STAR`, `GN_SPARK`, `GN_SPLAT` | 3×3, 3×3, 7×4 | a star; a sparkle (effect ramp); the miss splat (grey) |
| `GN_CLOUD` | 24×8 | a cloud (stages 0–2's sky) |
| `GN_SPECIES_CAP[SP_COUNT]` | `uint8_t` | each species' cap colour index (particles) |

### Layout in the 120×120 field layer

| Rows | What |
| --- | --- |
| 0–9 | HUD: SCORE (left, 6 digits in 6×8), the TRIP bar (centre, 36 px, filled from the effect ramp by level, red blinking under 200), the stage digit (right; PRACTICE / DEMO / CPU badges when they apply) |
| 10–16 | in a run, the message line: what a knob or button did, the ability used, WAIT, CHAIN +50, CHOMP!, WOBBLE, on a night strip under the HUD (the PRACTICE / DEMO badge gives way to it); on the title, the pause and GAME OVER a message is a box over the field's middle instead. Nothing is written over the field's lower part in play but PERFECT / GOOD / OOPS over a lane |
| 10–95 | the field: seven lanes 17 px wide from x = 0 (lane C) to x = 118; lane guides as faint dotted lines; the spawn row is 10, the mouth line y = 96. From stage 4 (the plasma themes) a dark track 11 px wide stands behind each lane (every other pixel night on the field layer) so the mushrooms read over the colour; the 6 px between tracks show it whole. AUTO notes are drawn as the outline of their mushroom in light grey, never filled, so they never read as something to eat |
| 96–111 | the gnome (16 wide, 20 tall, feet at 112), standing in the lane of the last press (he dashes 1 frame per lane); the mouth is at y = 96 |
| 112–119 | the ground row: the stash as three 7-px icons at the left, the lane letters C D E F G A B under the lanes at stages 0–2 (and always while the furthest stage saved is 0; light grey with a night shadow, legible on every floor), the combo at the right |

The background layer by stage theme: a sky gradient (ramp A) with stars or eyes, a tree line and the toadstool
house at stages 0–2, clouds drifting, a plasma from `gfx_sin` at stage 4 and up, the ground (ramp B). Mushrooms are
13-px sprites (the GIANT 17); the boss spans three lanes. Particles: up to 32, 2×2 px, from eats (8 a burst in the
species' cap colour), misses (4 grey), BURP (a ring), the boss (confetti).

### Text

Two bitmap fonts in `gfx.c`: 4×6 (3 px glyphs, caps, digits, a few signs) for the HUD and menus, and 6×8 digits
for the score. The logo is a sprite (ART). The title card ("STAGE 2", "SPORE DRIFT") uses 4×6 at double size.

### The splash

`APP_SPLASH`: `app_splash()` (ui.c) draws the logo and the tagline through gfx on the game's layers and presents
once; it calls `gfx_init` and `gfx_palette` itself, since it may run before anything else of ours.

## 4. Sound

### The band (`engine/band.c`)

Parts and voices, all float on the FPU, control rate every 32 samples, audio rate per sample. Oscillators are
PolyBLEP (saw and pulse: two corrections per discontinuity) for LEAD and STAB; BASS may be a naive saw behind its
4-pole filter. LEAD's filter is a trapezoidal (Zavalishin) SVF with the cutoff clamped at 0.45 × fs and resonance
mapped to Q 0.5..12, coefficients every GN_CTL samples.

- LEAD ×6 voices, centred (no pan: a lane-fixed pan would be the per-note pan jump Kyle's notes forbid; width
  comes from the ensemble and WIDE's detune): two oscillators (the second at BP_LEAD_DETUNE × 9 cents, silent at
  0), waveforms per patch: PLUCK triangle + sine sub with a 2 ms attack and a 2 ms decay-to-sustain click
  (pizzicato), SAW, SQUARE (50 % pulse), BELL (a 2-operator FM pair, ratio 3.5, index 2 → 0.3, a 5 Hz 2-cent
  wobble). Cutoff = base (BP_LEAD_CUTOFF 200 Hz..12 kHz, exponential) × 2^(2 × TONE) × an envelope (attack 2 ms,
  decay BP_LEAD_DECAY 60 ms..1.2 s to a 40 % floor); amp ADSR attack 2 ms, decay BP_LEAD_DECAY, sustain 0.5 while
  held, release BP_LEAD_RELEASE 30..900 ms. BN_SOFT: half velocity and a 120 ms decay; BN_ACCENT: full; BN_SOUR: the
  two oscillators at +70 and −40 cents at double level for 200 ms; BN_NOTEOFF_SELF: the voice releases itself when
  its decay ends. Glide BP_LEAD_GLIDE 0..200 ms on legato. Allocation: a free voice, else the oldest; the same
  note again reuses its voice.
- BASS ×1, mono: four cascaded one-pole low-passes with global feedback k 0..3.9 (a Stilson/Smith ladder) and a
  soft clip in the feedback path, cutoff per control tick in log space: cutoff = base × 2^(envmod × env × 6).
  BP_BASS_CUTOFF 60..4000 Hz log, BP_BASS_RESO k 0..3.9, BP_BASS_ENVMOD 0..6 octaves, BP_BASS_DECAY 40..600 ms exp.
  Accent (BN_ACCENT): +4 dB, envmod ×1.4, decay ×0.6, resonance +0.3 for that note. Slide (BN_SLIDE): a linear-in-
  pitch glide from the previous note lasting one 16th at the tempo, the previous gate extended to meet it. Gate:
  ACID notes 55 % of a 16th unless slid. Styles: SOFT triangle, cutoff 0.5, envmod 0, reso 0.1; HOUSE saw, cutoff
  0.35, envmod 0.3, decay 0.3, reso 0.3; ACID saw, cutoff 0.25, envmod 0.75, decay 0.35, reso 0.8; HARD square,
  cutoff 0.5, envmod 0.4, reso 0.5, drive 0.7 (tanh after the filter by BP_BASS_DRIVE).
- STAB ×3, each voice two saws and a pulse an octave down: SS_STAB saws ±7 cents, pulse off, a shared 2-pole filter
  at BP_STAB_CUTOFF with a 50 ms envelope, attack 1 ms, decay BP_STAB_DECAY 100..800 ms; SS_PAD attack 20 ms, decay
  400 ms, sustain 0.4, release 2 s; SS_HOOVER saws ±40 cents plus the pulse with PWM 0.5 Hz between 40 and 60 %
  duty, a pitch envelope from +9 semitones to pitch over 220 ms, the ensemble on. The composer sends the voicing
  (three notes) per style; the band plays what it is sent.
- DRUMS, one voice per DR_*, a hit retriggers without a click. KICK: sine, pitch 140 → 48 Hz exponential over 35
  ms, a 2 ms noise click, amp decay by kit (BRUSH 120 ms at −6 dB, HOUSE 320, TECHNO 420, BREAKS 250, GLITCH 180);
  the HARD gabber kick (KIT_TECHNO with BP_DRUM_LEVEL high and kit drive): pitch 200 → 52 Hz over 70 ms, decay 380
  ms, gain ×8 into a hard clip then a 3 kHz low-pass. SNARE: noise through a band-pass 1.8 kHz Q 0.8 + a 180 Hz tone
  at −6 dB, 120 ms (BRUSH: band-pass 900 Hz, 200 ms, −8 dB, noise only; GLITCH 60 ms). HAT: squares at 205, 304,
  369, 522, 540, 800 Hz, band-pass 7 kHz Q 1.5 then high-pass 6 kHz, 40 ms (OHAT 250); GLITCH decimates hats to 8
  kHz / 8 bits (BP_DRUM_CRUSH on hats, clap, snare). CLAP: three 10 ms noise bursts 11 ms apart then a 110 ms
  band-passed (1.2 kHz) tail. RIM: a 1 kHz resonant ping 50 ms (a woodblock in BRUSH). CRASH: bright noise 1.2 s
  high-passed at 3 kHz. TOM: 110 → 70 Hz sine, 200 ms. SHAKER: high-passed noise 60 ms with a 20 ms attack. The
  kit table has columns kick decay, kick drive, snare length, hat crush, clap level, so kits differ in shape.
- RISER (`band_riser(steps)`): white noise through a band-pass sweeping 300 Hz → 6 kHz over the given 16ths at
  BP_TEMPO, level rising, then a sub drop (a sine 120 → 35 Hz over 300 ms). One at a time.
- Buses (gn.h's `band_render` comment is the law): DRY (KICK, TOM, BASS) bypasses every effect; BAND-WET (STAB and
  the other drums with fixed send trims) → ring mod → decimator; LEAD joins after those (decimator at 40 %, never
  ring-modulated, so the melody is never glitched); the stutter ring (mono, 12288 frames, LEAD excluded) replaces
  DRY + BAND-WET while it repeats; the wet bus → `fx_pre` (ensemble, tape delay with both legs on the grid:
  `dly_ratio` 1.0) → the hall a block behind on core 2 → + DRY → `fx_post` (the limiter). Sidechain on every KICK:
  2 ms to the floor 1 − 0.7 × BP_DUCK, hold 30 ms, release over a 16th at the tempo, on STAB and the wet drums
  only, never LEAD or BASS. Decimator BP_CRUSH 0..1: hold rate 44.1 kHz → 6 kHz (log) and 16 → 6 bits. Ring mod
  BP_RING depth on the wet bus with BP_RING_HZ set by the composer to 1.5 × the bar's chord root. DJ filter
  BP_FILTER. BP_PITCH on LEAD, STAB and BASS (bass folded above MIDI 33), drums unpitched but their decays ×1.5 when
  it is at or below −6 (SLOW's tape feel), smoothed over 100 ms.
- Levels (matched by RMS over the first 200 ms at full velocity, `tools/levels.py`): LEAD −12 dBFS, BASS −12, STAB
  −15, snare and clap −14, hats −20, kick −10 with peaks to −4; a bus soft clip at 0.36 before `fx_pre` as DEEP
  FIELD does; the limiter holds the output under −0.5 dBFS. The default `band_init` state is musical on its own
  (HOUSE kit, SAW lead, HOUSE bass, STAB).
- MIDI out as gn.h's `band_midi_out` comment: channels, GM notes, the 50 ms drum offs, the self-released offs.

### The composer (`music.c`)

Owns the transport (gn.h: the integer clock, tempo and its ramp, rate, holds by reason, sections and bars,
`music_bar_ticks` for the 7/8 bars) and the backing. Every 16th step (and, for swung drums, at the delayed ticks
through a small queue of pending hits: +4 ticks on the odd 16ths for DEEP, +3 for BREAKS, drums only; melody,
spawns and judging stay on the straight grid):

1. Section bookkeeping: INTRO (`intro_bars`) → TUNE (the tune's `bars`) → BOSS (4 bars; `game_boss` at its first
   step with hit = that step + GN_BAR + 2 × GN_PPQ) → CLEAR (2 bars: the tune's first four notes as STAB stabs, a
   crash) → `game_on_section(SEC_COUNT)`; `game_on_section` at every change. The chord is the tune's `chord[bar]`
   during TUNE and BOSS, the tonic in INTRO and CLEAR; its quality from the mode's triad unless GN_CH_MINOR /
   GN_CH_MAJOR forces it. `build` = bars into TUNE / the tune's bars.
2. Drums per genre from bitmask tables (16 steps × DR_COUNT, four variations: bars 1..8 use 0 0 1 0 0 0 1 2, the
   eighth bar a fill (variation 3)), velocities with accents on the beat. Steps are 0..15 of the bar:
   - FOLK/BRUSH: K 0 8 (from build 0.3); brushed S 4 12 (from build 0.3), ghost 11 at 50 in variation 3; X every
     even step; R 2 10 in variation 2; the eighth-bar fill a snare 16th roll on 12–15 rising 60 → 110; from build
     0.75 the roll every fourth bar. A snare roll (not a riser) leads into TUNE.
   - HOUSE: K 0 4 8 12; O 2 6 10 14; H odd steps at 50; C 4 12; var 2 adds K 15 at 80; var 3 a C flam 12 13; var 4
     drops O 14 for a CRASH on the next bar's 0; fill S 16ths on 8–15 rising.
   - DEEP: as HOUSE with H every step (odd steps swung), X 16ths at 40, R 3 7 11 15 at 45, C 4 12 at 70, O only 6
     14.
   - ACID: K 0 4 8 12; H every step with accents on 2 6 10 14; O 10 14; C 4 12; var 4 drops the kick on 12 and
     puts a snare on 13.
   - RAVE (TECHNO kit): K 0 4 8 12 hard; O 2 6 10 14 loud; C + S layered 4 12; CRASH on step 0 of bars 1 and 9; T
     13 14 15 in var 3; fill S 16ths 8–15 then `band_stutter(32, 4)` on 14.
   - BREAKS: K 0 6 10 (var 2: 0 10; var 3: 0 7 10; var 4: 0 6 9); S 4 12; ghost S 9 15 at 45 (var 2: 7 9); H even
     steps, O crushed 7 14; odd steps swung; fill S on 8 10 12 13 14 15 rising.
   - IDM (GLITCH kit): K 0 7 10 (var 2: 0 5 10 13; var 3: 0 3 10; var 4: 0 8 11 14); S displaced 3 12 / 4 11 / 5
     12 / 4 13 by variation; H every step with a seeded 25 % dropout; R clusters 1 2 or 9 10 by seed; on the 7/8
     bars (4n + 3 of TUNE, 14 steps) steps 12 13 get `band_stutter(32, 3)` or `(16, 2)` by seed with the stage's
     `stutter` chance.
   - HARD: K 0 4 8 12 (the gabber kick); O 2 6 10 14; S + C 4 12; CRASH every 4 bars; var 4 doubles K on 0 2 8 10;
     fill K on every 16th of 8–15.
3. Bass over the bar's chord root (two octaves under the lane register): SOFT root 0, octave 4, fifth 8, the
   approach (the degree below the next bar's root) 14, quarters; HOUSE root on 2 6 10 14 short, the octave on 14
   every other bar; DEEP root 0 held 3 steps, fifth 6, root 10 11, octave 14; ACID 16ths: root 0 4 8 12, octave
   accented 3 7 11 15, the ♭7 or fifth on 6 and 14, seeded 20 % slides and 25 % accents, 20 % rests; BREAKS root 0
   6 10 with the kick, fifth 13; IDM as BREAKS with one note a bar displaced by seed; HARD root every 8th, octave on
   15.
4. Stabs: the voicing per style sent as three notes: SS_STAB (HOUSE) 3rd, 5th, root an octave up on steps 2 6 10
   14 (variation 2 adds 7 15); SS_PAD (DEEP) 3rd, 7th, 9th of the mode on the bar's root (no root: the bass has it),
   held steps 0–5 and 8–13, sidechained; SS_HOOVER (RAVE, HARD) root, 5th, octave on step 0 of every bar and 8 11
   in variation 3; HARD adds off-beat 8ths (2 6 10 14) at 80 ms decay; FOLK a low fifth pad (root and fifth two
   octaves down, cutoff 0.2, −12 dB) sustaining each bar from build 0.2; ACID none.
5. Risers: `band_riser(32)` two bars before TUNE (stages 1+) and before BOSS. ARP (`music_arp`): the chord's
   arpeggio on the empty 16ths. The ring frequency: `band_set(BP_RING_HZ, 1.5 × root Hz)` each bar.
6. Spawns for `now + fall`: the melody cursor runs `fall` ticks ahead of the backing on the same tune data; each
   melody note becomes `game_spawn(lane = deg − 1, hit, species, note, chain, flags)` with note = root + 12 × oct +
   mode[deg − 1] + alteration (the stage's mode), species by the schedule (section 1), AUTO flags by the density
   grid and `min_gap_ms`, chain ids by the six-note rule; the guide (stages with `guide`) plays every melody note
   at its hit on LEAD at 45 with BN_SOFT whether or not it is eaten, so the song is always heard at stages 0..1
   and the eats are the loud doubling. Death caps by the stage's rate in lanes free within ±1 beat and not the
   gnome's lane within a beat of its last press. Spawns only in TUNE (and the INTRO's last `fall`). On a 7/8 bar
   steps 14 and 15 are dropped from the melody too.
7. Endless (round ≥ 1): GENRE_HARD and KIT_GLITCH whatever the row; +6 BPM a round capped at 180; the mode rotated
   a step a round and the root up a fourth; `music_bump` only here.

The title's music box (`music_title`), the GAME OVER march after GN_OVER_GAP blocks (`music_over`), and
`music_silence` are the composer's too. Everything is deterministic from the seed; every step is bounded work.

### Tunes (`tunes.c`)

Each tune: name, mode (7 semitone offsets), bars, `gn_note_t { deg (degree 1..7 with GN_SHARP / GN_FLAT bits, 0 a
rest); oct −1..2; dur in 16ths }`, and `chord[bars]` (root degree with GN_CH_MINOR / GN_CH_MAJOR). Transcribed
diatonically in the tune's own mode with alterations where the tune needs them (Mountain King's ♯4, Korobeiniki's
raised 7th on the V chord, Bumblebee's chromatic runs); only ornaments are snapped. Recognisable at tempo is the
bar. Pieces written in 2/4 take two bars per 4/4 bar. The nine: TUNE_MOUNTAIN (Grieg, dorian, 16 bars: the
theme's second bar ♯4 2 ♯4 | 4 ♭2 4, the answer a fifth up, both again an octave higher), TUNE_KOROBEINIKI
(natural minor, 16: the A strain's two halves twice), TUNE_CANON (Pachelbel, major, 16: the I V vi iii IV I IV V
loop twice; the first violin's three entries in quarters, the same three in eighths, then the eighth-note
variations), TUNE_HUNGARIAN (Brahms, natural minor, 16), TUNE_FATE (Beethoven 5, natural minor, 16: the motif, its
fermatas, the running 8ths), TUNE_TURCA (Mozart, natural minor, 16: the turns climb to the octave, B A G♯ A twice
high before the high C), TUNE_BUMBLEBEE (natural minor with alterations, 16), TUNE_CANCAN (Offenbach, major, 16:
the Can-Can's own strain, C | D F E D | G G | G A E F | D D | D F E D | C C' B A | G F E D and again to C G D E C,
then the galloping strain), TUNE_FUNERAL (Chopin, natural minor, 4, GAME OVER).

The stage table `GN_STAGES[8]` (`gn_stage_t`): the table in section 1 plus fall_q 8 8 8 8 9 10 10 10; density 1 1
1 1 1 2 2 2; min_gap_ms 280 220 250 200 180 180 170 170 (1.5× over TUNE's first two bars and 1.25× over the next
two: an on-ramp after each INTRO); intro_bars 2 2 4 4 4 4 4 4; guide 1 1 0 0 0 0 0 0;
miss_trip and poison_trip as section 1; agaric_bars 8; stash_bars 6 6 6 8 8 8 8 8; first_stash BURP WIDE then
AB_NONE; stash_w by stage (early stages favour SLOW, WIDE, CHORD; late stages ARP, ECHO, SHIMMER, FREEZE, BURP;
every weight nonzero); deathcap_q 0 4 4 6 8 8 8 12; ens 0 30 40 20 50 20 30 60; dly_send 10 20 30 35 30 40 50 50;
rev_send 25 20 25 20 30 20 30 45; shimmer 0 0 0 0 10 0 20 40; crush 0 0 0 0 10 25 40 30; ring 0 0 0 0 0 0 25 35;
stutter 0 0 0 10 20 40 70 60; duck 0 35 60 30 50 20 30 70.

Measured under the autoplayer (mushrooms to press a second over TUNE; death caps a second after the slash; the
busiest two seconds in brackets), stage 1 to 8: 2.25 / 0 (3.5), 2.04 / 0.39 (3.0), 2.06 / 0.46 (2.5), 2.36 / 0.68
(4.5), 2.37 / 1.04 (4.0), 3.16 / 1.07 (5.0), 4.02 / 1.12 (5.5), 3.76 / 1.84 (6.0). The tunes set the ceiling: the
first five play every note they have (the Canon's eighths alternate with AUTO notes), so their step is the tempo,
the guide leaving at stage 3, the death caps and the miss cost; the last three are where the presses climb, and the
finale has the busiest passage, the most death caps and the fastest tempo.

## 5. Data crossing contexts (gn.h is the spec; this is the summary)

- `gn_snap_t` by seqlock (writer: seq odd → fields → seq even, with compiler barriers; reader: up to four tries,
  else keep the last copy). Positions are for now + 30 ms. mush[i] mirrors the game's slot i; MS_EATEN holds two
  frames; MS_MISSED until y256 ≥ 320; AUTO notes hold a slot drawn as a ghost.
- The event ring: 64 entries, free-running counters, one producer (render), one consumer (UI), the newest dropped
  when full. Payloads as gn.h's `gn_event_t` comment.
- `gn_cmd_t`: req/ack words for START (stage, jam, seed), PAUSE, RESUME (count-in), STOP, ATTRACT; tone, space and
  voice read every block.

## 6. Files, conventions, verification

- SPDX MPL-2.0 header and `Copyright (C) 2026 Kyle McCann` on every file, as the other two apps. C99, no libc
  beyond what the unity build already brings, float is fine in every unit, `fastmath.h` for sin/exp (no libm).
  Comments only for a non-obvious why; no docstrings. Big zeroed buffers `PLAT_POOL`. Const tables are flash. No
  malloc, no 64-bit integers.
- `fm1.toml`: name "gnomnom", id_prefix "FM-1_15", include ["engine"], o2 the engine units, cpu2 true,
  cpu2_fallback true, ble true, ble_central true, usb_audio_in true, gen ["tools/gen_art.py {gen}/gn_art.h
  {gen}/art.png"]. `app.h`: APP_SPLASH, APP_CANVAS_PX 240×32, FM1B_UAC_DEEP 1, APP_BIG_FONT FONT_M.
- Host: `./build.sh gnomnom --host`, then `gnomnom/build/host/gnomnom_host gnomnom/tests/basic.txt
  gnomnom/build/host`. Deterministic (the run's seed is fixed in the simulator). `app_expect` names: state,
  section, stage (0..7), round, score, trip, triplevel, combo, hiscore, jam, practice, bpm, bar, gnomelane,
  ability, abilitybars, bosson, bossbites, auto, countin, stash0..2, mush, eaten, missed, poisoned, perfect,
  spawned, air, chains, chomps, clams, nexthit, nextlane, voices, peakN (part N, PART_COUNT the drums), peakout,
  furthest, hat, flash, dirty. Host commands (prefix `gn`): `gn auto N`, `gn autooff`, `gn demo 0|1`, `gn stage N`
  (1..8 as printed: the title's pick, past the furthest reached), `gn seed N`.
- Web: `./build.sh gnomnom --web`, served by `.claude/launch.json` "gnomnom-emu" on port 8766; the page's
  keyboard (qwertyui / asdfghjk white keys, digits black keys, P = PLAY, Escape = HOME, Z/X = OCT) plays it.
- Device: `./build.sh gnomnom` → `gnomnom/build/gnomnom.fwsc`. Flash only on Kyle's "Flash it".
- `tests/basic.txt`: boot → title; PLAY; the intro; `gn auto` and the score; a miss; pause and the count-in; JAM
  wobbles instead of ending; a run ends → OVER, settings saved, `reboot`, the high score survives; MIDI out of
  LEAD notes; peak_db_max −0.3 (the limiter holds); knobs. `tests/screens.txt` makes the manual's screenshots.
  `tools/levels.py` renders each stage under the autoplayer and prints peak and RMS per part.
