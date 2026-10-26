# Knockbox

**90s NY boom bap** plugins by 8cee. Windows VST3. Two plugins:

- **Knockbox Track** goes on each track (kick, snare, sample, bass).
- **Knockbox** goes last on your master bus.

## Knockbox Track (channel strip)

| Section | Controls | What it does |
|---|---|---|
| **EQ** | 4 bands: On, Type, Freq, Gain, Q | Q4-style EQ. Each band can be Low Cut, Low Shelf, Bell, High Shelf or High Cut. Drag the numbered points on the graph, scroll over a point to change its width, double-click to reset its gain. |
| **Knock** | Knock, Freq, Punch, Snap, Snap Hz | Body bump tuned to the sound (kick ~60–90 Hz, snare ~180–220 Hz) with a mud cut, transient Punch, and Snap for the crack. |
| **Tape / Out** | Input, Tape, Output | Tape saturation on the single sound, plus gain in and out. |

**Presets (16):**
- *Drums:* Kick Knock, Kick Sub Boom, Snare Crack, Snare Fat Body, Rimshot Click, Hats Tame, Open Hat Air, Breakbeat Loop
- *Samples & bass:* Dusty Sample, Jazz Keys Loop, Horn Stabs, Bass Weight, Filtered Bassline
- *Vocals:* Vocal Chops, Telephone Vocal
- *Start from scratch:* Clean
On **Clean** the plugin does nothing except a 30 Hz low cut, so you can build from scratch.

## Knockbox (master bus)

| Stage | Controls | What it does |
|---|---|---|
| **Knock** | Knock, Freq, Punch, Snap | Low-end bump tuned to your kick (Freq), mud cut around 320 Hz, transient punch so kicks and snares jump out, and a 2.5 kHz crack for the snare. The dot next to the title flashes when Punch is working. |
| **Tape** | Tape, Input | Warm, slightly asymmetric tape saturation with a low head bump and a soft top. Input drives the whole chain. |
| **Glue** | Thresh, Ratio, Attack, Release, Mix, Makeup, SC HPF | Stereo-linked bus compressor. **Mix** blends it in parallel (NY compression), so the beat pumps without losing the knock. SC HPF stops the kick from over-triggering it. |
| **Dust** | Dust, Vinyl | Rolls off the digital top end; Vinyl adds crackle and hiss. |
| **Out** | Output, Ceiling, Clip, Mono, Mono Hz | Output gain into a clipper at the Ceiling (red line on the meter). Mono keeps everything below Mono Hz in the centre. |

**Presets (16):**
- *All-rounders:* Default Knock, Gentle Polish, Tight & Snappy, Radio Ready
- *Dusty / lo-fi:* Dusty Basement, Jazz Loop Warmth, Late Night Mellow, Crunchy Tape, Demo Tape
- *Low end:* Thick Low End, Heavy Kick Room, Filtered Bassline
- *Compression:* Parallel Squash, Slow Pump, Hard Snare Crack, Loud & Clipped

Double-click any knob to reset it.

## Get the Windows plugins

Every push to GitHub builds and tests both plugins automatically (about 20–30 minutes):

1. Open the repository on github.com and click the **Actions** tab.
2. Click the newest run of **Build Windows VST3**. Wait until it has a green tick.
3. Scroll down to **Artifacts** and download **Knockbox-Windows-VST3**.
4. Unzip it and follow `INSTALL.txt`.

To start a build by hand: Actions > Build Windows VST3 > **Run workflow**.

The project also contains a `.gitlab-ci.yml`, so a copy on GitLab builds the same way
(Build > Pipelines > job > Download artifacts). Keeping both is a good backup.

## What every build checks

- DSP tests at 44.1 / 48 / 96 kHz: no NaNs, ceiling holds, transparent at neutral settings, same output for any buffer size, Punch really adds punch, EQ gains match the graph.
- Click test: flipping switches, band types, tape and dust while a tone plays makes no pops.
- Loads each real `.vst3` like a DAW: scan, process, random automation, every preset, save/restore, editor.
- Neither plugin needs the Visual C++ Redistributable installed.
- pluginval at strictness 10.

Also checked before release (on Linux): no memory allocation on the audio thread, and a clean run under
address/undefined-behaviour sanitizers.

## Don't lose it again

Keep this zip somewhere safe (PC + external drive or cloud folder), and download each finished build.
GitLab can also mirror to a second free host (Settings > Repository > Mirroring repositories), e.g. Codeberg.

## Build it yourself (optional)

Visual Studio 2022 + CMake:
```
cmake -B build -A x64
cmake --build build --config Release
```
