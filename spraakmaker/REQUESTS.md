# Requests

- 2026-11-15 — "make it, but no speaking back to it, also seperately make this module as a vst instrument and have it also able to speak dutch https://modulargrid.net/e/moddict-phonemer-vce26-s3"
- 2026-11-15 — "give Phonemer a dutch name" → Spraakmaker
- 2026-11-15 — "make both dark theme" → dark panel edition (only theme)
- 2026-11-15 — "make a pdf manual for both" → docs/Spraakmaker-manual.pdf (docs/manual.html source); fixed UTF-8 in default word lists
- 2026-11-15 — "for Spraakmaker make it the pixel girl from your renderings instead of the robot." → Sanne sprite on the OLED (Source/Sanne.h), mouth follows phonemes, blinks
- 2026-11-15 — "test the other speech plugin and make sure all aspects of it work too" → tests/SpraakFull (110 checks: every trigger mode incl. synced rates, retrigger, all six sources in EN and NL, lists, typing, CLASSIC poly, VOCODER with its own carriers and with external carrier / modulator inputs, VOSINGER chords, all tunings + a .scl file, every knob, every effect and delay time, LFO shapes, CV sources and every destination, macros, trg out modes, user presets, session state, all 100 slots). Fixed: (1) right after a change of source / language / list / preset a note could speak the previous source's words (the queue is now stamped with its settings); (2) typing + Enter could speak the next queued word instead of the typed text (it now waits for the typed text); (3) a trigger gave up after 0.5 s while the planner was refilling, now 3 s.
