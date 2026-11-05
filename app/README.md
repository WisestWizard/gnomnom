# GNOMNOM — a rhythm game for the M-VAVE FM-1

A garden gnome eats mushrooms falling down seven lanes. Every mushroom is a note of a famous tune; eating it on
the beat plays the note on a real synth, so a clean run plays the melody. The backing band starts as a straight
rendition and turns into house, acid, breaks and IDM as the stages climb, and the screen gets wavy, colourful and
strange along the way. The 27 keys are the controller (white keys by letter are the lanes, the lowest three black
keys hold the power-ups), the LEDs under the keys cue the lanes, the trip meter is the life bar: let it run out
and you come down.

Built as a third app on fm1-base beside Pocket Poly (`../app`) and DEEP FIELD (`../deepfield`). The design and the
module contracts: `docs/DESIGN.md`; the manual: `docs/MANUAL.md`.

## Build, test, play

    ./build.sh gnomnom --host
    gnomnom/build/host/gnomnom_host gnomnom/tests/basic.txt gnomnom/build/host
    ./build.sh gnomnom --web            # the browser emulator: gnomnom/build/emu/ (serve it; keys qwertyui asdfghjk, P = PLAY)
    ./build.sh gnomnom                  # the device package: gnomnom/build/gnomnom.fwsc
    python3 base/tools/fm1_install.py gnomnom/build/gnomnom.fwsc

## Licence

MPL-2.0 (`../LICENSE`). The tunes are public domain (Grieg, Russian folk, Denza, Rimsky-Korsakov, Mussorgsky,
Saint-Saëns, Fučík, Offenbach, Chopin). fm1-base and felucca-bsp keep their own MPL-2.0 files.
