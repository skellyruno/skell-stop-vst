# StopDial changelog

## 2.3.1 — Cross-platform render and warning fixes

- Release offscreen JUCE graphics contexts before reading pixels on Windows.
- Keep original UI pixel assertions and save full/isolated renders at three sizes.
- Resolve shadowed editor identifiers, accidental float-to-integer text rectangles,
  and implicit numerical conversions in controls and DSP.
- Enforce strict, warning-free native C++ regression builds on Clang/GCC/MSVC.
- Correct the host test's conflicting JUCE standalone macro at target creation,
  and explicitly convert the remaining UI integer coordinates to floats.
- Remove an unreadable waveform annotation under the lock crescent and use a
  more restrained loop-region tint.


## 2.3.0 — Host-level DSP/UI verification and precision UX

- Add a JUCE-linked host integration test that runs the actual processor across sample
  rates and block sizes, including stop, looping, pattern automation, resume,
  malformed upstream samples, zero-latency and state round-trip checks.
- Render the actual vector editor at minimum, default and maximum sizes and check
  child bounds, menu hit-testing and visible colour output. Save PNG proof images.
- Prime the loop gate from the first rhythm step instead of full gain;
  a completely muted pattern no longer emits an initial capture burst.
- Include a measured 48 kHz Modern-mode callback CPU budget check (reports
  actual callback time versus real-time block duration).
- Improve loop-step hover affordances, discoverability of loop length editing and
  keyboard accessibility (+/- or up/down to change loop division).
- Preserve existing parameter IDs, audio modes, saved-state compatibility and
  dependency-free sanitizer tests.


## 2.2.0 — Interface clarity, smoother loops, lower DSP overhead

### Sound and real-time performance
- Smooth arbitrary four-step rhythm mask automation through a short 0.65 ms de-click gate without introducing reported latency.
- Bound cubic Hermite overshoot in both live-loop playback and frozen captured clips, not just the slowing tape reader.
- Update Modern's expensive cutoff `pow/exp` math every 16 frames, slewing the shared stereo coefficient at the audio rate over about 8 ms.
- Avoid evaluating `tanh` for Modern-mode processing at full tape speed while keeping filter/DC history warm.
- Add native regression coverage for gate automation, coefficient bounds, cutoff settling, malformed host values, and loop interpolation.
- Add an optional dependency-free Modern coefficient microbenchmark (`STOPDIAL_PROFILE_DSP=ON`).

### Vector UI and controls
- New legible green STOP DEPTH numerical display within the dial.
- Restored the white curve dot to the beginning of its green path; added small CURVE / STOP LENGTH legends.
- Waveform monitor distinguishes design preview from real input with an explicit status label.
- Lock-speed percentage supports its own wheel gestures and double-click reset to 14%; dial hover now clears on mouse exit.
- Keyboard focus and controls: Stop via Space/Enter, curve and stop depth via arrow keys, mode pills via Left/Right or 1–3; focus outlines remain vector-only.
- Preserve the original 725×892 fixed-aspect layout, green/charcoal palette, APVTS IDs, and saved loop-marker keys.

## 2.1.0 — Safety and transparency

- Detect exhausted tape history during both active braking and speed hold.
- Complete a bounded 50 ms fade to unprocessed input before data can be overwritten.
- Prioritise history-safety release over normal stop/loop handoff to prevent stuck attenuation.
- Preserve the original signal through Modern mode at full speed (no unwanted high-pass).
- Refresh waveform loop markers after preset loads, even while transport is idle.
- Add regression tests for safety threshold, fade guard and full-speed tone transparency.

## 2.0.0 — UI + DSP upgrade

### Sound engine
- Four-frame causal reader start to prevent Hermite sampling beyond recorded history.
- Transient-bounded cubic interpolation on variable-speed playback.
- Shared `ModernTone` class: dual-stage speed-sensitive low-pass, compensated soft saturation and DC blocking.
- Tone state primed on retrigger, reducing previous-state leakage.
- Removed the extra fade-to-zero ahead of Loop mode's stop-to-loop handoff.
- New `resumeTrigger` parameter and lock-free one-shot UI resume request.
- Fast fully-live dry-input path with history capture and waveform peak metering intact.
- Avoids expensive curve math when playback is already frozen/looping.
- Avoids resetting SPSC waveform FIFO counters during audio-device reconfiguration.

### Interface
- Compact vector status strip: LIVE INPUT / BRAKING / STOPPED / SPEED HOLD / LOOPING, detected BPM, RESUME.
- Moving orange transport playhead over the waveform.
- Larger drag-handle affordances, hover highlights, and double-click marker reset.
- Individual four-step rhythm bars toggle bits instead of cycling all 16 patterns by a single click.
- Improved pointer cursors and parameter tooltips.
- Darker screenshot-style curved header, charcoal backdrop and footer.

### Build / reliability
- Standalone no-JUCE CMake DSP test configuration via `-DSTOPDIAL_MATH_ONLY=ON`.
- New Linux AddressSanitizer/UndefinedBehaviorSanitizer workflow job.
- Native tests of causal read taps, bounded cubic interpolation, actual ModernTone implementation and loop transitions.
- Original nine parameter IDs and existing marker state keys preserved. One new automatable parameter added.

### Known limits
- Full JUCE VST3/Standalone compilation and DAW audio tests are still required.
- Moving loop markers affects the next captured loop, not an already-frozen clip.
- Some hosts may stop requesting audio after input ceases because the reported tail is zero.
- No production-grade spectral anti-aliasing or continuous infinite slow-speed history guarantee.

## 1.3.0
- Loop-entry crossfade, periodic Hermite guards and in-block loop duration fixes.

## 1.2.0
- Tempo-tracking stop phase, incremental snapshot capture and locked-speed safety fade.
