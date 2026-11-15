#!/usr/bin/env python3
"""Render the Spreekbuis voice bank with the system's text-to-speech, on macOS, Windows or Linux.

    python3 tools/render.py [--female] [--force] [--stories] [--engine auto|say|sapi|piper|espeak] [--voice NAME] [--items FILE]

Output (the folder Spreekbuis reads):
    macOS    ~/Library/Application Support/Spreekbuis/voice[-female]/
    Windows  %APPDATA%\\Spreekbuis\\voice[-female]\\
    Linux    ~/.config/Spreekbuis/voice[-female]/
with nl/<id>.wav (48 kHz mono 16-bit), en/<id>.wav (the English gloss) and manifest.json (word onsets).

Engines, best first (chosen per language; --engine forces one):
    piper    neural voices (VITS), the same on every OS: nl_NL-pim-medium (the man), nl_BE-nathalie-medium (the woman),
             en_US-lessac-medium for the English glosses. `python3 tools/render.py --setup-piper` makes a venv with
             piper-tts in the data folder and downloads those three voices from the rhasspy/piper-voices Hugging Face
             repo (~200 MB). An existing install is reused: PIPER_PYTHON=<python with piper>, PIPER_VOICES=<dir with
             .onnx>, PIPER_MODEL=<one .onnx>, the Tongval venv (~/tongval/tools/venv + tools/piper-voices), a `piper`
             binary on PATH, or ~/.local/share/piper.
    macOS    `say` with the best installed Dutch voice (Premium > Enhanced > compact; install better ones in
             System Settings > Accessibility > Spoken Content > Manage Voices > Dutch). With Apple voices,
             tools/render.sh prefers tools/render.swift, which gets exact word onsets from the speech engine.
    Windows  PowerShell / System.Speech (SAPI) with an installed Dutch voice (Settings > Time & language >
             Speech > Manage voices > add Dutch).
    Linux    espeak-ng -v nl.
Word onsets are estimated from the audio (silence between words); the Swift renderer's are exact.
Without any engine the plug-in still speaks: it has a built-in formant voice.
"""
import argparse, json, os, re, subprocess, sys, shutil, struct, wave, tempfile, math, urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent


def data_root():
    if os.environ.get("SPREEKBUIS_DATA_DIR"): return Path(os.environ["SPREEKBUIS_DATA_DIR"])
    if sys.platform == "darwin": return Path.home() / "Library" / "Application Support" / "Spreekbuis"
    if sys.platform.startswith("win"): return Path(os.environ.get("APPDATA", Path.home() / "AppData" / "Roaming")) / "Spreekbuis"
    return Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "Spreekbuis"


def out_root(female):
    name = "voice-female" if female else "voice"
    if os.environ.get("SPREEKBUIS_DATA_DIR"):   # the same override the plug-in honours
        return Path(os.environ["SPREEKBUIS_DATA_DIR"]) / name
    if sys.platform == "darwin":
        base = Path.home() / "Library" / "Application Support"
    elif sys.platform.startswith("win"):
        base = Path(os.environ.get("APPDATA", Path.home() / "AppData" / "Roaming"))
    else:
        base = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
    return base / "Spreekbuis" / name


def file_id(key):   # FNV-1a 64, same as render.swift and the plug-in's built-in voice
    h = 0xcbf29ce484222325
    for c in key.encode("utf-8"):
        h ^= c
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % h


def load_items(path):
    if path:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    # Tongval's own data when it is here (the course's live word lists), else the bundled list
    tongval = Path.home() / "tongval" / "web" / "data"
    if tongval.is_dir() and shutil.which("node"):
        try:
            return json.loads(subprocess.run(["node", str(HERE / "export_items.js")], check=True, capture_output=True).stdout)
        except Exception as e:  # noqa
            print("Tongval export failed (%s), using the bundled list" % e)
    return json.loads((ROOT / "data" / "items.json").read_text(encoding="utf-8"))


