#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Kyle McCann
# GNOMNOM's palette and pixel art, kept here as text (one character a pixel, a legend a group) and written as the C
# header ui.c includes (gn_art.h: GN_PAL, GN_DIM, GN_SPECIES_CAP and the sprites of docs/DESIGN.md section 3, every
# index gn.h's GN_C_* / GN_PAL_* layout) plus a 4x contact sheet for review. Deterministic; Pillow only for the PNG.
# Run by the build (fm1.toml gen) from app/: python3 tools/gen_art.py build/gen/gn_art.h build/gen/art.png
import colorsys, pathlib, sys

SPECIES = ["PORCINI", "AGARIC", "DEATHCAP", "MOREL", "CHANTERELLE", "SHIITAKE", "MEANIE", "GIANT", "FROST", "PIXIE",
           "PUFFBALL", "BOSS"]
ABILITIES = ["NONE", "ARP", "CHORD", "ECHO", "SLOW", "WIDE", "FREEZE", "SHIMMER", "BURP"]
GNOME_FRAMES = ["IDLE0", "IDLE1", "OPEN", "BITE", "DASH", "TRIP0", "TRIP1", "WINCE", "DOWN"]
STAGES = ["FOREST FLOOR", "SPORE DRIFT", "CAP OPENING", "RED SPOTS", "THE WAVY BIT", "KALEIDOSCOPE", "SPIRAL EYES",
          "ENLIGHTENMENT"]
PAL_SKY, PAL_GROUND, PAL_GNOME, PAL_MUSH, PAL_FX, PAL_PROPS = 32, 64, 96, 128, 192, 224

# ---- the palette

FIXED = [  # 0..31: GN_C_* in gn.h's order, then seven HUD shades
    (0, 0, 0), (255, 255, 255), (200, 200, 208), (128, 128, 136), (64, 64, 72), (224, 32, 32), (255, 128, 0),
    (255, 224, 0), (160, 255, 32), (32, 192, 48), (0, 160, 144), (0, 224, 255), (96, 176, 255), (32, 80, 255),
    (72, 32, 192), (144, 48, 224), (224, 32, 224), (255, 128, 192), (144, 80, 32), (208, 160, 96), (255, 240, 208),
    (112, 0, 16), (0, 80, 32), (16, 32, 112), (10, 10, 40),
    (28, 28, 36), (80, 88, 104), (184, 192, 208), (232, 184, 48), (128, 72, 8), (0, 80, 72), (80, 16, 96),
]
GNOME_PAL = [  # 96..127: hat x4 (rewritten from HAT), skin x3, beard x3, coat x4, boots x2, eyes x3, the rest
    (255, 120, 104), (224, 40, 40), (152, 8, 24), (72, 0, 16),
    (255, 212, 180), (236, 170, 136), (184, 112, 88),
    (255, 255, 255), (220, 220, 232), (156, 156, 180),
    (112, 160, 255), (40, 88, 216), (24, 52, 152), (10, 24, 80),
    (64, 56, 64), (20, 16, 24),
    (255, 255, 255), (16, 16, 28), (140, 255, 255),
    (255, 136, 152), (210, 86, 112), (44, 32, 36), (244, 196, 56), (255, 164, 160), (88, 16, 32), (232, 72, 104),
    (255, 255, 236), (12, 8, 20), (220, 228, 244), (30, 22, 44), (150, 220, 255), (200, 160, 120),
]
MUSH_PAL = [  # 128..187: a species' cap, marking, stem, dark, light, in SP_* order
    [(160, 96, 40), (124, 68, 28), (232, 208, 160), (72, 40, 16), (255, 232, 192)],       # porcini
    [(224, 32, 32), (255, 120, 100), (240, 232, 216), (96, 0, 16), (255, 255, 255)],      # agaric
    [(168, 184, 144), (112, 128, 88), (200, 208, 184), (56, 64, 48), (208, 224, 184)],    # death cap
    [(120, 64, 88), (64, 24, 48), (216, 200, 176), (40, 16, 32), (176, 112, 136)],        # morel
    [(240, 176, 32), (200, 128, 16), (232, 200, 96), (112, 64, 0), (255, 224, 128)],      # chanterelle
    [(128, 72, 40), (232, 216, 184), (208, 184, 152), (56, 24, 8), (184, 128, 88)],       # shiitake
    [(48, 96, 224), (96, 40, 144), (200, 208, 232), (16, 32, 96), (120, 168, 255)],       # meanie
    [(176, 104, 48), (136, 76, 32), (240, 216, 168), (80, 40, 16), (255, 240, 200)],      # giant
    [(224, 248, 255), (96, 216, 255), (200, 232, 240), (32, 96, 144), (255, 255, 255)],   # frost
    [(232, 96, 208), (255, 232, 255), (216, 176, 232), (112, 24, 112), (255, 168, 240)],  # pixie
    [(184, 184, 176), (48, 48, 48), (152, 152, 144), (72, 72, 72), (232, 232, 224)],      # puffball
    [(176, 56, 32), (224, 120, 80), (232, 200, 160), (72, 16, 8), (255, 160, 120)],       # boss
]
BOSS_FACE = [(40, 8, 16), (200, 48, 80), (120, 24, 16), (255, 240, 200)]   # 188..191: maw, tongue, brow, teeth
PROPS_PAL = [  # 224..255
    (8, 40, 24), (24, 96, 40), (56, 152, 64), (104, 200, 80),            # pine: outline, dark, mid, light
    (104, 64, 32), (56, 32, 16), (64, 160, 48), (120, 208, 64),          # bark, bark dark, grass, grass light
    (216, 40, 40), (136, 16, 24), (255, 248, 240), (240, 224, 192),      # house red, red dark, white, wall
    (200, 176, 136), (112, 64, 24), (255, 224, 96),                      # wall shade, door, window
    (255, 240, 128), (255, 255, 224),                                    # star, star pale
    (224, 32, 48), (128, 8, 24), (255, 240, 240), (16, 8, 16),           # eye red, eye dark, eye white, pupil
    (255, 255, 255), (208, 216, 236), (160, 176, 208),                   # cloud white, shade, dark
    (255, 64, 64), (255, 160, 0), (255, 240, 0), (64, 224, 64), (0, 224, 255), (64, 96, 255), (200, 64, 255),
    (255, 128, 220),                                                     # confetti
]
# the two scenery ramps as closed loops of (hue in degrees, unwrapped so the hue runs the way the loop should,
# saturation, value) keys spread evenly over 32 steps
SKY_KEYS = [(232, 0.85, 0.30), (272, 0.75, 0.55), (310, 0.80, 0.85), (385, 0.90, 1.00), (405, 0.85, 1.00),
            (418, 0.45, 1.00), (470, 0.45, 0.92), (530, 0.70, 0.70), (592, 0.85, 0.30)]
