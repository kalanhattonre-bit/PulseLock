# PulseLock

PulseLock is a VST3 effect for Cubase on Windows. Draw a shape and it plays in time with your song on
the track's **volume**, a **filter**, and **pan**: trance gates, sidechain pumping, filter risers,
auto-pan. There are **eight patterns (1 to 8)**, and a MIDI keyboard switches between them live.

---

## 1. Download the latest build

**Easiest: the Releases page**

1. Open <https://github.com/kalanhattonre-bit/PulseLock/releases>.
2. Under the newest release, download `PulseLock-v…-Windows-x64.zip`.
   (If the page shows no releases yet, use the Actions tab below.)

**Newest test build: the Actions tab** (you need to be signed in to GitHub)

1. Open <https://github.com/kalanhattonre-bit/PulseLock/actions>.
2. Click the top run that has a green tick.
3. Scroll to **Artifacts** and click **PulseLock-VST3-Windows**.

## 2. Install it

1. Right-click the zip and choose **Extract All…**.
2. Copy the **`PulseLock.vst3`** folder into **`C:\Program Files\Common Files\VST3`**. Windows asks for
   permission; click **Continue**.
3. In Cubase: **Studio → VST Plug-in Manager → Rescan All** (or restart Cubase). PulseLock appears under
   **Kalan Hatton**.

## 3. Use it in Cubase

1. **Insert PulseLock on an audio track** (or a group or FX channel) and press play. Pattern 1, a 16th
   gate, starts chopping in time.
2. **Optional: switch patterns from the keyboard.** Create a MIDI track, set its **output** to
   **PulseLock**, and record-enable or monitor it. Keys **C1 to G1** pick patterns **1 to 8** (Cubase's
   note names, where middle C is C3). The keyboard strip along the bottom numbers those keys. Record those notes into a MIDI part to switch patterns as the song
   plays.
3. Click a pattern number to edit that pattern, and click **VOLUME**, **FILTER** or **PAN** to edit that lane.

## 4. Drawing

| Do this | And this happens |
| --- | --- |
| Click on empty space | Adds a point; keep holding to drag it into place. |
| Drag a point | Moves it. It snaps to the grid; hold **Shift** to place it freely. |
| Double-click or right-click a point | Deletes it. The first and last points always stay. |
| Drag a small **diamond**, or **Alt**-drag a slope | Bends that segment into a curve. |
| Put two points on the same grid line | Makes a hard vertical edge, like a gate. |

The tool strip under the drawing:

| Button | What it does |
| --- | --- |
| 4 / 8 / 16 / 32 | Grid: how many divisions the pattern has. |
| SNAP | Points snap to the grid. |
| Gate, Pump, Ramp up, Ramp down, Triangle, Random, Flat, Invert | Replace the lane you are editing with that shape, sized to the grid. |
| COPY / PASTE | Copy a whole pattern (all three lanes) and paste it over another. |
| UNDO / REDO | Step back and forward through your drawing. |

## 5. Controls

**Pattern bar**: **1 to 8** pick the pattern to edit and play. The **filled** number is playing, the
**outlined** one is being edited, and a **dashed** one is waiting for the next beat or bar. Each shows the
key that switches to it. **VOLUME / FILTER / PAN**: click the name to draw that lane; click its **light** to
switch the lane on or off.

| Control | What it does |
| --- | --- |
| Length | How long one pass of the pattern is, in time with Cubase (1/32 up to 4 bars, T = triplet), or Free. |
| Trigger | **Song**: follows the song position. **Retrigger**: every MIDI note restarts the pattern. **One-shot**: every note plays it once, then it rests at its end. |
| Switch | When a pattern key takes effect: **Instant**, **Next beat**, or **Next bar**. |
| Rate | Speed in Hz, used only when Length is Free. |
| Keys | Which eight keys switch patterns (default C1 to G1). |
| Depth: Volume | How far the Volume lane can pull the level down. |
| Filter: Type | Low-pass, high-pass or band-pass. |
| Filter: Low / High | The cutoff at the bottom and at the top of the Filter lane. |
| Filter: Res | Resonance: a peak at the cutoff. |
| Depth: Pan | How far the Pan lane can move the sound left and right. |
| Smooth | Rounds off hard edges so gates don't click (a few milliseconds). |
| Mix | Blend of the original and the processed sound. |
| Gain | Output level. |

**Knobs**: hover to see the value, drag up or down to change it, hold **Ctrl** for fine steps,
double-click to reset.

## 6. Factory presets

Presets set the knobs only; they never overwrite your drawings.

| Preset | Sound |
| --- | --- |
| Trance Gate | Pattern 1's 16th gate, one bar. |
| Sidechain Pump | Pattern 2: dips on every beat and swells back. |
| Filter Riser | Pattern 5: a low-pass sweep that opens over four bars. |
| Auto-Pan | Pattern 1's pan wave every half bar. |
| Stutter Keys | Every key restarts pattern 7's stutter. Play it like a sampler. |
| 3-3-2 Groove | Pattern 3's 3-3-2 rhythm through a moving filter, with a little pan. |
| Random Steps | Pattern 6's random steps on volume, a band-pass filter and pan. |

## 7. Something wrong? Tell me

Open an issue at <https://github.com/kalanhattonre-bit/PulseLock/issues/new> with:

- your **Cubase version** (Help → About Cubase),
- your **sample rate**, **buffer size** and the song's **tempo**,
- which **PulseLock version** you installed,
- **what you heard**, and what you expected to hear. Say which pattern and settings you used.

---

## Licences and credits

- Built with [JUCE](https://juce.com). JUCE has its own licence terms (a free open-source AGPLv3 option and
  commercial licences). **Check JUCE's licence before you share or sell builds of this plugin.**
- VST is a registered trademark of Steinberg Media Technologies GmbH.

## For developers

Builds need CMake 3.22+ and a C++20 compiler; JUCE 8.0.15 is fetched automatically.

```bash
cmake -S . -B build -DPULSELOCK_BUILD_TESTS=ON
cmake --build build --config Release
build/PulseLockTests_artefacts/Release/PulseLockTests
```

The test runner checks gate timing against the song position, note triggers, pattern switching, click-free
edges, filter and pan response, bit-exact pass-through, state and undo, allocation-free processing and the
presets. CI builds on Windows and Linux, validates the VST3 with pluginval at strictness 10, renders
interface snapshots, and attaches a zip to a GitHub release for every `v*` tag.
