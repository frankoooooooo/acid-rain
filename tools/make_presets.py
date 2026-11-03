#!/usr/bin/env python3
"""Writes presets/<name>.json, the OP-XY's version-4 presets, from the presets in
definition.json, so the device and the browser preview share them. Mono, the
track's stock filter fully open, a short, plucky amp release. Run after changing them:

    python3 tools/make_presets.py
"""
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
definition = json.loads((ROOT / "definition.json").read_text())


def word(parameter, value):
    if parameter["mapping"]["kind"] == "enum":
        count = len(parameter["mapping"]["labels"])
        choice = min(int(value * count), count - 1)
        return round((choice + 0.5) / count * 32767)  # stock stores the middle of a choice's range
    return round(value * 32767)


folder = ROOT / "presets"
folder.mkdir(exist_ok=True)
for name, values in definition["presets"].items():
    if name == "Init":
        continue  # the builder makes default.json from the controls' defaults
    params = [word(p, values[p["id"]]) for p in definition["parameters"]]
    preset = {
        "engine": {"bendrange": 8191, "highpass": 0,
                   "modulation": {s: {"amount": 16384, "target": 0} for s in ["aftertouch", "modwheel", "pitchbend", "velocity"]},
                   "params": params, "playmode": "mono",
                   "portamento.amount": 0, "portamento.type": 0, "transpose": 0,
                   "tuning.root": 0, "tuning.scale": 0, "velocity.sensitivity": 0, "volume": 16384, "width": 0},
        "envelope": {"amp": {"attack": 0, "decay": 32767, "release": 1024, "sustain": 32767},
                     "filter": {"attack": 0, "decay": 0, "release": 0, "sustain": 32767}},
        "fx": {"active": False, "params": [0, 0, 0, 0, 0, 32767, 0, 0], "type": "svf"},
        "lfo": {"active": False, "params": [0] * 8, "type": "value"},
        "octave": -1, "platform": "OP-XY", "regions": [], "type": definition["id"], "version": 4}
    (folder / (name.lower().replace(" ", "_") + ".json")).write_text(json.dumps(preset, indent=2) + "\n")
