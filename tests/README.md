# Suggested plugin verification matrix

No dependency-free unit-test binary is included. Test using JUCE's plugin host,
the standalone target, and optionally pluginval.

- Verify momentary stopTrigger produces one stop per rising edge.
- At 120 BPM, 1/4 should reach the stop in 0.5 s, 1/8 in 0.25 s,
  and 1/8. in 0.375 s; at 90 BPM these should scale proportionally.
- Run mono/stereo silence, impulse, sine sweeps and full scale signals.
- Automate wet depth and curve while stopping; test denormal handling.
- Save a preset with non-default loop marker positions and restore it.
- Measure zero samples of host-reported latency.
- On host close, waveform producer must drop samples safely if FIFO fills.
