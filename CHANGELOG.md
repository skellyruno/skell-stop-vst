# StopDial changelog

## 2.1.0 — Safety and transparency

- Detect exhausted tape history during both active braking and speed hold.
- Complete a bounded 50 ms fade to unprocessed input before data can be overwritten.
- Prioritise history-safety release over normal stop/loop handoff to prevent stuck attenuation.
- Preserve the original signal through Modern mode at full speed (no unwanted high-pass).
- Pass header menu clicks through the non-interactive background layer.
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
