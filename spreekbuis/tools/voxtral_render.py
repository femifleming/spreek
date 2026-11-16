#!/usr/bin/env python3
"""Runs inside a Python that has mlx-audio (render.py calls it, macOS Apple Silicon): renders a job list with
Voxtral-4B-TTS (Mistral; native Dutch presets nl_female / nl_male), 24 kHz, peak-normalised.
    HF_HOME=<weights> python -I voxtral_render.py jobs.json
jobs.json: [{"voice": "nl_male", "text": "...", "out": "/path/out.wav"}, ...]
The model is loaded once; one line per clip with the timing (RTF ~0.8 on an M2 Max)."""
import json, os, sys, time, wave, struct

os.environ.setdefault("HF_HOME", os.path.expanduser("~/tongval/tools/hf"))
import numpy as np
from mlx_audio.tts.utils import load_model

repo = os.environ.get("VOXTRAL_MODEL", "mlx-community/Voxtral-4B-TTS-2603-mlx-4bit")
jobs = json.load(open(sys.argv[1], encoding="utf-8"))
t0 = time.time(); m = load_model(repo); print("loaded %s in %.1f s" % (repo, time.time() - t0), flush=True)
tot = 0.0; t0 = time.time()
for i, j in enumerate(jobs):
    t1 = time.time(); segs = []; sr = 24000
    try:
        for r in m.generate(text=j["text"], voice=j["voice"], temperature=0.6):
            segs.append(np.array(r.audio, dtype=np.float32)); sr = r.sample_rate
    except Exception as e:  # noqa
        print("  (failed) %s: %s" % (j["text"], e), flush=True); continue
    a = np.concatenate(segs) if segs else np.zeros(1, dtype=np.float32)
    pk = float(np.max(np.abs(a))) if a.size else 0.0
    if pk > 0.01: a = a * (0.89 / pk)          # Voxtral is quiet: peak-normalise like the other engines' level
    with wave.open(j["out"], "wb") as f:
        f.setnchannels(1); f.setsampwidth(2); f.setframerate(int(sr))
        f.writeframes(struct.pack("<%dh" % a.size, *np.clip(a * 32767, -32767, 32767).astype(np.int16).tolist()))
    d = a.size / sr; tot += d
    print("%d/%d: %.1fs for %.1fs audio  %s" % (i + 1, len(jobs), time.time() - t1, d, j["text"][:40]), flush=True)
print("total audio %.1fs in %.1fs (RTF %.2f)" % (tot, time.time() - t0, (time.time() - t0) / max(tot, 1e-6)), flush=True)
