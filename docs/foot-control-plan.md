# Foot control plan

Kevy does Devy copies the Ocean Machine's own three-switch layout and its modes, so a three-switch pedal like the Darkglass Anagram covers every job. The plugin's text never names the brand. Every job also has a button on screen.

## What each switch listens for (defaults)

| Switch | Listens for | Why |
|---|---|---|
| A | CC 17 or CC 102 (any channel), or CC 20 on ch 16 | Stomp-switch CC (Plan A), scene message (Plan B), house default |
| B | CC 18 or CC 103 (any channel), or CC 21 on ch 16 | as above |
| C | CC 19 or CC 104 (any channel), or CC 22 on ch 16 | as above |
| D | CC 23 on ch 16 | four-switch pedals: next mode |
| Expression | CC 11, any channel | house default |

The plugin works out how each switch behaves from what it sends:

- **Momentary switches** send a press and a release, so they get real holds.
- **Toggling and one-shot switches** (scene messages, 1/127 toggles) count every message as a press, so they use a double-tap wherever a hold is listed.
- **Program changes** and **notes** also work as switches, through Teach or Learn.

The plugin reads the footswitch's MIDI device directly, using Prism Patch's `DirectMidiInput`. It finds Anagram and Darkglass devices by name as well as the usual footswitch names. A MIDI track routed through the DAW still works as a fallback.

## Jobs per mode

| Mode | A | B | C | D |
|---|---|---|---|---|
| Play | Delay A on/off; hold or 2x: infinite | Delay B on/off; hold or 2x: infinite | Reverb on/off; hold or 2x: freeze | next mode |
| Tap | tap Delay A | tap Delay B | master tap | next mode |
| Looper | reverse | half speed; 2x: undo/redo last dub | rec/play/dub; 2x: stop; hold: clear | next mode |
| Patch | preset down | preset up | back to Play | next mode |

| Combo | Does |
|---|---|
| A+B | Looper mode (the pedal's own combo); again returns to Play |
| B+C | Tap mode; again returns to Play |
| A+C | Patch mode; again returns to Play |

## Differences from the house rules

This one-off plugin is for a friend, so these house rules are dropped:

- Keeper
- the Portability section and the Linux ARM64 build
- the Flip Fella look and Pebble

Pedal settings live in `%APPDATA%\Kevy does Devy\Pedals.xml`, apart from the shared Flip Fella file, because the bindings differ.

## Open questions

- It hasn't been confirmed whether the Anagram's Stomp-mode switches send CC out over USB (Plan A).
- If pressing the active scene again doesn't resend its message, Plan B needs alternating scenes, or Teach can learn something else.
- The original pedal's own MIDI CC chart couldn't be read (the manual hosts were blocked), so it isn't included yet.
