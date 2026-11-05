# Kevy does Devy: setup guide

Alright! This is your own Ocean Machine for bass: two delays, a reverb and a looper, all tuned so your low end stays tight, wearing your skull. Here's how to get it running in Reaper with your Anagram. It takes about ten minutes.

## 1. Install it

1. Unzip `KevyDoesDevy-Windows.zip`.
2. Copy the whole `Kevy does Devy.vst3` folder into `C:\Program Files\Common Files\VST3\`. Windows will ask for permission. Say yes.
3. In Reaper: **Options > Preferences > Plug-ins > VST**, then click **Re-scan**.
4. On your bass track, click **FX** and search for **Kevy**. Add **VST3: Kevy does Devy**.

Nothing else needs installing. There's also a standalone version (`Kevy does Devy standalone.exe`) if you ever want to use it without Reaper.

## 2. Anagram as your audio interface (you probably have this already)

- The Anagram runs at **48 kHz**, so set Reaper to 48000 in **Options > Preferences > Audio > Device**.
- On Windows you need the **Darkglass USB Audio Driver** (from the Darkglass site, installed after a firmware update). Pick the Anagram as the audio device there.
- Set your bass track's input to the Anagram channel you record from, arm it and switch monitoring on.

## 3. Get the footswitches talking to the plugin

The plugin listens to the Anagram's USB MIDI by itself. It finds the Anagram by name, so you don't need a MIDI track.

**In Reaper, leave the Anagram's MIDI input switched off.** Windows only lets one program read a MIDI input at a time. Go to **Options > Preferences > Audio > MIDI Inputs**. If the Anagram says anything other than *disabled*, right-click it and choose **Disable input**.

Now open the plugin, click **PEDAL SETUP + EXTRAS** and look at **YOUR PEDAL**. It should say **Connected: (your Anagram)**.

### Try this first: Stomp mode

1. Long-press footswitch **C** on the Anagram until it's in **Stomp** mode.
2. Press **A**, **B** and **C**. If the skull blinks and the A, B and C badges light up, you're done.

### If nothing happens: scene messages

Since KosmOS 1.17 the Anagram can send MIDI messages when a scene is selected. We'll use three scenes that all sound the same, and each one tells the plugin which switch you pressed.

1. Open **Darkglass Suite** and pick the preset you play with.
2. Make sure it has three scenes with the **same sound** (copy the scene).
3. For each scene, add a **MIDI Out** message. These are the default messages the plugin listens for:
   - Scene 1: **Control Change 102**, value **127**
   - Scene 2: **Control Change 103**, value **127**
   - Scene 3: **Control Change 104**, value **127**
4. In the Anagram's MIDI settings, send MIDI Out over **USB** (or USB and 3.5 mm).
5. Put the Anagram in **Scene** mode (long-press C). Footswitches A, B and C now drive the plugin.

The menu names in Darkglass Suite may be worded a little differently from these. Look for the scene's MIDI Out messages.

### If you've set it up some other way: Teach my pedal

Click **TEACH MY PEDAL**, then press **A**, **B** and **C** when the skull tells you to. Whatever the Anagram sends gets learnt. Every switch also has its own **LEARN** button on the setup page.

### Plan B: MIDI through Reaper

If you'd rather route MIDI through Reaper:

1. Enable the Anagram's MIDI input in Reaper.
2. Make a new track called *Anagram MIDI*. Set its input to the Anagram's MIDI (all channels), then arm it and switch monitoring on.
3. Add a **send** from that track to your bass track. Set the send to **MIDI only** (Audio: none).
4. In the plugin's setup page, set **PEDAL** to **THROUGH THE DAW**.

## 4. What the footswitches do

It works like the real Ocean Machine. Your three switches are its three switches, and the **mode** decides their jobs. The current mode is shown on the panel.

| Mode | A | B | C |
|---|---|---|---|
| **PLAY** | Delay A on/off | Delay B on/off | Reverb on/off |
| | hold or double-tap: infinite | hold or double-tap: infinite | hold or double-tap: freeze |
| **TAP** | tap Delay A's time | tap Delay B's time | master tap tempo |
| **LOOPER** | reverse | half speed (double-tap: undo last dub) | rec, play, dub (double-tap: stop, hold: clear) |
| **PATCH** | preset down | preset up | back to PLAY |

To **change mode**, press two switches together:

- **A + B**: Looper
- **B + C**: Tap
- **A + C**: Patch
- **C + D** (four-switch pedals only): kill tails

The same pair again takes you back to Play. Some pedals use two-switch presses for their own bank changes and never send them. If a pair does nothing, open the setup page and watch **LAST MESSAGE** while you press it: that shows what, if anything, arrives. The mode tabs on screen always work.

Every job also has a button on screen.

**Hold vs double-tap.** In Scene mode the Anagram only sends a message when you press, not when you let go. So anywhere a hold is listed, a quick double-tap does the same job and latches it. Double-tap again to let go.

## 5. Bass-friendly bits

- **Your dry bass is never touched.** No filters or EQ on it, so your low end and attack stay exactly as you played them.
- **LOW CUT** on each delay and on the reverb keeps the repeats and tails out of your low end. Defaults are 120 Hz on the delays and 180 Hz on the reverb. Turn it down if you want more boom.
- **DUCK** pushes the repeats down while you play and lets them bloom in the gaps.
- **TONE** is a low-pass by default. Right-click it to make it a high-pass, like the pedal's hidden setting.
- **MK I / MK II** on each section switches between the grittier original Ocean Machine sound and the cleaner Ocean Machine II sound.

## 6. Handy extras

- **KILL TAILS** (the red button next to Bypass All) stops every repeat and reverb tail at once, and releases infinite and freeze. It's the way out of a runaway feedback howl. Your dry bass and the looper carry on.
- Each delay's **ON light flashes in time** with its delay time, and the **TAP** button pulses with the tempo, so you can see what you tapped.

- **Right-click any knob** for MIDI Learn, if you want a knob on a controller.
- **Presets**: click the preset name to pick from 30 (10 banks of 3). Banks 1 to 8 are the Ocean Machine II's own presets, bank 9 is bass picks and bank 10 is the original Ocean Machine sound. Click **SAVE** to store your own. They save with your Reaper project.
- The **EXPRESSION** box is ready if you ever plug a pedal into the Anagram's Exp jack. It's set to sweep feedback on both delays. Click **LEARN**, then **CALIBRATE**.
- Drag the bottom-right corner of the plugin window to make it bigger.

## If something's not right

- **"is in use by your DAW"** on the setup page: Reaper still has the Anagram's MIDI input enabled. Disable it, as in step 3.
- **A switch does nothing:** open the setup page and watch **LAST MESSAGE** as you press it. If it shows a message but says *not a switch*, teach it with **LEARN**. If nothing shows, the pedal isn't sending anything for that press.
- **A switch only works every other press:** press **TEACH MY PEDAL** and teach that switch again.
- **No sound from the effects:** check the sections are **ON**, **BYPASS ALL** is off, and the MIX knobs are up.

Have fun. Make it gross.