GROUND_KEYS = [(25, 0.70, 0.28), (95, 0.55, 0.42), (115, 0.70, 0.62), (78, 0.78, 0.76), (42, 0.82, 0.70),
               (16, 0.85, 0.56), (-25, 0.45, 0.36), (25, 0.70, 0.28)]


def hsv_rgb(h, s, v):
    r, g, b = colorsys.hsv_to_rgb((h / 360.0) % 1.0, s, v)
    return (int(round(r * 255)), int(round(g * 255)), int(round(b * 255)))


def ramp(keys, n):
    out = []
    segs = len(keys) - 1
    for i in range(n):
        t = i * segs / n
        k = min(int(t), segs - 1)
        f = t - k
        a, b = keys[k], keys[k + 1]
        out.append(hsv_rgb(a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f))
    return out


def build_palette():
    pal = list(FIXED)
    pal += ramp(SKY_KEYS, 32)
    pal += ramp(GROUND_KEYS, 32)
    pal += GNOME_PAL
    for sp in MUSH_PAL:
        pal += sp
    pal += BOSS_FACE
    pal += [hsv_rgb(i * 360.0 / 32, 1.0, 1.0) for i in range(32)]
    pal += PROPS_PAL
    assert len(pal) == 256, len(pal)
    return pal


def build_dim(pal):
    hsv = [colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0) for r, g, b in pal]
    steady = list(range(1, 32)) + list(range(96, 192)) + list(range(224, 256))   # never rotated by a cycle

    def darker(i, cands, tol):
        h, s, v = hsv[i]
        best, score = None, None
        for j in cands:
            hj, sj, vj = hsv[j]
            if j == i or vj > v * 0.72:
                continue
            if s > 0.2:
                if sj < 0.2:
                    continue
                dh = abs(h - hj)
                dh = min(dh, 1.0 - dh)
                if dh > tol:
                    continue
                sc = dh * 4 + abs(vj - v * 0.5) + abs(sj - s) * 0.3
            else:
                if sj > 0.25:
                    continue
                sc = abs(vj - v * 0.5)
            if score is None or sc < score:
                best, score = j, sc
        return best

    def pick(i, groups):
        for cands in groups:
            for tol in (0.12, 0.2, 0.3):
                j = darker(i, cands, tol)
                if j is not None:
                    return j
        return 0 if hsv[i][2] < 0.2 else 24

    dim = [0] * 256
    for i in range(1, 256):
        if PAL_SKY <= i < PAL_GNOME:                     # a ramp's dim stays in its ramp so it rotates with it
            base = PAL_SKY if i < PAL_GROUND else PAL_GROUND
            groups = [range(base, base + 32), steady]
        elif PAL_FX <= i < PAL_PROPS:
            groups = [steady]
        elif PAL_GNOME <= i < PAL_GNOME + 4:             # the hat's four follow the HAT knob together
            groups = [range(PAL_GNOME, PAL_GNOME + 4), steady]
        elif PAL_MUSH <= i < PAL_MUSH + 60:
            base = PAL_MUSH + (i - PAL_MUSH) // 5 * 5
            groups = [range(base, base + 5), steady]
        elif i < PAL_SKY:                                # a fixed colour dims to a fixed one: still under the tint cap
            groups = [range(1, PAL_SKY)]
        else:
            groups = [steady]
        dim[i] = pick(i, groups)
    return dim


# ---- the art: text, a legend a group

class Sprite:
    def __init__(self, name, rows, legend, w, h):
        if len(rows) != h:
            raise SystemExit(f"gen_art: {name} has {len(rows)} rows, wants {h}")
        px = []
        for y, row in enumerate(rows):
            if len(row) != w:
                raise SystemExit(f"gen_art: {name} row {y} is {len(row)} wide, wants {w}: {row!r}")
            for ch in row:
                if ch == ".":
                    px.append(0)
                elif ch in legend:
                    px.append(legend[ch])
                else:
                    raise SystemExit(f"gen_art: {name} row {y} has {ch!r}, not in its legend")
        self.name, self.w, self.h, self.px = name, w, h, px


GNOME_LEGEND = {
    "H": 96, "h": 97, "d": 98, "D": 99,            # hat light .. dark
    "S": 100, "s": 101, "t": 102,                  # skin
    "W": 103, "w": 104, "v": 105,                  # beard
    "C": 106, "c": 107, "k": 108, "K": 109,        # coat
    "B": 110, "b": 111,                            # boots
    "E": 112, "p": 113, "g": 114,                  # eye white, pupil, glint
    "n": 115, "N": 116, "L": 117, "u": 118, "r": 119, "m": 120, "T": 121, "e": 122,
    "o": 123, "l": 124, "q": 126,                  # ink, motion line, sweat
    "1": 192, "2": 200,                            # the spirals, on the effect ramp
}

