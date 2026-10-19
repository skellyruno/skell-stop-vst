# StopDial — JUCE VST3 + Standalone

Original, vector-only tape-stop/tempo-loop audio effect. Developed from the uploaded
Skell quickstart build template, not dependent on any third-party UI assets or marks.

## Build locally

Requires CMake 3.22+, a C++17 compiler, JUCE 7 or later, and VST3 build dependencies.
By default CMake downloads JUCE 8.0.12, so a network connection is needed during
first configuration; alternatively use `-DJUCE_PATH=...`.

### Windows — Visual Studio 2022/2026, MSVC x64

```powershell
cmake -S . -B build -A x64 -DJUCE_PATH="C:/JUCE"
cmake --build build --config Release --parallel 2
```

### macOS — Xcode/Clang

```bash
cmake -S . -B build -G Xcode -DJUCE_PATH="$HOME/JUCE"
cmake --build build --config Release --parallel 3
```

Typical outputs (JUCE 7/8 convention):

- Windows: `build/StopDial_artefacts/Release/VST3/StopDial.vst3` and
  `build/StopDial_artefacts/Release/Standalone/StopDial.exe`.
- macOS: `build/StopDial_artefacts/Release/VST3/StopDial.vst3` and
  `build/StopDial_artefacts/Release/Standalone/StopDial.app`.

The exact location can vary by generator. Search the build folder for `StopDial.vst3`.
Copy `.vst3` to the usual VST3 location, e.g. `C:\Program Files\Common Files\VST3`
or `~/Library/Audio/Plug-Ins/VST3` on macOS. The standalone build is useful
for testing without a DAW. A disconnected build host should set `JUCE_PATH`.

## Automated builds from GitHub

Upload everything in this folder to a GitHub repo and run **Actions → Build StopDial**.
Both Windows and macOS workflows compile VST3 and standalone and upload ZIP artifacts.

## How it works

- **Main ring:** effect depth (0–100%). Clicking/draging its outside arc changes depth,
  wheel edits it, shift is fine-adjust, double-click resets to 100%.
- **Curve:** changes when braking becomes strong; negative curves wait longer to brake.
- **Stop button:** momentary trigger. Tap the square to start a full time-stop;
  holding is not required. Clicking the `1/4` label opens tempo division choices.
- **Classic:** interpolated variable-speed playback.
- **Modern:** same brake with interpolated lowpass cutoff sweep and soft saturation.
- **Loop:** after the brake, freezes an input slice and keeps looping it.
  Loop length is set by the `1/8.` label; the four bars select one of 16 rhythmic
  gate patterns. Marker positions constrain how much of the selected slice loops.
- **Lock icon:** toggles brake lock. When on, `lockAmount` sets the lowest speed during
  braking; when off the brake naturally reaches speed zero. Drag the percentage vertically.
- **Waveform:** 60 Hz peak view from a lock-free single-producer/single-consumer FIFO.
  The two markers are draggable and persist in saved state.

### Engineering notes

The circular history stores ~8 s of audio per stereo channel and is allocated only
in `prepareToPlay`. A trigger begins reading recent input with 4-point Hermite
interpolation; continuous input capture proceeds during braking. At the stop point,
input recording freezes so loop playback remains stable indefinitely. The effect uses
true wet/dry mixing; depth 100% produces full stop, while depth 0% is bypass.
The host tempo is retrieved from AudioPlayHead with a 120 BPM fallback. Reported host
latency is **0 samples**, as there is no lookahead; intentional time stretching and
modulation are not latency compensation. The first wet samples use the latest history,
so the dry/wet crossfade guards the onset.

**Limits:** The input history is fixed to 8 s; extremely long musical divisions at
low tempos are not guaranteed to retain all material. Zero-latency tape-stop cannot
play *future* incoming audio; audio is captured while braking. A hard retrigger starts
from the current buffer position. Pattern gate playback is a creative rhythmic effect,
not a phase-locked reconstruction of a whole DAW measure. There is no preset browser,
only a parameter reset and about popup. Font typography uses the host's system sans,
not an embedded third-party font; all marks and shapes are drawn with JUCE paths.

## Testing guidance

1. In standalone, run a vocal or sustained-note input and tap Stop.
2. Compare `Classic`, `Modern` and `Loop` at 120 BPM and 90 BPM.
3. Automate stopAmount, curve, and syncDiv in a DAW, then save/reopen the project.
4. Check the Standalone and VST3 plugin at 44.1, 48, 96 kHz, mono/stereo, and
   block sizes 32/128/512/2048 samples.
5. Verify no audio allocations or locks in the audio callback; inspect with an
   RT-safety profiler in the target host.

**Verification:** Source generated and statically checked in the authoring environment.
There was no JUCE installation or GitHub network access available, so a full compile
and DAW runtime test could not be performed here. Use the CI workflow or local JUCE
checkout to confirm compilation.
