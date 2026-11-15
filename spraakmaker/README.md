# spraakmaker

Unofficial, local-only emulation of the Moddict Phonemer VCE26-S3 Eurorack "speech computer", as a
VST3 / AU / standalone instrument (dark panel edition) that speaks **English and Dutch**.
Not affiliated with Moddict; it copies the module's layout for personal use, so it is never published.

## How it talks
- **Text → phonemes**: English via the CMU Pronouncing Dictionary (data/en_dict.txt, 126k words) plus
  letter rules for unknown words; Dutch via a rule-based converter (Source/G2P.cpp: open/closed
  syllables, digraphs ui/eu/ij/ei/ou/oe/ie, sch/ch/g, schwa, -lijk/-ig, n-deletion, final devoicing,
  prefix and suffix stress).
- **Prosody** (Source/Planner.cpp): stress lengthening, phrase-final lengthening, accents, declination,
  question rise.
- **Synth** (Source/Voice.cpp): Klatt-style formant synthesizer — KLGLOTT88 glottal source with
  jitter/shimmer/breath, cascade F1–F5, nasal pole/zero, parallel frication + bursts, coarticulated
  transitions, unaspirated Dutch stops, uvular ʀ trill.

## Panel → plug-in
Knobs Pitch/Formant/Intonation/Brightness/Speed/Emotion, M1–M3 macros (destinations on MOD page),
Data encoder (turn = edit / browse presets, push = load / trigger, Shift+push = save preset),
F1–F4 = TRG/SRC · VOICE · FX · MOD pages, L/R = select, Shift/Exit, round button = trigger.
Jacks: Carrier / Modulator = side-chain inputs 1 / 2, Pitch + Trg In = MIDI notes, Trg Out = MIDI
notes 60 (word) / 62 (phoneme), CV1–CV4 = modulation slots (KNOB/LFO1-4/VEL/MODWH/RAND → dest).
USB Host = type on the computer keyboard, Enter speaks it. Shift+F1 edits the current list,
Shift+F2 loads a Scala .scl tuning.

Modes: CLASSIC / VOCODER / VOSINGER (4-voice poly, 10 chord memories, Fine detune); triggers
AUTO (tempo-synced or free), TRIG, GATE, PH.TRG, PH.GATE, Retrigger; sources DICT, ALPHA, KEYB,
MARKOV (Alice in Wonderland / Tongval Dutch sentences), LETTR (sound poetry), LIST (99 lists);
microtonal EQUAL/JUST/PYTHAG/MEANTN/24-TET/USER .scl; FX bitcrush, width, ping-pong delay, reverb.
100 preset slots (20 factory). Data in ~/Library/Application Support/Spraakmaker (lists/, presets/).

## Build / test
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
    build/SpraakTest_artefacts/Release/SpraakTest <outdir>          # G2P coverage + renders
    build/SpraakShot_artefacts/Release/SpraakShot.app/Contents/MacOS/SpraakShot <outdir>   # all presets + UI shots
    build/SpraakFull_artefacts/Release/SpraakFull.app/Contents/MacOS/SpraakFull           # every feature, one by one (110 checks)
JUCE comes from ~/disting-ex-plugins/JUCE (cache var JUCE_DIR). CMU dict: data/raw/LICENSE.cmudict (BSD).