GNOME_ART = {
    "IDLE0": [
        "........DD......",
        ".......DhhD.....",
        "......DHhhD.....",
        ".....DHhhhdD....",
        "....DHhhhhhdD...",
        "...DHhhhhhhhdD..",
        "...DDDDDDDDDDD..",
        "...oSEESSSEESo..",
        "...oSEpSnSEpSo..",
        "..oWrSsNnNsSrWo.",
        "..oWWWwmmmwWWWo.",
        "..oWWWWWWWWWWWo.",
        "..oWWWWvWvWWWWo.",
        "..oKWWWvWvWWWKo.",
        "..oKcWWvvvWWcKo.",
        "..oKccwvvvwccKo.",
        "..oKcccLuLcccKo.",
        "..oKkkkkkkkkkKo.",
        "..obBBBo.oBBBbo.",
        "..oooooo.oooooo.",
    ],
    "IDLE1": [
        "................",
        "........DD......",
        ".......DhhD.....",
        "......DHhhD.....",
        ".....DHhhhdD....",
        "....DHhhhhhdD...",
        "...DHhhhhhhhdD..",
        "...DDDDDDDDDDD..",
        "...oSEpSnSEpSo..",
        ".oWWrSsNnNsSrWWo",
        ".oWWWWwmmmwWWWWo",
        ".oWWWWWWWWWWWWWo",
        ".oWWWWWvWvWWWWWo",
        "..oKWWWvWvWWWKo.",
        "..oKcWWvvvWWcKo.",
        "..oKccwvvvwccKo.",
        "..oKcccLuLcccKo.",
        "..oKkkkkkkkkkKo.",
        "..obBBBo.oBBBbo.",
        "..oooooo.oooooo.",
    ],
    "OPEN": [
        "........DD......",
        ".......DhhD.....",
        "......DHhhD.....",
        ".....DHhhhdD....",
        "....DHhhhhhdD...",
        "...DHhhhhhhhdD..",
        "...DDDDDDDDDDD..",
        "...oSEpSSSEpSo..",
        "...oSEESnSEESo..",
        "..oWrSsNnNsSrWo.",
        "..oWWWmmmmmWWWo.",
        "..oWWmmTTTmmWWo.",
        "..oWWWmmmmmWWWo.",
        "..oKWWWvvvWWWKo.",
        "..oKcWWvvvWWcKo.",
        "..oKccwvvvwccKo.",
        "..oKcccLuLcccKo.",
        "..oKkkkkkkkkkKo.",
        "..obBBBo.oBBBbo.",
        "..oooooo.oooooo.",
    ],
    "BITE": [
        "........DD......",
        ".......DhhD.....",
        "......DHhhD.....",
        ".....DHhhhdD....",
        "....DHhhhhhdD...",
        "...DHhhhhhhhdD..",
        "...DDDDDDDDDDD..",
        "...oSppSSSppSo..",
        "...oSSSSnSSSSo..",
        ".orrrSsNnNsSrrro",
        ".oWrrWwmmmwWrrWo",
        ".oWWWWWWWWWWWWWo",
        "..oWWWWvWvWWWWo.",
        "..oKWWWvWvWWWKo.",
        "..oKcWWvvvWWcKo.",
        "..oKccwvvvwccKo.",
        "..oKcccLuLcccKo.",
        "..oKkkkkkkkkkKo.",
        "..obBBBo.oBBBbo.",
        "..oooooo.oooooo.",
    ],
    "DASH": [
        ".........DD.....",
        "........DhhD....",
        ".......DHhhD....",
        "......DHhhhdD...",
        ".....DHhhhhhdD..",
        "....DHhhhhhhhdD.",
        "....DDDDDDDDDDD.",
        "....oSEESSSEESo.",
        "....oSEpSnSEpSo.",
        "...oWrSsNnNsSrWo",
        "l..oWWWwmmmwWWWo",
        "...oWWWWWWWWWWWo",
        "ll.oWWWWvWvWWWWo",
        "...oKWWWvWvWWWKo",
        "l..oKcWWvvvWWcKo",
        "...oKccwvvvwccKo",
        "...oKcccLuLcccKo",
        "...oKkkkkkkkkkKo",
        "..obBBBo.oBBBbo.",
        "..oooooo.oooooo.",
    ],
    "TRIP0": [
        ".........DhhD...",
        ".......DDhhdD...",
        "......DHhhD.....",
        ".....DHhhhdD....",
        "....DHhhhhhdD...",
        "...DHhhhhhhhdD..",
        "...DDDDDDDDDDD..",
        "...oS12SSS12So..",
        "...oS21SnS21So..",
        "..oWrSsNnNsSrWo.",
        "..oWWmwwwwwmWWo.",
        "..oWWWWWWWWWWWo.",
        "..oWWWWvWvWWWWo.",
        "..oKWWWvWvWWWKo.",
        "..oKcWWvvvWWcKo.",
        "..oKccwvvvwccKo.",
        "..oKcccLuLcccKo.",
        "..oKkkkkkkkkkKo.",
        "..obBBBo.oBBBbo.",
        "..oooooo.oooooo.",
    ],
    "TRIP1": [
        "........DDhhD...",
        ".......DhhhdD...",
        "......DHhhD.....",
        ".....DHhhhdD....",
        "....DHhhhhhdD...",
        "...DHhhhhhhhdD..",
        "...DDDDDDDDDDD..",
        "...oS21SSS21So..",
        "...oS12SnS12So..",
        "..oWrSsNnNsSrWo.",
        "..oWWWmmmmmWWWo.",
        "..oWWWmTTTmWWWo.",
        "..oWWWWvTvWWWWo.",
        "..oKWWWvWvWWWKo.",
        "..oKcWWvvvWWcKo.",
        "..oKccwvvvwccKo.",
        "..oKcccLuLcccKo.",
        "..oKkkkkkkkkkKo.",
        "..obBBBo.oBBBbo.",
        "..oooooo.oooooo.",
    ],
    "WINCE": [
        "........DD......",
        ".......DhhD.....",
        "......DHhhD.....",
        ".....DHhhhdD....",
        "....DHhhhhhdD...",
        "...DHhhhhhhhdD..",
        "...DDDDDDDDDDDq.",
        "...oSSSSSSSSSoq.",
        "...oSppSnSppSo..",
        "..oWrSsNnNsSrWo.",
        "..oWWmmmmmmmWWo.",
        "..oWWWWWWWWWWWo.",
        "..oWWWWvWvWWWWo.",
        "..oKWWWvWvWWWKo.",
        "..oKcWWvvvWWcKo.",
        "..oKccwvvvwccKo.",
        "..oKcccLuLcccKo.",
        "..oKkkkkkkkkkKo.",
        "..obBBBo.oBBBbo.",
        "..oooooo.oooooo.",
    ],
    "DOWN": [
        "....................",
        ".....ooooo..oooooooo",
        "....oSSSSSooKKKKoBBo",
        "...opSpSpSpoKcccKBBo",
        "...oSpSnSpSoKcccKbbo",
        "...opSpSpSpoKcucKBBo",
        "...oSSSSSSSoKcccKBBo",
        "....oWWWWWWoKkkkKbbo",
        ".....oWWWWWWooooooo.",
        "......oWWWWWWo......",
        "...DDDoWWWWWWo......",
        ".DDhhhDDooooo.......",
        "DhhhhhhhhhD.........",
        ".DDDDDDDDDD.........",
        "....................",
        "....................",
    ],
}
GNOME_WIDE_ART = [
    "...........DDD..........",
    "..........DhhhD.........",
    ".........DHhhhdD........",
    "........DHhhhhhdD.......",
    ".......DHhhhhhhhdD......",
    "......DHhhhhhhhhhdD.....",
    ".....DHhhhhhhhhhhhdD....",
    "....DDDDDDDDDDDDDDDDD...",
    "....oSSEESSSSSSSEESSo...",
    "....oSSEpSSSnSSSEpSSo...",
    "..oWWrrSsSSNnNSSsSrrWWo.",
    ".oWWWWWWwmmmmmmwWWWWWWo.",
    ".oWWWWWWWWWWWWWWWWWWWWo.",
    ".oWWWWWvWWWvWWWvWWWWWWo.",
    ".oKKWWWvWWWvvWWWvWWWKKo.",
    ".oKcccWWvvvvvvvvWWcccKo.",
    ".oKccccccLLuuLLccccccKo.",
    ".oKkkkkkkkkkkkkkkkkkKo..",
    "..obBBBBo......oBBBBbo..",
    "..ooooooo......ooooooo..",
]


