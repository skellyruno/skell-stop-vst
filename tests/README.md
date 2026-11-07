# StopDial tests

`stopdial_math_tests.cpp` exercises the actual C++ math helpers in
`Source/dsp/StopDialMath.h` without depending on JUCE. Test coverage includes
brake-curve monotonicity, tempo continuity, Hermite interpolation, seamless
crossfade weights, 16 loop gate patterns, bounded incremental snapshot copy,
loop-wrapped interpolation guards, minimum-length loop bounds, mid-block loop
period updates, and a no-jump stop-to-loop transition.

From the project root:

```
clang++ -std=c++17 -Wall -Wextra -Werror -O2 tests/stopdial_math_tests.cpp -o StopDialDSPTests
./StopDialDSPTests
python3 tests/check_math.py
```

C++ assertions remain enabled even in optimized Release builds compiled with
`-DNDEBUG`, so GitHub Actions / CTest does not silently skip checks.

Alternatively, pass `-DSTOPDIAL_BUILD_TESTS=ON` to CMake and run CTest after
configuring JUCE. Sanitizer arguments and manual host test guidance are in the
root README.

These are *not* audio-thread profilers, full JUCE integration tests, DAW bounce
or subjective sound-quality listening evaluations.

### v2.0 test coverage

The native suite exercises causal read tap offsets, bounded Hermite behavior
across every combination of -1/0/1 tap values, ModernTone's *actual* shared
processor at rates from 22.05 through 192 kHz, loop timing and source limits.

Without JUCE you can now build and run CTest directly:

```sh
cmake -S . -B build-math -DSTOPDIAL_MATH_ONLY=ON
cmake --build build-math --parallel
ctest --test-dir build-math --output-on-failure
```

No JUCE render, host automation timing, or audible acceptance tests are
included in this standalone math target.

### v2.2 tests and optional performance measurement

The native regression now covers the actual `ModernCutoffSmoother` and
`GateSlew` helpers, their responses across sample rates, arbitrary rhythm
pattern changes, invalid values, and bounded interpolation in both live and
captured loop buffers. It is still **not** a plugin-host integration test.

To measure only filter-coefficient calculation overhead (not total plugin CPU):

```sh
cmake -S . -B build-bench -DSTOPDIAL_MATH_ONLY=ON -DSTOPDIAL_PROFILE_DSP=ON
cmake --build build-bench --parallel
./build-bench/StopDialModernBenchmark
```

The baseline deliberately uses one `pow` and `exp` per sample (v2.1 behavior),
while the updated version uses a shared target every 16 frames with audio-rate
slewing. Results depend on compiler, optimization, CPU, and system load; do
not extrapolate them to total DAW performance.
