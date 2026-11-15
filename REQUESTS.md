# Requests

The user's typed requests, in order. Both plug-ins kept their own log before they became one bundle.

## spreekbuis

- 2026-11-15 — "what if there was also a vst plugin that taught me dutch, but i could also use in my experimental music? how would you do that?" → proposal (Spreekbuis) → "make it, but no speaking back to it"
- 2026-11-15 — "make both dark theme" (both VSTs): Spreekbuis UI is dark only, Tongval dark palette (bg #141413, panels #1d1c1a, lines #3a3833, text #f1ede4, muted #9b968c, orange #ff6a00, navy #1b2a6b), karaoke highlight yellow #f4c20d with #111 text
- 2026-11-15 — "make a pdf manual for both" → docs/Spreekbuis-manual.pdf (docs/manual.html source)
- "i dont think the klinker section of spreekbuis is working, can you check that all of the things work" → the vowel chart made no sound on its own (only notes routed by Toetsen played it, and Toetsen starts on Words): pressing/dragging the chart now plays it; hints on the chart when notes don't reach it or the Input source gets no audio; Orca vowels play with the layer off (like the g) and no longer hang; switching Klinker off mid-note fades out; the output's hard clip at 1.5 replaced by a soft ceiling at 1.0. SBTest checks all of it, plus the real Orca UDP socket; the installed AU/VST3 pass auval and a host check.
- "have a switch thats male voice to female voice" → Stem: Man / Vrouw. The words come from a second bank rendered with `tools/render.sh --female` (Ellen until Claire is installed); the vowel chart gets female formants and an octave higher default pitch.
- "shoulnt klinker also have the option to process loop and the granular though it?" → Bron (source) gains Loop, Wolk and Loop + Wolk: with Klinker on, that layer is heard through the vowel chart (glides, resonance, formant shift, the man / woman formants), at the same loudness; hints on the chart if the chosen layer is off. Tested in SBTest.

## spraakmaker

- 2026-11-15 — "make it, but no speaking back to it, also seperately make this module as a vst instrument and have it also able to speak dutch https://modulargrid.net/e/moddict-phonemer-vce26-s3"
- 2026-11-15 — "give Phonemer a dutch name" → Spraakmaker
- 2026-11-15 — "make both dark theme" → dark panel edition (only theme)
- 2026-11-15 — "make a pdf manual for both" → docs/Spraakmaker-manual.pdf (docs/manual.html source); fixed UTF-8 in default word lists
- 2026-11-15 — "for Spraakmaker make it the pixel girl from your renderings instead of the robot." → Sanne sprite on the OLED (Source/Sanne.h), mouth follows phonemes, blinks
- 2026-11-15 — "test the other speech plugin and make sure all aspects of it work too" → tests/SpraakFull (110 checks: every trigger mode incl. synced rates, retrigger, all six sources in EN and NL, lists, typing, CLASSIC poly, VOCODER with its own carriers and with external carrier / modulator inputs, VOSINGER chords, all tunings + a .scl file, every knob, every effect and delay time, LFO shapes, CV sources and every destination, macros, trg out modes, user presets, session state, all 100 slots). Fixed: (1) right after a change of source / language / list / preset a note could speak the previous source's words (the queue is now stamped with its settings); (2) typing + Enter could speak the next queued word instead of the typed text (it now waits for the typed text); (3) a trigger gave up after 0.5 s while the planner was refilling, now 3 s.

## spreek (the bundle)

- 2026-11-15 — "also check that the dutch and vocal synthesis moules work well and push those as a bundle to my github, homebrew. and make sure they all have pdf manuals and work on mac windoes and linux." → both built with JUCE 9.0.5, tests + auval + a DAW-style host check; one repo femifleming/spreek with mac (universal) / Windows / Linux CI releases, casks `spreekbuis` and `spraakmaker`; the macOS-only parts (AVSpeech renderer, Application Support paths) behind platform guards, a cross-platform renderer (SAPI / piper / espeak-ng) and the built-in Klatt voice as the fallback so Spreekbuis is never silent; the moddict wordmark and the Phonemer name removed from Spraakmaker (an homage, see README); PDF manuals regenerated and shipped in every zip
- 2026-11-15 — the Dutch voice should be "very high quality, not old tts dutch" → the renderer picks the best installed voice (Premium > Enhanced > compact); on this Mac only the compact Xander / Ellen are installed, so the bank is still the compact voice until Xander (Enhanced) / Claire (Premium) are downloaded in System Settings and render.sh --force is run