def mush_legend(species):
    base = PAL_MUSH + species * 5
    return {"C": base, "M": base + 1, "S": base + 2, "D": base + 3, "L": base + 4, "W": 1, "N": 24,
            "a": 188, "T": 189, "B": 190, "e": 191}


MUSH_ART = {
    "PORCINI": [
        "....DDDDD....",
        "..DDCCLCCDD..",
        ".DCCLLCCCCCD.",
        ".DCLCCCCCCMD.",
        "DCCCCCCCCCMMD",
        "DCCCCCCCCMMMD",
        "DDDDDDDDDDDDD",
        "..DSSSLSSSD..",
        "..DSSLLSSSD..",
        "..DSSLSSSSD..",
        ".DSSSLSSSSSD.",
        ".DSSSSSSSSSD.",
        ".DDDDDDDDDDD.",
    ],
    "AGARIC": [
        "....DDDDD....",
        "..DDCWWCCDD..",
        ".DCCCWWCCCCD.",
        ".DCWCCCCCWWD.",
        "DCCWCCCCCWWCD",
        "DCCCCCWWCCCCD",
        "DDDDDDDDDDDDD",
        "...DSSSSSD...",
        "..DLLLLLLLD..",
        "...DSSLSSD...",
        "...DSSLSSD...",
        "...DSSSSSD...",
        "...DDDDDDD...",
    ],
    "DEATHCAP": [
        "...DDDDDDD...",
        ".DDCCWWWCCDD.",
        "DCCCWWWWWCCCD",
        "DCCCWNWNWCCCD",
        "DCCCWWWWWCCCD",
        "DCCCCNWNCCCCD",
        "DDDDDDDDDDDDD",
        "....DSSSD....",
        "....DMMMD....",
        "....DSSSD....",
        "....DSLSD....",
        "...DSSSSSD...",
        "...DDDDDDD...",
    ],
    "MOREL": [
        ".....DDD.....",
        "...DDCMCDD...",
        "..DCMCCCMCD..",
        "..DCCCMCCCD..",
        ".DCMCCCCCMCD.",
        ".DCCCMCMCCCD.",
        ".DCMCCCCCMCD.",
        "..DCCMCMCCD..",
        "..DDCCCCCDD..",
        "...DSSSSSD...",
        "...DSSLSSD...",
        "...DSSSSSD...",
        "...DDDDDDD...",
    ],
    "CHANTERELLE": [
        ".....DDD.....",
        "....DCLCD....",
        ".DDDDCCCDDDD.",
        "DCLCDCMCDCLCD",
        "DCCCDCCCDCCCD",
        "DDDDDDDDDDDDD",
        ".DSDDSMSDDSD.",
        ".DSD.DSD.DSD.",
        "..DSDDSDDSD..",
        "...DSSSSSD...",
        "...DSSSSSD...",
        "....DSSSD....",
        "....DDDDD....",
    ],
    "SHIITAKE": [
        "....DDDDD....",
        "..DDCCLCCDD..",
        ".DCCCLCCCCCD.",
        ".DCCCCCCCCCD.",
        "DCCCCCCCCCCCD",
        "DMMMMMMMMMMMD",
        ".DDDDDDDDDDD.",
        "...DSMMMSD...",
        "...DMMMMMD...",
        "....DSSSD....",
        "....DSSSD....",
        "....DSSSD....",
        "....DDDDD....",
    ],
    "MEANIE": [
        "......D......",
        ".....DCD.....",
        "....DCCCD....",
        "...DCCLCCD...",
        "..DCCCCMMCD..",
        "..DCCCCMMMD..",
        ".DCCCCCCMCCD.",
        "DDDDDDDDDDDDD",
        "....DSSSD....",
        "....DSSSD....",
        "....DSLSD....",
        "....DSSSD....",
        "....DDDDD....",
    ],
    "GIANT": [
        "......DDDDD......",
        "....DDCCLLCCDD...",
        "...DCCLLCCCCCCD..",
        "..DCCLCCCCCCCCMD.",
        ".DCCLCCCCCCCCMMMD",
        "DCCCCCCCCCCCCMMMD",
        "DCCCCCCCCCCCMMMMD",
        "DCCCCCCCCCCCCMMMD",
        "DDDDDDDDDDDDDDDDD",
        "...DSSSSLSSSSSD..",
        "...DSSSLLSSSSSD..",
        "...DSSSLSSSSSSD..",
        "..DSSSSLSSSSSSSD.",
        "..DSSSSLSSSSSSSD.",
        "..DSSSSSSSSSSSSD.",
        "..DSSSSSSSSSSSSD.",
        "..DDDDDDDDDDDDDD.",
    ],
    "FROST": [
        "...D..D..D...",
        "..DMDDMDDMD..",
        ".DMMMCCCMMMD.",
        "DCCCCCCCCCCCD",
        "DCLCCCCCCCCCD",
        "DCCCCCCCCCCCD",
        ".DDDDDDDDDDD.",
        "...DSSMSSD...",
        "...DSMSMSD...",
        "...DSSMSSD...",
        "...DSSSSSD...",
        "...DSSSSSD...",
        "...DDDDDDD...",
    ],
    "PIXIE": [
        ".W..DDDDD..W.",
        "WLWDCCLCCDWLW",
        ".WDCCLCCCCDW.",
        ".DCCCCCCCCCD.",
        "DCCCCMCCCMCCD",
        "DMCCCCCCCCCMD",
        ".DDDDDDDDDDD.",
        "...DSSLSSD...",
        "..W.DSSSD.W..",
        ".WLWDSSSDWLW.",
        "..W.DSSSD.W..",
        "...DSSSSSD...",
        "...DDDDDDD...",
    ],
    "PUFFBALL": [
        "....DDDDD....",
        "..DDCCMCCDD..",
        ".DCCCMMMCCCD.",
        ".DCCCCMCCCCD.",
        "DCCLCCCCCCCCD",
        "DCLCCCCCCCCCD",
        "DCCCCCCCCCCCD",
        "DCCCCCCCCCCCD",
        ".DCCCCCCCCCD.",
        ".DCCCCCCCCCD.",
        "..DDCCCCCDD..",
        "...DSSSSSD...",
        "...DDDDDDD...",
    ],
    "BOSS": [
        "....DDDDD....",
        "..DDCCLCCDD..",
        ".DCCLCCCMCCD.",
        ".DCCCCCCCMMD.",
        "DCWWCCCCCWWCD",
        "DCWNCCCCCWNCD",
        "DCCCCaaaCCCCD",
        "DDDDDDDDDDDDD",
        "...DSSSSSD...",
        "...DSSLSSD...",
        "...DSSLSSD...",
        "...DSSSSSD...",
        "...DDDDDDD...",
    ],
}
BOSS_ART = [
    "..................DDDDDDDDDDDDDDD..................",
    "..............DDCCCCCCCLLLLLCCCCCCCDD..............",
    "...........DCCCCCLLLLLCCCCCCCCCCCCCCCCCD...........",
    "........DCCCCLLLCCCCCCCCCCCCMMMCCCCCCCCCCCD........",
    "......DCCCCLLCCCCCCCCCCCCCCMMMMMCCCCCCCCCCCCD......",
    "....DCCCCCCCCBBBBBBBCCCCCCCCCCCBBBBBBBCCCCCCCCD....",
    "..DCCCCCCCCCCDWWWWWDCCCCCCCCCCCDWWWWWDCCCCCCCCCCD..",
    ".DCCCCCCCCCCCDWWNNWDCCCCCCCCCCCDWWNNWDCCCCCCCCCCCD.",
    "DCCCMMCCCCCCCDWWNNWDCCCCCCCCCCCDWWNNWDCCCCCCCMMCCCD",
    "DCCMMMMCCCCCCDDDDDDDCCCCCCCCCCCDDDDDDDCCCCCCMMMMCCD",
    "DCCCCCCCCCDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDCCCCCCCCCD",
    "DCCCCCCCCCDaeeeaeeeaeeeaeeeaeeeaeeeaeeeaDCCCCCCCCCD",
    "DCCCCCCCCCDaaaaaaaaaTTTTTTTTTTTaaaaaaaaaDCCCCCCCCCD",
    "DCCCCCCCCCDaaaaaaaaaaaaaaaaaaaaaaaaaaaaaDCCCCCCCCCD",
    "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD",
    "................DSSSSSSSSSSSSSSSSSD................",
    "................DSSLLSSSSSSSSSSSSSD................",
    "................DSSLLSSSSSSSSSSSSSD................",
    "................DSSLLSSSSSSSSSSSSSD................",
    "................DSSLLSSSSSSSSSSSSSD................",
    "................DSSLLSSSSSSSSSSSSSD................",
    "................DSSSLSSSSSSSSSSSSSD................",
    "................DSSSSSSSSSSSSSSSSSD................",
    "................DDDDDDDDDDDDDDDDDDD................",
]


