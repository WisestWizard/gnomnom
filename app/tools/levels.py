#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Kyle McCann
"""Render each stage's intro and first bars under the autoplayer in the host simulator and print its levels: the
output's peak and RMS over time (dBFS) and the band's per-part peaks the simulator reports, so the stages and the
parts can be matched by ear and by number. Run from the repository after ./build.sh gnomnom --host.
  python3 gnomnom/tools/levels.py            every stage, 16 bars of the tune each, into gnomnom/build/levels/
  python3 gnomnom/tools/levels.py 4 8        stages 4 and 8 only"""
import array, math, pathlib, subprocess, sys, wave
ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
HOST = ROOT / "gnomnom/build/host/gnomnom_host"
OUT = ROOT / "gnomnom/build/levels"
BPM0 = [88, 112, 124, 128, 136, 138, 146, 158]       # the stage table's bpm0 (tunes.c); the wait is generous
PARTS = ["lead", "bass", "stab", "drums", "", "out"]  # expect peakN: PART_LEAD, PART_BASS, PART_STAB, the drums, -, the output

def script(stage, bars=16):
    ms_bar = 4 * 60000 // BPM0[stage - 1]
    s = ["wait 700", "master 4096", f"gn stage {stage}", "tap PLAY", "gn auto 999", f"wav stage{stage}.wav"]
    for b in range(4 + bars):
        s.append(f"wait {ms_bar}")
        if b in (3, 7, 11, 19):
            s += [f"expect peak{i} >-1" for i in (0, 1, 2, 3, 5)]
    s += ["wavstop", "wait 200", "expect eaten >0"]
    return "\n".join(s) + "\n"

def db(x):
    return 20.0 * math.log10(x) if x > 1e-9 else -99.0

def analyse(path):
    with wave.open(str(path)) as w:
        n, rate = w.getnframes(), w.getframerate()
        data = array.array("h")
        data.frombytes(w.readframes(n))
    peak = max(abs(v) for v in data) / 32768.0 if data else 0.0
    rms = math.sqrt(sum(v * v for v in data) / (len(data) * 32768.0 ** 2)) if data else 0.0
    win = rate * 2 * 2                                  # two seconds of stereo frames
    rows = []
    for i in range(0, len(data), win):
        seg = data[i:i + win]
        if len(seg) < win // 4:
            break
        pk = max(abs(v) for v in seg) / 32768.0
        r = math.sqrt(sum(v * v for v in seg) / (len(seg) * 32768.0 ** 2))
        rows.append((i / (2 * rate), db(pk), db(r)))
    return db(peak), db(rms), rows

def main():
    stages = [int(a) for a in sys.argv[1:]] or list(range(1, 9))
    OUT.mkdir(parents=True, exist_ok=True)
    for s in stages:
        (OUT / f"stage{s}.txt").write_text(script(s))
        r = subprocess.run([str(HOST), str(OUT / f"stage{s}.txt"), str(OUT)], capture_output=True, text=True)
        peaks = [l.strip() for l in r.stdout.splitlines() if "peak" in l]
        fails = [l for l in r.stdout.splitlines() if l.startswith("FAIL")]
        pk, rms, rows = analyse(OUT / f"stage{s}.wav")
        print(f"stage {s}: peak {pk:6.1f} dBFS  rms {rms:6.1f} dBFS  {'FAIL ' + fails[0] if fails else ''}")
        for t, p, q in rows:
            print(f"   {t:5.1f}s  peak {p:6.1f}  rms {q:6.1f}")
        for l in peaks:
            print("   " + l)
    print(f"wavs in {OUT}")

if __name__ == "__main__":
    main()
