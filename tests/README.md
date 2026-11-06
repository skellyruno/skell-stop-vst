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