def bite(rows, cx, cy, r):
    h, w = len(rows), len(rows[0])
    g = [list(row) for row in rows]
    gone = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            d2 = (x - cx) ** 2 + (y - cy) ** 2
            if g[y][x] == ".":
                continue
            if d2 <= r * r:
                gone[y][x], g[y][x] = True, "."
            elif d2 <= (r + 1.6) ** 2 and g[y][x] in "CML":
                g[y][x] = "S"
    for y in range(h):
        for x in range(w):
            if g[y][x] == ".":
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h and gone[ny][nx]:
                    g[y][x] = "D"
                    break
    return ["".join(row) for row in g]


PROPS_LEGEND = {
    "G": 3, "g": 2, "d": 4, "W": 1, "N": 24,
    "R": 5, "O": 6, "Y": 7, "l": 8, "n": 9, "t": 10, "c": 11, "s": 12, "b": 13, "i": 14, "v": 15, "m": 16,
    "p": 17, "B": 18, "T": 19, "C": 20, "r": 21, "k": 22, "u": 23,
    "1": 192, "2": 200,
}
ICON_ART = {
    "NONE": ["G.G.G.G", ".......", "G.....G", ".......", "G.....G", ".......", "G.G.G.G"],
    "ARP": [".......", ".....l.", ".....l.", "...l.l.", "...l.l.", ".l.l.l.", "kkkkkkk"],
    "CHORD": [".......", "..YYY..", ".......", "..YYY..", ".......", "..YYY..", "......."],
    "ECHO": ["..c....", "...c...", ".t..c..", "..t.c..", ".t..c..", "...c...", "..c...."],
    "SLOW": ["..sss..", ".s.W.s.", "s..W..s", "s..WW.s", "s.....s", ".s...s.", "..sss.."],
    "WIDE": [".......", "..O.O..", ".O...O.", "OOOOOOO", ".O...O.", "..O.O..", "......."],
    "FREEZE": ["c..c..c", ".c.c.c.", "..ccc..", "cccWccc", "..ccc..", ".c.c.c.", "c..c..c"],
    "SHIMMER": ["...p...", "...p...", "..ppp..", "pppWppp", "..ppp..", "...p...", "...p..."],
    "BURP": ["Y..Y..Y", ".Y.O.Y.", "..OOO..", "YOOWOOY", "..OOO..", ".Y.O.Y.", "Y..Y..Y"],
}

