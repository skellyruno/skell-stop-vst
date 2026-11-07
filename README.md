# StopDial v2.2 — JUCE VST3 + Standalone

v2.2 refines the vector UI and makes loop-pattern changes safer on audio:
bounded cubic interpolation is now used for captured/live loop slices, the
four-step gate has its own short de-click envelope, and Modern filtering updates
expensive cutoff math every 16 samples with an 8 ms coefficient slew. The main
dial now shows exact stop depth, plus contextual wheel, keyboard, and reset
controls. All existing APVTS parameter IDs and preset state keys are preserved.


StopDial is a vector-drawn stereo/mono tape-stop and rhythmic looping effect.
The plugin uses original Skell Audio branding, a 725 × 892 fixed-aspect-ratio
interface, and stable original APVTS parameter IDs for compatibility with
existing DAW sessions. Version 2.0 adds one automatable `resumeTrigger` bool
without renaming or removing existing parameters.

## Build

- **JUCE:** 7+ compatible design; CMake currently fetches JUCE **8.0.12** if
  `JUCE_PATH` is not set. The machine configuring CMake needs Git/network
  access to fetch JUCE, or a local JUCE checkout.
- **Windows:** MSVC via Visual Studio 2022/2026 (x64), CMake 3.22+, Windows SDK.
- **macOS:** Xcode/Clang, CMake 3.22+.

Windows PowerShell:

```powershell
cmake -S . -B build -A x64 -DJUCE_PATH="C:/JUCE"
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

macOS:

```sh
cmake -S . -B build -G Xcode -DJUCE_PATH="$HOME/JUCE"
cmake --build build --config Release --parallel 3
ctest --test-dir build -C Release --output-on-failure
```

If JUCE is not local, omit `-DJUCE_PATH=...` and configure with network access.
The GitHub Actions workflow produces Windows and macOS artifacts and runs the
native C++ regression tests after compilation.

Usual outputs:

| Platform | VST3 | Standalone |
| --- | --- | --- |
| Windows | `build/StopDial_artefacts/Release/VST3/StopDial.vst3` | `build/StopDial_artefacts/Release/Standalone/StopDial.exe` |
| macOS | `build/StopDial_artefacts/Release/VST3/StopDial.vst3` | `build/StopDial_artefacts/Release/Standalone/StopDial.app` |

The location can differ across JUCE generators; search `build` for `StopDial.vst3`.
Copy the VST3 bundle to `C:\Program Files\Common Files\VST3` on Windows,
or `~/Library/Audio/Plug-Ins/VST3` on macOS.

## Controls

- **New live-status strip:** LIVE INPUT / BRAKING / STOPPED / SPEED HOLD /
  LOOPING, the host BPM, and a **RESUME** control to return immediately to
  normal live playback. The new `resumeTrigger` parameter can also be automated.
  UI clicks use an audio-thread event latch so short clicks are not lost.
- **Green ring — stopAmount:** effect wet depth 0–100%; the new in-dial
  value display shows the exact amount. Drag the arc or use Up/Down/Left/Right
  while it has keyboard focus, double-click the ring to reset. Shift-wheel,
  Shift-drag, and Shift-arrow fine adjustment are supported.
- **Curve button — curve:** acceleration/braking shape, -100 to +100.
  Revised curve indicator and separate label; Up/Down arrows adjust when focused.
- **Stop square — stopTrigger:** a momentary APVTS bool, plus a UI-only event
  latch that catches quick mouse taps occurring between audio callbacks.
  Space or Enter on a focused STOP button also triggers a complete pulse.
- **Division label — syncDiv:** quarter-note subdivisions and dotted values;
  changing tempo while stopping changes the rate of phase advance, preserving
  the musical position. Division selection itself is latched at trigger.
- **Mode:** Classic (tape speed), Modern (tape speed with low-pass / saturation),
  or Loop (repeat a captured slice after braking). Focus the pills and use
  Left/Right or the 1, 2, 3 keys to switch modes.
- **Loop label — loopDiv:** 1/4, 1/8, dotted durations, and others.
- **Four green bars — loopPattern:** click any individual bar to toggle its
  corresponding beat; each change is automatable through the existing
  four-bit rhythm mask (0–15). Pattern 15 plays without rhythmic gain dips.
- **Padlock / percentage — lock and lockAmount:** toggles a non-zero final
  speed and adjusts that speed as a percentage. Drag or wheel the percentage
  to edit, Shift-wheel for fine control, or double-click it to reset to 14%.
- **Waveform green handles:** drag either edge of the selected slice; the
  waveform now highlights handles on hover and shows a moving orange tape/loop
  playhead. Double-click a handle to restore the original marker positions.
  A subtle waiting-for-audio label distinguishes preview peaks from input.
  Marker edits apply at the **next loop capture**, not to an already playing
  frozen slice, and are saved to the project state.

## Engine details

1. A 16-second per-channel, continuously updated history buffer keeps
   **fresh** audio available even when a frozen loop has been playing for a long
   time. Recording happens in `processBlock`; buffers are allocated in
   `prepareToPlay` only.
2. Variable-speed playback uses a shared stereo read cursor with bounded
   four-point Hermite interpolation; as of v2.2 that same bounded reader is
   used for frozen/live loop slices: cubic transients cannot overshoot the
   four source taps. The read cursor now starts a safe four frames behind the
   next write position so it cannot sample unwritten future taps. The slowdown
   duration is measured in beats with a block-rate update of host BPM so tempo
   changes preserve brake phase.
3. When the stop enters Loop mode, its source range is preserved using an
   independently allocated snapshot. At most **8 stereo frames** are copied
   per processed audio frame, spreading the copy over time instead of invoking
   a single long operation on the realtime thread. The capture range is from
   the always-live ring. The frozen clip then loops indefinitely, independent
   of the live recording buffer.
4. Loop-splice overlap uses smooth **linear** crossfade weights (which sum to
   unity). This avoids equal-power boosts of nearly correlated samples. Gate
   patterns fade where adjacent steps differ, followed by a shared 0.65 ms
   one-pole gate slew that prevents hard gain jumps if the four-bit pattern is
   automated mid-step. During Loop mode, subsequent BPM changes adjust
   playback rate and loop transport progress.
5. Retriggers, stop entry, resume, and mode changes use a short output
   crossfade. Classic and Modern fade to quiet at stop completion; Loop uses
   only the stop-to-loop handoff and **does not fade to zero first**. This
   removes a 12 ms double-fade dip from the older implementation. There is
   still no guarantee of click-free audio for arbitrary transient material.
6. With Lock enabled, the slowdown continues at the locked minimum speed.
   Because the live buffer is finite, a **50 ms automatic safety release to
   dry input** occurs just before its read material would be overwritten. The
   Lock toggle stays armed for the next trigger. To sustain an indefinite
   repetition, use Loop mode instead.
7. Lock-free SPSC peak FIFO drives the waveform, and an editor re-open flushes
   stale peaks rather than drawing seconds-old audio. The audio reset no
   longer resets FIFO counters while an open editor might still be consuming,
   avoiding an avoidable data race on sample-rate reconfiguration.
8. **Modern** blends in two cascaded low-pass stages as the stop slows,
   uses speed-dependent, gain-compensated soft saturation, and includes a DC
   blocker. It now avoids costly saturation at full tape speed. The shared
   stereo cutoff coefficient updates its `pow/exp` calculations every 16
   samples, then smooths over 8 ms at the audio rate to avoid zipper noise.
   DSP states are reset on retrigger; the same components are covered by
   the dependency-free native tests.
9. When fully **LIVE**, the audio callback takes a lightweight dry-through
   path: no tape interpolation, exponential coefficients, waveshaping or phase
   math, while preserving live ring capture and waveform metering.

The host-reported latency is **zero samples** (no look-ahead and no reported
DSP delay). The intentional variable-speed wet read starts with a few frames
of recent recorded audio. Tape braking is
an intentional playback effect, not a conventional delay. Source history is
limited to 16 seconds; a captured loop is limited to 9 seconds at 20 BPM.
The infinite looping may require the host to continue processing after source
input ends; hosts differ in how they interpret a plugin's zero tail report.
There is no production preset browser or embedded font.

## Verification

Run the separate dependency-free C++ tests even before JUCE is installed:

```sh
clang++ -std=c++17 -Wall -Wextra -Werror -O2 \
  tests/stopdial_math_tests.cpp -o StopDialDSPTests
