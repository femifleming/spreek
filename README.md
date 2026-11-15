<p align="center"><img src="Resources/icon.png" width="128"></p>

# spreekbuis

Dutch as an instrument. A VST3/AU/Standalone instrument that plays the words and sentences you learn in
[Tongval](../tongval) — so practising Dutch and making experimental music become the same thing.

## Layers (mix freely)
- **Loop** — the current Tongval lesson (or any lesson) as tempo-synced call & response: an item in the call
  bars, silence (plus the English whispered) in the answer bars. Karaoke highlight; MIDI out note at every word onset
  (first word accented) — the rhythm of Dutch as a sequencer.
- **Wolk** (cloud) — granular cloud of the words you know: density, grain size, scatter, position, pitch, stereo,
  reverse, and *translation grains*: English glosses run through a 12-band noise vocoder (whisper).
- **Klinker** — formant filter whose XY pad is the Dutch vowel chart (i y u ɪ ʏ e ø o ə ɔ ɛ a ɑ), with diphthong
  glides ɛi (ij/ei), œy (ui), ʌu (ou/au). Source: saw, noise, the sidechain input, or saw + input. Click or drag the chart
  to hear it; notes play it when *Toetsen* includes Klinker (a hint on the chart says when they don't). Live doesn't send
  audio into instruments, so *Input* is silent there (the chart says so).
- **Stem** (Man / Vrouw, top bar) — the voice of the words (a second rendered bank) and the vowel chart (female formants,
  an octave higher). Render the woman's voice with `tools/render.sh --female` (Claire if installed, else Ellen, Flemish).
- **G** — the Dutch g/ch as a pitched fricative: hard Holland /x/ with uvular rasp ↔ soft Flemish /ɣ/ with voicing.
- **Orca** — UDP on 127.0.0.1:49162 (Orca's `;` operator): `;fiets` speaks a word in your bank, `;g` / `;g7` fires
  the g (base-36 semitones), `;vui` `;veu` `;vij` `;va` hits a vowel, anything else base-36 indexes your items.
- **Keys** — MIDI notes play words (note → item), the vowel pad, the g, or everything.

## Words and progress
- Voices: `tools/render.sh` renders every Tongval item (words, sentences, dialog, lesson 0) with the best installed
  nl-NL voice plus English glosses into `~/Library/Application Support/Spreekbuis/voice/` (manifest with word onsets).
  Re-run it after installing *Xander (Enhanced)* or *Claire (Premium)*; add `--stories` for story lines, `--force` to redo all.
- Only items you've learned in Tongval are offered (plus the current lesson); **Alle items** plays everything.
- Plays are logged to `~/Library/Application Support/Spreekbuis/plays.json`; Tongval reads it at launch and shows
  ♪ counts in the word lists and gives listening XP (1 per 5 plays, max 30 a day).

## Build
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # JUCE_DIR defaults to ~/disting-ex-plugins/JUCE
    cmake --build build                                       # installs into ~/Library/Audio/Plug-Ins
    build/SBTest_artefacts/Release/SBTest                     # offline render test of every layer

Local project; not published.
