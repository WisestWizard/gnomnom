# GNOMNOM — a rhythm game for the M-VAVE FM-1

A garden gnome eats mushrooms falling down seven lanes. Every mushroom is a note of a famous tune; eating it on
the beat plays the note on a real synth, so a clean run plays the melody. The backing band starts as a straight
rendition and turns into house, acid, breaks and IDM as the stages climb, and the screen gets wavy, colourful and
strange along the way. The 27 keys are the controller, the LEDs under them cue the lanes, and the trip meter is
the life bar. The firmware is in [`app/`](app/), and [`app/README.md`](app/README.md) is the whole game.

Built as an app on [fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0) over
[felucca-bsp](https://github.com/hugelton/felucca-bsp) (MPL-2.0).

**Where it stands:** the game, the band, the eight stages and the pixel art are built and pass the simulator
suite, and the pre-flash review's twenty fixes and six design changes are in. **It has never run on a real FM-1**,
so nothing here is confirmed on the device, and the audio load at stage 8 is the open question (`app/docs/DESIGN.md`
has the budget). It grew up beside [Pocket Poly](https://github.com/WisestWizard/pocketpoly) and
[DEEP FIELD](https://github.com/WisestWizard/deepfield) in one repository until 2026-11-08; the history up to that
day is there, and GNOMNOM's own carries on here.

## Layout

| Path | What |
| --- | --- |
| `app/` | the firmware: `fm1.toml` (build settings), `game.c` / `music.c` / `ui.c` / `gfx.c` / `tunes.c` / `engine/`, `tests/` (simulator scripts), `docs/DESIGN.md`, `docs/MANUAL.md`, `site/`, `tools/` |
| `base/` | fm1-base, a git submodule (felucca-bsp inside it as `base/bsp`) |
| `build.sh` | runs `base/tools/build.py app` |

## Build and test

```
./build.sh --host
app/build/host/gnomnom_host app/tests/basic.txt app/build/host
./build.sh --web
./build.sh
```

The simulator runs the whole game on the Mac from a script (commands at the top of `base/host/plat_host.c`), with
WAV and PNG output; the run's seed is fixed, so a script plays out the same every time. `app/tests/screens.txt`
writes the manual's screenshots. `python3 app/tools/levels.py` renders each stage's tune to WAV. Device builds run
the JieLi toolchain in a `linux/amd64` container.

## Prerequisites (macOS, Apple silicon)

- Homebrew's Python 3.14 and its Pillow, built against libraqm: `brew install python@3.14 pillow` (the font and
  art generators need raqm; Xcode's Python and pip's Pillow wheel do not have it).
- A container runtime with a `docker` CLI for the Linux-only JieLi toolchain: `brew install colima docker`, then
  `colima start --vm-type vz --vz-rosetta`.
- The JieLi toolchain: `sh base/tools/get_toolchain.sh` (→ `~/.jieli/toolchain`, 135 MB).
- The JieLi AC79 SDK, three files of which go into every package:
  `git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK` (1 GB).
- For installing over USB from a terminal: `python3 -m pip install --user mido python-rtmidi`.

`JIELI_TOOLCHAIN`, `AC79_SDK` and `PYTHON` override the defaults (`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`,
`/opt/homebrew/bin/python3.14`).

## Install

```
python3 base/tools/fm1_install.py app/build/gnomnom.fwsc
```

At your own risk, and nobody has done it yet. Hold OCT− (or OCT− and OCT+ for five seconds) at power-on for
update mode; the installer can put M-VAVE's own firmware back. If the FM-1 only shows up as the chip's
`WL82 UBOOT1.00` disk, see `base/bsp/tools/fm1_rescue`.

## Licence

MPL-2.0. The tunes are public domain (Grieg, Russian folk, Denza, Rimsky-Korsakov, Mussorgsky, Saint-Saëns,
Fučík, Offenbach, Chopin). fm1-base and felucca-bsp keep their own MPL-2.0 files; JieLi's SDK files used by the
build are Apache-2.0 and are never committed here.
