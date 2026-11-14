# Outrigger (working name)

A VST3 and standalone plugin that drives a Roland MC-303 Groovebox as an external sound module.

- **MIDI out:** sends the rhythm part on ch 10 and parts 1–7 on ch 1–7.
- **Sound control per part:** program, mix, filter, envelope, vibrato, pitch and effects.
- **Audio return:** brings the unit's audio back from your interface with meters and latency
  compensation.

See `docs/DESIGN.md` for the plan, the MIDI confirmation table and host notes. Roland and
MC-303 are trademarks of Roland Corporation, used only to describe compatibility.

## Build

```
cmake -B build -DCMAKE_BUILD_TYPE=Release            # fetches JUCE 8.0.4
# or: cmake -B build -DJUCE_DIR=/path/to/JUCE
cmake --build build --config Release
ctest --test-dir build                                # engine tests
```

macOS builds target macOS 11+ universal (Intel and Apple Silicon). They use no AVX2 or other
`-march` flags. Windows builds use the static runtime. On Linux, the build needs the ALSA and
X11 dev packages.

## Layout

- `Source/engine` is plain C++: the protocol table, part sync with a MIDI bandwidth budget,
  routing, clock, meters and the latency probe. Its tests are in `tests/`.
- `Source/plugin` holds the JUCE wrapper, the shared MIDI-port hub, the UI and the 4 themes.
- `tools/Snapshot.cpp` renders UI screenshots. Build it with `-DOUTRIGGER_BUILD_SNAPSHOT=ON`.