# ---------------------------------------------------------------- wav helpers (stdlib only)
def read_wav(path):
    with wave.open(str(path), "rb") as w:
        ch, sw, sr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if sw == 2:
        s = struct.unpack("<%dh" % (len(raw) // 2), raw)
        scale = 32768.0
    elif sw == 1:
        s = [(b - 128) * 256 for b in raw]; scale = 32768.0
    elif sw == 4:
        s = struct.unpack("<%di" % (len(raw) // 4), raw); scale = 2147483648.0
    else:
        raise ValueError("unsupported sample width %d" % sw)
    if ch > 1:
        s = [sum(s[i:i + ch]) / ch for i in range(0, len(s), ch)]
    return [x / scale for x in s], sr


def resample(x, sr, target=48000):
    if sr == target or not x:
        return x
    n = int(len(x) * target / sr)
    out = [0.0] * n
    r = sr / target
    for i in range(n):
        p = i * r; j = int(p); f = p - j
        a = x[j] if j < len(x) else 0.0
        b = x[j + 1] if j + 1 < len(x) else a
        out[i] = a + (b - a) * f
    return out


def write_wav(path, x, sr=48000):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(struct.pack("<%dh" % len(x), *[max(-32767, min(32767, int(round(v * 32767)))) for v in x]))


def trim(x, sr=48000, thr=0.002):
    a = 0
    while a < len(x) and abs(x[a]) < thr: a += 1
    b = len(x)
    while b > a and abs(x[b - 1]) < thr: b -= 1
    a = max(0, a - sr // 100); b = min(len(x), b + sr // 20)
    return x[a:b], a


def onsets_from_audio(x, n_words, sr=48000):
    """Starts of the speech segments between pauses; if their number doesn't match the word count, the words are
    spread over the sound in proportion to their length (close enough for the karaoke and the MIDI out)."""
    if n_words <= 1 or not x:
        return [0]
    frame = sr // 100   # 10 ms
    env = [math.sqrt(sum(v * v for v in x[i:i + frame]) / frame) for i in range(0, len(x) - frame, frame)]
    peak = max(env) if env else 0
    if peak <= 0:
        return [0] * n_words
    thr = peak * 0.08
    starts, on, quiet = [], False, 0
    for i, e in enumerate(env):
        if e > thr:
            if not on and (not starts or quiet >= 4): starts.append(i * frame)
            on, quiet = True, 0
        else:
            quiet += 1
            if quiet >= 4: on = False
    if len(starts) == n_words:
        return starts
    return None


def proportional_onsets(x, words):
    lens = [max(1, len(w)) for w in words]
    total = sum(lens)
    out, acc = [], 0
    for L in lens:
        out.append(int(len(x) * acc / total)); acc += L
    return out


# ---------------------------------------------------------------- engines
class Engine:
    name = "?"
    def voices(self): return []          # list of (name, lang, gender)  gender: m / f / ?
    def speak(self, voice, text, out):   # writes a wav (any rate / width) to out
        raise NotImplementedError


class Say(Engine):
    name = "say"
    def voices(self):
        res = []
        for line in subprocess.run(["say", "-v", "?"], capture_output=True, text=True).stdout.splitlines():
            # "Xander (Dutch (Netherlands)) nl_NL    # Hallo! ..."  /  "Xander (Enhanced) (Dutch (Netherlands)) nl_NL ..."
            m = re.match(r"^(.+?)\s+([a-z]{2,3}[_-][A-Z]{2,4})\s+#", line)
            if not m: continue
            name, lang = m.group(1).strip(), m.group(2).replace("_", "-")
            if name.endswith(")"):   # drop the trailing "(language (region))" group, keep "(Enhanced)" / "(Premium)"
                depth, i = 0, len(name) - 1
                while i >= 0:
                    depth += 1 if name[i] == ")" else -1 if name[i] == "(" else 0
                    if depth == 0: break
                    i -= 1
                name = name[:i].strip()
            g = "f" if any(n in name for n in ("Claire", "Ellen", "Samantha", "Ava", "Zoe", "Allison", "Susan")) else "m" if any(n in name for n in ("Xander", "Alex", "Tom", "Daniel", "Fred")) else "?"
            res.append((name, lang, g))
        return res
    def speak(self, voice, text, out):
        subprocess.run(["say", "-v", voice, "-o", str(out), "--file-format=WAVE", "--data-format=LEI16@48000", text], check=True)


class Sapi(Engine):
    name = "sapi"
    PS = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass"]
    def voices(self):
        cmd = ("Add-Type -AssemblyName System.Speech; (New-Object System.Speech.Synthesis.SpeechSynthesizer).GetInstalledVoices() "
               "| ForEach-Object { $_.VoiceInfo } | ForEach-Object { $_.Name + '|' + $_.Culture.Name + '|' + $_.Gender }")
        res = []
        for line in subprocess.run(self.PS + ["-Command", cmd], capture_output=True, text=True).stdout.splitlines():
            p = line.strip().split("|")
            if len(p) == 3: res.append((p[0], p[1], "f" if p[2].lower().startswith("f") else "m" if p[2].lower().startswith("m") else "?"))
        return res
    def speak_many(self, jobs):   # [(voice, text, out)] in one PowerShell process
        jf = Path(tempfile.mkstemp(suffix=".json")[1])
        jf.write_text(json.dumps([{"voice": v, "text": t, "out": str(o)} for v, t, o in jobs]), encoding="utf-8")
        script = r"""
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Speech
$syn = New-Object System.Speech.Synthesis.SpeechSynthesizer
$fmt = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(48000, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)
$jobs = Get-Content -Raw -Encoding UTF8 '%s' | ConvertFrom-Json
foreach ($j in $jobs) { $syn.SelectVoice($j.voice); $syn.SetOutputToWaveFile($j.out, $fmt); $syn.Speak($j.text); $syn.SetOutputToNull() }
$syn.Dispose()
""" % str(jf).replace("'", "''")
        sf = Path(tempfile.mkstemp(suffix=".ps1")[1]); sf.write_text(script, encoding="utf-8")
        subprocess.run(self.PS + ["-File", str(sf)], check=True)
        jf.unlink(); sf.unlink()


PIPER_VOICES = {   # name: (hugging face path, gender)   https://huggingface.co/rhasspy/piper-voices
    "nl_NL-pim-medium": ("nl/nl_NL/pim/medium", "m"),
    "nl_BE-nathalie-medium": ("nl/nl_BE/nathalie/medium", "f"),
    "en_US-lessac-medium": ("en/en_US/lessac/medium", "f"),
}
PIPER_GENDER = { "pim": "m", "ronnie": "m", "rdh": "m", "nathalie": "f", "lessac": "f", "amy": "f", "ryan": "m", "joe": "m", "kathleen": "f" }


class Piper(Engine):
    """Piper neural voices through a Python that has piper-tts (one process for all clips), or the piper binary."""
    name = "piper"
    def __init__(self):
        self.python = None; self.binary = shutil.which("piper"); self.models = {}
        for cand in [os.environ.get("PIPER_PYTHON"), data_root() / "piper" / "venv" / ("Scripts/python.exe" if sys.platform.startswith("win") else "bin/python"),
                     Path.home() / "tongval" / "tools" / "venv" / "bin" / "python", sys.executable]:
            if not cand: continue
            cand = Path(cand)
            if cand.exists() and subprocess.run([str(cand), "-I", "-c", "import piper"], capture_output=True).returncode == 0:
                self.python = cand; break
        dirs = []
        if os.environ.get("PIPER_MODEL"): dirs.append(Path(os.environ["PIPER_MODEL"]).parent)
        if os.environ.get("PIPER_VOICES"): dirs.append(Path(os.environ["PIPER_VOICES"]))
        dirs += [data_root() / "piper-voices", Path.home() / "tongval" / "tools" / "piper-voices",
                 Path.home() / ".local" / "share" / "piper", Path.home() / "piper", Path("/usr/share/piper-voices")]
        for d in dirs:
            if d.is_dir():
                for m in sorted(d.rglob("*.onnx")):
                    if m.with_name(m.name + ".json").exists(): self.models.setdefault(m.stem, m)
    def available(self): return (self.python is not None or self.binary is not None) and bool(self.models)
    def voices(self):
        out = []
        for stem, m in self.models.items():
            lang = stem.split("-")[0].replace("_", "-")
            who = stem.split("-")[1] if "-" in stem else ""
            out.append((stem, lang, PIPER_GENDER.get(who, "?")))
        return out
    def speak_many(self, jobs):   # [(voice stem, text, out)]
        js = [{"model": str(self.models[v]), "text": t, "out": str(o)} for v, t, o in jobs]
        if self.python:
            jf = Path(tempfile.mkstemp(suffix=".json")[1]); jf.write_text(json.dumps(js), encoding="utf-8")
            subprocess.run([str(self.python), "-I", str(HERE / "piper_render.py"), str(jf)], check=True)
            jf.unlink()
        else:
            for j in js:
                subprocess.run([self.binary, "--model", j["model"], "--output_file", j["out"]], input=j["text"].encode("utf-8"), check=True, capture_output=True)


def setup_piper():
    """A venv with piper-tts in the data folder (unless a Python with piper exists already) and the three voices."""
    root = data_root(); root.mkdir(parents=True, exist_ok=True)
    p = Piper()
    if p.python is None:
        venv = root / "piper" / "venv"
        print("creating %s and installing piper-tts" % venv)
        subprocess.run([sys.executable, "-m", "venv", str(venv)], check=True)
        py = venv / ("Scripts/python.exe" if sys.platform.startswith("win") else "bin/python")
        subprocess.run([str(py), "-m", "pip", "install", "-q", "piper-tts"], check=True)
    else:
        print("piper found in %s" % p.python)
    vd = root / "piper-voices"; vd.mkdir(exist_ok=True)
    for name, (hf, _) in PIPER_VOICES.items():
        if name in p.models: print("have %s (%s)" % (name, p.models[name])); continue
        for ext in (".onnx", ".onnx.json"):
            url = "https://huggingface.co/rhasspy/piper-voices/resolve/main/%s/%s%s" % (hf, name, ext)
            dst = vd / (name + ext)
            if dst.exists(): continue
            print("downloading %s" % url)
            urllib.request.urlretrieve(url, dst)
    print("piper voices in %s" % vd)


class Espeak(Engine):
    name = "espeak"
    def voices(self):
        return [("nl", "nl-NL", "m"), ("en-us", "en-US", "m")]
    def speak(self, voice, text, out):
        subprocess.run(["espeak-ng", "-v", voice, "-s", "150", "-w", str(out), text], check=True, capture_output=True)


def engines(name):
    """Candidate engines in order of preference (best first), limited to one with --engine."""
    out = []
    if name in ("auto", "piper"):
        p = Piper()
        if p.available(): out.append(p)
    if name in ("auto", "say") and sys.platform == "darwin" and shutil.which("say"): out.append(Say())
    if name in ("auto", "sapi") and sys.platform.startswith("win") and shutil.which("powershell"): out.append(Sapi())
    if name in ("auto", "espeak") and shutil.which("espeak-ng"): out.append(Espeak())
    return out


def choose(voices, prefix, female, want_name=None):
    vs = [v for v in voices if v[1].lower().startswith(prefix)]
    if want_name:
        vs2 = [v for v in vs if want_name.lower() in v[0].lower()]
        if vs2: return vs2[0]
    def score(v):
        n = v[0]
        s = 0
        if v[1] in ("nl-NL", "en-US"): s += 100   # Netherlands Dutch over Flemish, US English over the others
        if "Premium" in n: s += 30
        elif "Enhanced" in n: s += 20
        if n == ("nl_BE-nathalie-medium" if female else "nl_NL-pim-medium"): s += 200   # the bundle's two Piper voices (nathalie is nl-BE)
        if "medium" in n or "high" in n: s += 10   # piper model quality
        if v[2] == ("f" if female else "m"): s += 5
        return s
    return max(vs, key=score) if vs else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--female", action="store_true"); ap.add_argument("--force", action="store_true"); ap.add_argument("--stories", action="store_true")
    ap.add_argument("--engine", default="auto"); ap.add_argument("--voice"); ap.add_argument("--items")
    ap.add_argument("--setup-piper", action="store_true", help="install piper-tts in a venv and download the Piper voices")
    ap.add_argument("--probe", action="store_true", help="print the engines that would be used and exit (0 = Piper)")
    a = ap.parse_args()
    if a.setup_piper: setup_piper(); return 0
    cands = engines(a.engine)
    if not cands:
        print("no speech engine found (piper: tools/render.py --setup-piper; macOS: say; Windows: PowerShell/System.Speech; Linux: espeak-ng).\n"
              "Spreekbuis still speaks with its built-in formant voice."); return 1
    # the Dutch voice from the best engine that has one; the English gloss from the best engine that has English
    nl = en = None; nl_eng = en_eng = None
    for e in cands:
        vs = e.voices()
        if nl is None:
            v = choose(vs, "nl", a.female, a.voice)
            if v: nl, nl_eng = v, e
        if en is None:
            v = choose(vs, "en", a.female) or choose(vs, "en", not a.female)
            if v: en, en_eng = v, e
    if a.probe:
        print("Dutch: %s / %s; English: %s / %s" % (nl_eng.name if nl_eng else "-", nl[0] if nl else "-", en_eng.name if en_eng else "-", en[0] if en else "-"))
        return 0 if nl_eng and nl_eng.name == "piper" else 2
    if nl is None:
        print("%s: no Dutch voice installed (see the top of this file)." % cands[0].name); return 1
    print("Dutch voice: %s (%s) - English: %s (%s)" % (nl[0], nl_eng.name, en[0] if en else "(none)", en_eng.name if en_eng else "-"))
    items = load_items(a.items)
    root = out_root(a.female)
    (root / "nl").mkdir(parents=True, exist_ok=True); (root / "en").mkdir(parents=True, exist_ok=True)
    manifest = root / "manifest.json"
    old = {}
    if not a.force and manifest.exists():
        try:
            j = json.loads(manifest.read_text(encoding="utf-8"))
            if j.get("voice") == nl_eng.name + ":" + nl[0]: old = {i["key"]: i for i in j.get("items", [])}
        except Exception: pass
    todo = []
    out_items = []
    for it in items:
        if it.get("kind") == "t" and not a.stories: continue
        key = it["key"]; fid = file_id(key)
        nlf, enf = root / "nl" / (fid + ".wav"), root / "en" / (fid + ".wav")
        if key in old and nlf.exists() and enf.exists():
            out_items.append(old[key]); continue
        todo.append((it, fid, nlf, enf))
    print("%d items, %d to render" % (len(items), len(todo)))
    tmp = Path(tempfile.mkdtemp(prefix="spreekbuis-"))
    # raw renders, one engine at a time (one process for all jobs where the engine allows it)
    def run_jobs(eng, jobs):
        if hasattr(eng, "speak_many"):
            for i in range(0, len(jobs), 400): eng.speak_many(jobs[i:i + 400]); print("  %s %d / %d" % (eng.name, min(i + 400, len(jobs)), len(jobs)))
        else:
            for i, (v, t, o) in enumerate(jobs):
                try: eng.speak(v, t, o)
                except subprocess.CalledProcessError as e: print("  (failed) %s: %s" % (t, e))
                if (i + 1) % 100 == 0: print("  %s %d / %d" % (eng.name, i + 1, len(jobs)))
    run_jobs(nl_eng, [(nl[0], it["nl"], tmp / (fid + "-nl.wav")) for it, fid, nlf, enf in todo])
    if en: run_jobs(en_eng, [(en[0], it["en"], tmp / (fid + "-en.wav")) for it, fid, nlf, enf in todo])
    # convert + onsets
    for it, fid, nlf, enf in todo:
        src = tmp / (fid + "-nl.wav")
        if not src.exists(): print("  (no audio) %s" % it["nl"]); continue
        x, sr = read_wav(src); x = resample(x, sr); x, _ = trim(x)
        words = it["nl"].split()
        ons = onsets_from_audio(x, len(words)) or proportional_onsets(x, words)
        write_wav(nlf, x)
        esrc = tmp / (fid + "-en.wav")
        if esrc.exists():
            e, esr = read_wav(esrc); e = resample(e, esr); e, _ = trim(e); write_wav(enf, e)
        rec = dict(it); rec.update(file="nl/%s.wav" % fid, en_file="en/%s.wav" % fid, onsets=ons, frames=len(x))
        out_items.append(rec)
    shutil.rmtree(tmp, ignore_errors=True)
    manifest.write_text(json.dumps({"version": 1, "rate": 48000, "voice": nl_eng.name + ":" + nl[0], "voiceName": nl[0] + (" (Piper)" if nl_eng.name == "piper" else ""),
                                    "enVoice": en[0] if en else "", "items": out_items}, ensure_ascii=False, sort_keys=True), encoding="utf-8")
    print("done: %d items -> %s" % (len(out_items), root))
    return 0


if __name__ == "__main__":
    sys.exit(main())
