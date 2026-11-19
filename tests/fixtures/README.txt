States written by the released 1.0.0 plug-ins (installed from the spreek v1.0.0 macOS zip), saved in REAPER 7.83 projects
on 2026-11-19 and cut out of the project files:
  spreekbuis-1.0.0-vst3-a.state      Spreekbuis.vst3 1.0.0: every layer on, the woman's voice, notes on everything
  spreekbuis-1.0.0-vst3-a.hostchunk  the same as REAPER stored it (the VST3 chunk around the state)
  spreekbuis-1.0.0-vst3-b.state      Spreekbuis.vst3 1.0.0: a fresh instance with the Orca bridge switched off
  spreekbuis-1.0.0-au-a.state        Spreekbuis.component 1.0.0: sentences as a dialog loop, the vowel chart on noise
  spreekbuis-1.0.0-au-a.aupreset     the same as the Audio Unit hands it to a host (a property list, as in an .aupreset)
  spraakmaker-1.0.0-vst3-a.state     Spraakmaker.vst3 1.0.0: list 4, VOSINGER, three voices, LFO1 on the formant, just tuning
  spraakmaker-1.0.0-vst3-a.hostchunk the same as REAPER stored it
tests/SpreekTest.cpp loads them: old sessions open unchanged, Spraakmaker states import.
