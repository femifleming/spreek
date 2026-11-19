#!/usr/bin/env python3
"""Runs inside a Python that has piper-tts installed (render.py calls it): renders a list of jobs with Piper.
    python -I piper_render.py jobs.json
jobs.json: [{"model": "/path/voice.onnx", "text": "...", "out": "/path/out.wav"}, ...]
Each model is loaded once. Prints one line per 100 clips."""
import json, sys, wave
from piper import PiperVoice

jobs = json.load(open(sys.argv[1], encoding="utf-8"))
voices = {}
for i, j in enumerate(jobs):
    v = voices.get(j["model"])
    if v is None:
        v = voices[j["model"]] = PiperVoice.load(j["model"])
    try:
        with wave.open(j["out"], "wb") as f:
            if hasattr(v, "synthesize_wav"): v.synthesize_wav(j["text"], f)
            else: v.synthesize(j["text"], f)
    except Exception as e:  # noqa
        print("  (failed) %s: %s" % (j["text"], e), flush=True)
    if (i + 1) % 100 == 0: print("  %d / %d" % (i + 1, len(jobs)), flush=True)