SCENE_LEGEND = {  # the props' own colours, 224.. (plus the fixed ones the HUD sprites use)
    "o": 224, "g": 225, "G": 226, "L": 227, "b": 228, "B": 229, "e": 230, "E": 231,
    "R": 232, "r": 233, "w": 234, "C": 235, "c": 236, "d": 237, "y": 238,
    "Y": 239, "P": 240, "X": 241, "x": 242, "W": 243, "p": 244, "K": 245, "s": 246, "k": 247,
    "N": 24, "2": 2, "3": 3, "4": 4, "1": 192, "5": 200,
}
TREE_ART = [
    [
        ".........oo.........",
        "........oLGo........",
        "........oLGo........",
        ".......oLGGgo.......",
        ".......oLGGgo.......",
        "......oLLGGggo......",
        "......oLGGGggo......",
        ".....oLLGGGgggo.....",
        ".....oLGGGGgggo.....",
        "....oooGGGGGgooo....",
        "......oLGGGggo......",
        ".....oLLGGGgggo.....",
        ".....oLGGGGGggo.....",
        "....oLLGGGGGgggo....",
        "....oLGGGGGGgggo....",
        "...oLLGGGGGGggggo...",
        "...oLGGGGGGGGgggo...",
        "..oooGGGGGGGGggooo..",
        ".....oLGGGGGgggo....",
        "....oLLGGGGGGgggo...",
        "....oLGGGGGGGGggo...",
        "...oLLGGGGGGGggggo..",
        "..oLLGGGGGGGGGggggo.",
        ".oLLGGGGGGGGGGgggggo",
        "oooooooGGGGGgooooooo",
        ".......oBbbBo.......",
        ".......oBbbBo.......",
        ".......oBbbBo.......",
        "......oBBbbBBo......",
        "......ooooooo.......",
    ],
    [
        "..........o.........",
        ".........oLo........",
        ".........oGo........",
        "........oLGgo.......",
        "........oLGgo.......",
        ".......oLGGgo.......",
        ".......oLGGggo......",
        "......oLLGGggo......",
        "......oLGGGggo......",
        ".....oLLGGGgggo.....",
        ".....oLGGGGgggo.....",
        "....oooLGGGgooo.....",
        ".....oLGGGGGggo.....",
        "....oLLGGGGGgggo....",
        "....oLGGGGGGgggo....",
        "...oLLGGGGGGggggo...",
        "...oLGGGGGGGGgggo...",
        "..oLLGGGGGGGGggggo..",
        "..oLGGGGGGGGGGgggo..",
        ".ooooGGGGGGGGgoooo..",
        "...oLLGGGGGGGgggo...",
        "..oLLGGGGGGGGGgggo..",
        "..oLGGGGGGGGGGggggo.",
        ".oLLGGGGGGGGGGGggggo",
        "oooooooGGGGGgooooooo",
        "........oBbbBo......",
        "........oBbbBo......",
        "........oBbbBo......",
        ".......oBBbbBBo.....",
        ".......ooooooo......",
    ],
]
HOUSE_ART = [
    ".........rrrrrr.........",
    "......rrrRRRRRRrrr......",
    "....rrRRRRwwRRRRRRrr....",
    "...rRRRwwRwwRRRRwwRRr...",
    "..rRRRRwwRRRRRRwwwwRRr..",
    ".rRRRRRRRRRRwwRwwwwRRRr.",
    ".rRwwRRRRRRRwwRRRRRRwwr.",
    "rRRwwRRRRRRRRRRRRRRRwwRr",
    "rRRRRRRRRRRRRRRRRRRRRRRr",
    "rrrrrrrrrrrrrrrrrrrrrrrr",
    "...BCCCCCCCCCCCCCCCCB...",
    "...BCCBBBBCCCCCCCCCCB...",
    "...BCCByyBCCCBBBBCCCB...",
    "...BCCByyBCCCBddBCCCB...",
    "...BCCBBBBCCCBddBCCCB...",
    "...BCCCCCCCCCBddBCCCB...",
    "...BcCCCCCCCCBddBCCcB...",
    "...BcCCCCCCCCBdyBCCcB...",
    "...BccCCCCCCCBddBCccB...",
    "...BccccCCCCCBddBcccB...",
    "...BBBBBBBBBBBddBBBBB...",
    "eEeeEeeeEeeeeeEeeeEeeeEe",
]
EYE_ART = [
    [
        "..xxxxx..",
        ".xXWXXXx.",
        "xXXpppXXx",
        ".xXXXXXx.",
        "..xxxxx..",
    ],
    [
        ".........",
        ".........",
        "xxxxxxxxx",
        ".x..x..x.",
        ".........",
    ],
]
STAR_ART = [".Y.", "YPY", ".Y."]
SPARK_ART = [".1.", "151", ".1."]
SPLAT_ART = [".2.3.2.", "2334332", ".34443.", "..2.2.."]
CLOUD_ART = [
    ".........KKKK...........",
    "......KKKKKKKKK.KKK.....",
    "....KKKKKKKKKKKKKKKKK...",
    "..KKKKKKKKKKKKKKKKKKKKK.",
    ".KKKKKKKKKKKKKKKKKKKKKKK",
    ".KKKKKKKKKKKKKKKKKKKKKKK",
    "..sssssssssssssssssssss.",
    "....kkkkkkkkkkkkkkkkk...",
]

