# Kevy does Devy

A Windows VST3 for bass, recreating both Mooer Ocean Machines (the 2018 original and the Ocean Machine II): two delays with 9 types each, a reverb with 9 types plus shimmer, a 120 second stereo looper, four routings, 30 presets (the Ocean Machine II's factory banks plus bass and MK I banks), tap tempo and footswitch control built around a three-switch pedal such as the Darkglass Anagram. It's styled after the friend's own artwork, with his big-eyed skull as the mascot.

**Download:** [KevyDoesDevy-Windows.zip](https://github.com/kevinwkaye/kevydoesdevy/raw/builds/KevyDoesDevy-Windows.zip), built by GitHub Actions on every push. It contains the VST3, a standalone version and the setup guide. Nothing else needs installing.

- [Setup guide](docs/SETUP-GUIDE.md): Reaper and the Anagram
- [Test checklist](docs/TEST-CHECKLIST.md)
- [Foot control plan](docs/foot-control-plan.md)

## How it covers both pedals

The plugin has the Ocean Machine II's feature set. Each section has a MK I / MK II voicing switch: MK I is the original's grittier, narrower converter colour with grainier pitch shifting, and MK II is cleaner and wider. Bank 8 holds MK I versions of the main sounds.

## Bass

The dry signal passes through untouched. Every delay repeat and reverb tail goes through a low cut inside the feedback loop, so self-oscillation rises as a pitch rather than a rumble. Pitch effects never shift anything under 150 Hz, there's ducking on both delays, and the looper's half speed has a sub guard.

## Building

```
cmake -S . -B build
cmake --build build --config Release
```

JUCE 8.0.4 is downloaded automatically, or pass `-DKDD_JUCE_PATH=...` to use a local copy. `kdd_render_test` renders a bass line through every type, routing and looper action, and checks for NaNs, runaway levels and silence.

## Credits

- Skull artwork by the friend this was made for, used with permission. The rest of the panel is drawn in his style.
- Fonts: Titan One and Archivo Black (SIL Open Font License, `Resources/OFL.txt`), Permanent Marker (Apache 2.0).
- Footswitch handling adapted from Prism Patch's `PedalControl`.
- Not affiliated with Mooer, Devin Townsend or Darkglass.