./StopDialDSPTests
python3 tests/check_math.py
```

With Clang address and undefined-behavior sanitizers:

```sh
clang++ -std=c++17 -Wall -Wextra -Werror -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  tests/stopdial_math_tests.cpp -o StopDialDSPTests
./StopDialDSPTests
```

These checks cover the C++ maths for curves, causal tape indexing,
transient-bounded Hermite interpolation, the **same Modern filter code used
by the plugin**, loop crossfades, rhythm masks, tempo timing, wrapped Hermite
guards, minimum-slice bounds, in-block loop period changes, and bounded
incremental snapshots. C++ assertions remain active in Release (`-DNDEBUG`) builds.
They are **not** proof that the JUCE plugin compiles, nor do they replace audio
rendering in FL Studio, Ableton, or Reaper.

### Quick offline CMake tests (no JUCE SDK required)

```sh
cmake -S . -B build-math -DSTOPDIAL_MATH_ONLY=ON
cmake --build build-math --parallel
ctest --test-dir build-math --output-on-failure
```

On Linux, add `-DSTOPDIAL_ENABLE_SANITIZERS=ON` for AddressSanitizer
and UndefinedBehaviorSanitizer. A dedicated Linux CI job now runs these tests
before Windows/macOS plugin builds.

### Manual DAW tests still required

- Windows MSVC build, VST3 load and unload, project save/reopen.
- Silent / sine / transient / full-mix recordings at 44.1, 48, 96 kHz;
  mono and stereo; block sizes 32/128/512/2048.
- Fast stop-trigger taps, DAW automation, bypass, repeated retriggers, and
  mode switching while output is audible.
- BPM changes during both slowing and looping; low BPM and dotted divisions.
- Long (>20s) loop playback and repeated retriggers from currently playing
  material; Lock safety release with long slowdowns.
- Reported latency, offline bounce tails, CPU profile and memory at 192 kHz.

**Validation for v2.0:** native math/DSP tests passed using GCC -O2 with
`-DNDEBUG`, Clang -O1 with ASan and UBSan, and CMake's standalone DSP-only
CTest configuration. Python timing/bounds checks also passed. These are
real tests of the dependency-free helpers, **not a full plugin or listening
validation**. JUCE was not installed in this environment and external DNS
could not fetch it; full JUCE compilation, FL Studio loading and audio-render
quality remain unverified.

## What changed in v2.0 (versus v1.3)

- **Live/resume control:** new vector status strip displays effect state, BPM
  and a clickable return-to-live action. New automatable `resumeTrigger` bool
  retains existing ID/state mappings for every original parameter.
- **Modern DSP:** shared, tested two-stage low-pass, stronger speed-dependent
  warmth, level-compensated saturation, DC removal, and cleared stale tone
  history at retrigger.
- **Cubic safety:** causal, four-frame tape-read margin and Hermite output
  bounded to source extrema around transients.
- **Stop/loop continuity:** no unnecessary fade-to-zero directly before the
  handoff crossfade when Loop mode takes over.
- **CPU:** minimal-cost live pass-through path with continuous recording and
  waveform metering; avoids unnecessary filter calculations in Classic/Loop.
- **GUI:** editable individual rhythm bars, waveform playhead and clearer
  drag handles, improved hover/cursor feedback, tooltips, and corrected
  screenshot-style charcoal header contrast.
- **Validation:** a Linux sanitizer CI job and offline `STOPDIAL_MATH_ONLY`
  CMake mode with regressions for the new DSP logic.

### Known limits

The current loop marker positions are saved but are **not additional
DAW-automatable parameters**. Moving them prepares the *next* loop capture;
it does not recut a frozen clip already playing. The zero reported tail may
cause some hosts to suspend infinite loop playback after input ends. Infinite
Lock playback is deliberately not offered with a finite history buffer; its
safety fallback resumes dry signal. Host-specific CPU/GUI performance and
listening quality have not been measured. The interface uses the operating
system's sans-serif font (no bundled font assets).

## What changed in v1.3 (versus v1.2)

- **Stop-to-loop handoff:** transitions from the slowing tape read head to a
  captured loop through a 6 ms continuity crossfade instead of hard switching.
  Removed the extra loop-entry fade that could create an audible gain dip.
- **Hermite loop boundaries:** snapshot guards now wrap within the selected
  loop instead of reading surrounding live audio; provisional live reads use
  the same periodic indexing, avoiding a phase jump when copying completes.
- **Very short loop regions:** constrained the minimum 16-frame slice to
  already captured history, even when the markers are near the write head.
- **Mid-block stop completion:** updates loop length immediately within the
  same process block, rather than running the newly captured loop through the
  previous clip's duration until the next host block.
- **Invalid audio protection:** replaces non-finite upstream samples with
  silence before storing them in the circular buffer / Modern filter path.
- **Stronger test integrity:** Release builds now execute assertions; added
  periodic-guard, short-slice, crossfade and in-block loop-period tests.

All original APVTS parameter IDs and editor state keys were preserved.
The optional `resumeTrigger` parameter was added in v2.0. No bitmap dependency.

## What changed in v1.2 (versus v1.1)

- Tempo-following stop progress instead of a fixed sample duration at the
  moment of trigger; tempo-dependent Loop playback after capture.
- A continuously recording history and incrementally copied frozen loop,
  allowing retriggers from recent incoming audio after a long held loop.
- Correct unity gain in fully active loop-pattern mode and a DC-safe smooth
  splice at the repeat boundary.
- Short crossfades on retriggers and mode changes; physical minimum-speed Lock
  continuation and a bounded-buffer safety release.
- Corrected WaveformView hit testing so it no longer intercepts padlock/amount
  control clicks; cleaned stale peak FIFO on editor opening.
- CMake CTest target for standalone C++ DSP math tests, and CI test steps.

### Optional CPU microbenchmark

```sh
cmake -S . -B build-bench -DSTOPDIAL_MATH_ONLY=ON -DSTOPDIAL_PROFILE_DSP=ON
cmake --build build-bench --parallel
./build-bench/StopDialModernBenchmark
```

This times only the shared Modern filter-coefficient updates, comparing the
v2.1 per-frame calculation against the decimated + smoothed v2.2 version.
On the development Linux container (GCC -O3, 3 million frames), the new
coefficient path measured about **2.4× faster** (54.5 ms vs 23.1 ms).
This is **not** an overall plugin CPU figure; the effect also performs cubic
sampling, filtering, saturation, loop copying, GUI rendering, and DAW I/O.
Re-run the benchmark on the target system before interpreting its speed.