# the logo: four fat rounded glyphs, 12x16, laid out seven times across 96 px, filled from the effect ramp by
# column (so cycle_fx runs a rainbow along the word) and outlined in GN_C_NIGHT
GLYPHS = {
    "G": [
        "...######...",
        ".##########.",
        "###......###",
        "###......###",
        "###.........",
        "###.........",
        "###.........",
        "###...######",
        "###...######",
        "###......###",
        "###......###",
        "###......###",
        "###......###",
        ".###....###.",
        ".##########.",
        "...######...",
    ],
    "N": [
        "###......###",
        "####.....###",
        "#####....###",
        "######...###",
        "###.###..###",
        "###.###..###",
        "###..###.###",
        "###..###.###",
        "###...######",
        "###...######",
        "###....#####",
        "###....#####",
        "###.....####",
        "###.....####",
        "###......###",
        "###......###",
    ],
    "O": [
        "...######...",
        ".##########.",
        ".###....###.",
        "###......###",
        "###......###",
        "###......###",
        "###......###",
        "###......###",
        "###......###",
        "###......###",
        "###......###",
        "###......###",
        "###......###",
        ".###....###.",
        ".##########.",
        "...######...",
    ],
    "M": [
        "###......###",
        "####....####",
        "#####..#####",
        "############",
        "###.####.###",
        "###.####.###",
        "###..##..###",
        "###..##..###",
        "###......###",
        "###......###",
        "###......###",
        "###......###",
        "###......###",
        "###......###",
        "###......###",
        "###......###",
    ],
}


