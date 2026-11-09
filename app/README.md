# GNOMNOM — a rhythm game for the M-VAVE FM-1

A garden gnome eats mushrooms falling down seven lanes. Every mushroom is a note of a famous tune; eating it on
the beat plays the note on a real synth, so a clean run plays the melody. The backing band starts as a straight
rendition and turns into house, acid, breaks and IDM as the stages climb, and the screen gets wavy, colourful and
strange along the way. The 27 keys are the controller (white keys by letter are the lanes, the lowest three black
keys hold the power-ups), the LEDs under the keys cue the lanes, the trip meter is the life bar: let it run out
and you come down.

Built as an app on [fm1-base](https://github.com/charlesvestal/fm1-base), beside
[Pocket Poly](https://github.com/WisestWizard/pocketpoly) and
[DEEP FIELD](https://github.com/WisestWizard/deepfield), which it shared a repository with until
2026-11-08. The design and the module contracts: `docs/DESIGN.md`; the manual: `docs/MANUAL.md`.

## Build, test, play

    ./build.sh --host
    app/build/host/gnomnom_host app/tests/basic.txt app/build/host
    ./build.sh --web            # the browser emulator: app/build/emu/ (serve it; keys qwertyui asdfghjk, P = PLAY)
    ./build.sh                  # the device package: app/build/gnomnom.fwsc
    python3 base/tools/fm1_install.py app/build/gnomnom.fwsc

## Licence

MPL-2.0 (`../LICENSE`). The tunes are public domain (Grieg, Russian folk, Denza, Rimsky-Korsakov, Mussorgsky,
Saint-Saëns, Fučík, Offenbach, Chopin). fm1-base and felucca-bsp keep their own MPL-2.0 files.