def logo_rows():
    w, h = 96, 20
    g = [["."] * w for _ in range(h)]
    for i, ch in enumerate("GNOMNOM"):
        x0, y0 = 2 + 13 * i, 2
        for y, row in enumerate(GLYPHS[ch]):
            for x, c in enumerate(row):
                if c == "#":
                    g[y0 + y][x0 + x] = "#"
    for y in range(h):
        for x in range(w):
            if g[y][x] != ".":
                continue
            if any(0 <= x + dx < w and 0 <= y + dy < h and g[y + dy][x + dx] == "#"
                   for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
                g[y][x] = "N"
    rows = []
    for y in range(h):
        rows.append("".join(chr(ord("a") + min(31, (x - 2) * 32 // 90)) if c == "#" else c for x, c in enumerate(g[y])))
    return rows


LOGO_LEGEND = {chr(ord("a") + i): PAL_FX + i for i in range(32)}
LOGO_LEGEND["N"] = 24


def build_sprites():
    s = {}
    s["gnome"] = [Sprite(f"GN_GNOME_{n}", GNOME_ART[n], GNOME_LEGEND, 20 if n == "DOWN" else 16,
                         16 if n == "DOWN" else 20) for n in GNOME_FRAMES]
    s["wide"] = Sprite("GN_GNOME_WIDE", GNOME_WIDE_ART, GNOME_LEGEND, 24, 20)
    s["mush"] = [Sprite(f"GN_MUSH_{n}", MUSH_ART[n], mush_legend(i), 17 if n == "GIANT" else 13,
                        17 if n == "GIANT" else 13) for i, n in enumerate(SPECIES)]
    boss = [BOSS_ART, bite(BOSS_ART, 51, 5, 10.5)]
    boss.append(bite(boss[1], -1, 6, 10.5))
    s["boss"] = [Sprite(f"GN_BOSS_{i}", rows, mush_legend(SPECIES.index("BOSS")), 51, 24) for i, rows in enumerate(boss)]
    s["icon"] = [Sprite(f"GN_ICON_{n}", ICON_ART[n], PROPS_LEGEND, 7, 7) for n in ABILITIES]
    s["logo"] = Sprite("GN_LOGO", logo_rows(), LOGO_LEGEND, 96, 20)
    s["tree"] = [Sprite(f"GN_TREE_{i}", rows, SCENE_LEGEND, 20, 30) for i, rows in enumerate(TREE_ART)]
    s["house"] = Sprite("GN_HOUSE", HOUSE_ART, SCENE_LEGEND, 24, 22)
    s["eye"] = [Sprite(f"GN_EYE_{i}", rows, SCENE_LEGEND, 9, 5) for i, rows in enumerate(EYE_ART)]
    s["star"] = Sprite("GN_STAR", STAR_ART, SCENE_LEGEND, 3, 3)
    s["spark"] = Sprite("GN_SPARK", SPARK_ART, SCENE_LEGEND, 3, 3)
    s["splat"] = Sprite("GN_SPLAT", SPLAT_ART, SCENE_LEGEND, 7, 4)
    s["cloud"] = Sprite("GN_CLOUD", CLOUD_ART, SCENE_LEGEND, 24, 8)
    return s


# ---- the header

def c_rows(values, per_row, per_line=32):
    lines = []
    for i in range(0, len(values), per_row):
        row = values[i:i + per_row]
        for j in range(0, len(row), per_line):
            lines.append("    " + ", ".join(f"{v:3d}" for v in row[j:j + per_line]) + ",")
    return "\n".join(lines)


def c_sprite(sp):
    return (f"static const uint8_t {sp.name}_PX[] = {{   /* {sp.w}x{sp.h} */\n{c_rows(sp.px, sp.w)}\n}};\n")


def write_header(path, pal, dim, s):
    out = ["/* SPDX-License-Identifier: MPL-2.0",
           " * Copyright (C) 2026 Kyle McCann */",
           "/* generated by tools/gen_art.py (GNOMNOM's palette and sprites, DESIGN.md section 3): do not edit; needs gn.h first */",
           "#pragma once",
           "",
           "static const uint8_t GN_PAL[256 * 3] = {",
           c_rows([c for rgb in pal for c in rgb], 24),
           "};",
           "static const uint8_t GN_DIM[256] = {",
           c_rows(dim, 16),
           "};",
           "static const uint8_t GN_SPECIES_CAP[SP_COUNT] = {"
           + ", ".join(str(PAL_MUSH + i * 5) for i in range(len(SPECIES))) + "};",
           "",
           "enum { " + ", ".join(f"GNOME_{n}" for n in GNOME_FRAMES) + ", GNOME_FRAMES };"]
    for sp in s["gnome"]:
        out.append(c_sprite(sp))
    out.append("static const gn_sprite_t GN_GNOME[GNOME_FRAMES] = {\n"
               + "".join(f"    {{{sp.w}, {sp.h}, {sp.name}_PX}},\n" for sp in s["gnome"]) + "};")

    def single(sp):
        out.append(c_sprite(sp))
        out.append(f"static const gn_sprite_t {sp.name} = {{{sp.w}, {sp.h}, {sp.name}_PX}};")

    def array(name, size, sps):
        for sp in sps:
            out.append(c_sprite(sp))
        out.append(f"static const gn_sprite_t {name}[{size}] = {{\n"
                   + "".join(f"    {{{sp.w}, {sp.h}, {sp.name}_PX}},\n" for sp in sps) + "};")

    single(s["wide"])
    array("GN_MUSH", "SP_COUNT", s["mush"])
    array("GN_BOSS", "3", s["boss"])
    array("GN_ICON", "AB_COUNT", s["icon"])
    single(s["logo"])
    array("GN_TREE", "2", s["tree"])
    single(s["house"])
    array("GN_EYE", "2", s["eye"])
    single(s["star"])
    single(s["spark"])
    single(s["splat"])
    single(s["cloud"])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(out) + "\n")


# ---- the contact sheet

def write_png(path, pal, dim, s):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print("gen_art: Pillow missing, no contact sheet")
        return
    Z = 4
    W = 1040
    img = Image.new("RGB", (W, 1500), (34, 34, 42))
    d = ImageDraw.Draw(img)

    def blit(sp, x, y, dim_it=False):
        for yy in range(sp.h):
            for xx in range(sp.w):
                c = sp.px[yy * sp.w + xx]
                if c == 0:
                    continue
                if dim_it:
                    c = dim[c]
                d.rectangle([x + xx * Z, y + yy * Z, x + xx * Z + Z - 1, y + yy * Z + Z - 1], fill=pal[c])

    def checker(x, y, w, h):
        for yy in range(0, h, 8):
            for xx in range(0, w, 8):
                d.rectangle([x + xx, y + yy, x + xx + 7, y + yy + 7],
                            fill=(58, 58, 68) if (xx // 8 + yy // 8) & 1 else (48, 48, 58))

    y = 8
    d.text((8, y), "GNOMNOM art  (4x; the palette, then the sprites on a checker and dimmed through GN_DIM)",
           fill=(230, 230, 230))
    y += 16
    for i in range(256):
        x = 8 + (i % 32) * 32
        yy = y + (i // 32) * 20
        d.rectangle([x, yy, x + 30, yy + 18], fill=pal[i])
        if i % 32 == 0:
            d.text((x + 2, yy + 2), str(i), fill=(255, 255, 255) if sum(pal[i]) < 380 else (0, 0, 0))
    y += 8 * 20 + 6
    d.text((8, y), "GN_DIM:", fill=(230, 230, 230))
    for i in range(256):
        x = 8 + (i % 32) * 32
        yy = y + 12 + (i // 32) * 8
        d.rectangle([x, yy, x + 30, yy + 6], fill=pal[dim[i]])
    y += 12 + 8 * 8 + 10
    sky, ground = pal[PAL_SKY:PAL_SKY + 32], pal[PAL_GROUND:PAL_GROUND + 32]
    for st, name in enumerate(STAGES):
        x = 8 + st * 128
        for r in range(40):
            d.rectangle([x, y + r, x + 119, y + r], fill=sky[(st * 4 + r * 32 // 40) % 32])
        for r in range(12):
            d.rectangle([x, y + 40 + r, x + 119, y + 40 + r], fill=ground[(st * 4 + r * 32 // 12) % 32])
        d.rectangle([x, y, x + 119, y + 51], outline=(0, 0, 0))
        d.text((x + 4, y + 4), f"{st + 1} {name}", fill=(255, 255, 255))
    y += 64

    def row(items, label):
        nonlocal y
        d.text((8, y), label, fill=(230, 230, 230))
        y += 14
        x = 8
        tallest = 0
        for sp, name in items:
            w, h = sp.w * Z, sp.h * Z
            if x + w * 2 + 12 > W:
                x = 8
                y += tallest + 18
                tallest = 0
            checker(x, y, w, h)
            blit(sp, x, y)
            checker(x + w + 4, y, w, h)
            blit(sp, x + w + 4, y, True)
            d.text((x, y + h + 2), name, fill=(200, 200, 210))
            x += w * 2 + 16
            tallest = max(tallest, h)
        y += tallest + 22

    row([(sp, n) for sp, n in zip(s["gnome"], GNOME_FRAMES)] + [(s["wide"], "WIDE")], "GN_GNOME[GNOME_FRAMES], GN_GNOME_WIDE")
    row([(sp, n) for sp, n in zip(s["mush"], SPECIES)], "GN_MUSH[SP_COUNT]")
    row([(sp, f"BOSS {i}") for i, sp in enumerate(s["boss"])], "GN_BOSS[3]")
    row([(sp, n) for sp, n in zip(s["icon"], ABILITIES)], "GN_ICON[AB_COUNT]")
    row([(s["logo"], "LOGO")], "GN_LOGO")
    row([(s["tree"][0], "TREE 0"), (s["tree"][1], "TREE 1"), (s["house"], "HOUSE"), (s["cloud"], "CLOUD")],
        "GN_TREE[2], GN_HOUSE, GN_CLOUD")
    row([(s["eye"][0], "EYE open"), (s["eye"][1], "EYE closed"), (s["star"], "STAR"), (s["spark"], "SPARK"),
         (s["splat"], "SPLAT")], "GN_EYE[2], GN_STAR, GN_SPARK, GN_SPLAT")
    img = img.crop((0, 0, W, y + 4))
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)


def main(argv):
    if len(argv) != 3:
        raise SystemExit("usage: gen_art.py OUT/gn_art.h OUT/art.png")
    pal = build_palette()
    dim = build_dim(pal)
    s = build_sprites()
    write_header(pathlib.Path(argv[1]), pal, dim, s)
    write_png(pathlib.Path(argv[2]), pal, dim, s)
    n = sum(len(sp.px) for group in s.values() for sp in (group if isinstance(group, list) else [group]))
    print(f"gen_art: {n} sprite bytes, 1024 palette bytes -> {argv[1]}")


if __name__ == "__main__":
    main(sys.argv)
