# Outrigger: design plan (v0.1)

*Outrigger* is a working name. The plugin makes a Roland MC-303 Groovebox work as an external
sound module inside a DAW. "Roland" and "MC-303" are trademarks of Roland Corporation. They are
used here only to describe compatibility and must not appear in the product name, logo or artwork.

## What it does

1. **MIDI out to the unit.** DAW MIDI goes out to the MC-303's part channels: rhythm part R on
   ch 10 and parts 1 to 7 on ch 1 to 7. Each part's channel can be changed.
   - *Passthrough* keeps the DAW's channels, for one instance that drives all parts.
   - *Focus part* sends everything to the selected part, for one instance per DAW track.
   - Held notes are tracked. Changing the routing, muting a part, bypassing the plugin or
     pressing *Panic* releases them, so no notes hang.
2. **Per-part sound control.** Each part has bank and program (tone), volume, pan, expression,
   reverb and chorus send, cutoff, resonance, attack, decay, release, vibrato rate, depth and
   delay, portamento on and time, mono/poly, bend range, and coarse and fine tune. All of these
   are host-automatable parameters.
   - The rhythm part has a drum-note editor for pitch, level, pan, reverb and chorus per drum
     note.
   - Global controls set the reverb type (8) and chorus type (8).
3. **Smart sending.** Only changed values are sent. Values are coalesced and kept under a MIDI
   bandwidth budget of about 2,000 of the 3,125 bytes/s on a DIN cable, so knob sweeps never
   choke the notes. *Send All* re-sends everything, and so do loading a project and connecting
   a device.
4. **Two ways out.**
   - *Direct* opens the MIDI port from the plugin, so it works in any host, including hosts
     with no VST3 MIDI out. Instances share one port, and a sender thread schedules each event
     to the sample.
   - *Host* sends through the plugin's MIDI output.
   - *Both* does both.
5. **Audio return.** The plugin's stereo input bus is the MC-303's audio arriving through your
   interface. The return has trim, output, monitor, mono sum, a peak/RMS meter with clip LED,
   and per-part note-activity LEDs.
6. **Latency compensation.** The hardware round trip is reported to the host as plugin latency,
   so the DAW lines the return up with your other tracks. *Measure* plays a side-stick on part R
   and times its arrival on the input. You can also set the value by hand in ms.
7. **MIDI clock (optional).** Sends 24 PPQ clock, start, stop, continue and song position from
   the DAW transport, so the unit's own patterns and arpeggiator stay in time.
8. **Themes.** Four themes: Studio Grey, Midnight, Acid and Paper. They are saved with the
   project.

## Layout

The code is laid out like Crate12 and Tapehead Echo:

- `Source/engine` is plain C++ with no JUCE. It holds the protocol table, part sync with the
  bandwidth budget, the router, the clock, the meter and the latency probe. Its tests are in
  `tests/`.
- `Source/plugin` is the JUCE wrapper, the shared MIDI-port hub, the UI and the themes.
- `tools/Snapshot.cpp` renders UI screenshots and is not shipped.
- The build is JUCE 8.0.4 with CMake. It targets VST3 and Standalone, macOS 11+ universal and
  Windows with the static CRT. The code is scalar C++ with no AVX2 or `-march` flags.

## MC-303 MIDI: what is confirmed

Sources:
- Roland *MC-303 Supplemental Notes* (10 Apr 1997), [cdn.roland.com/assets/media/pdf/MC303_Adv.pdf](http://cdn.roland.com/assets/media/pdf/MC303_Adv.pdf)
- *MC-303 Owner's Manual*, [archive.org text](https://archive.org/stream/synthmanual-roland-mc-303-owners-manual/rolandmc-303ownersmanual_djvu.txt)
- [Sweetwater: Editing tips for the MC-303](https://www.sweetwater.com/insync/editing-tips-roland-mc-303/)

I could not read the owner's manual MIDI implementation chart (p.101–108) in full. Anything
marked "likely" should be checked on the hardware.

| Control | Message | Status |
|---|---|---|
| Part channels R=10, 1–7 = ch 1–7 | n/a | **Confirmed** (owner's manual) |
| Tone select | CC0 bank MSB, CC32 = 0, Program Change | **Confirmed** |
| Rhythm set | Program Change on ch 10 | **Confirmed** |
| Modulation, volume, pan, hold | CC1, CC7, CC10, CC64 | **Confirmed** |
| Portamento on / time | CC65, CC5 | **Confirmed** |
| Mono | CC126 (poly via CC127) | **Confirmed** (mono) / likely (poly) |
| Bend range, fine tune, coarse tune | RPN 0, 1, 2 | **Confirmed** |
| Cutoff, resonance | NRPN 01/20h, 01/21h, value 14–114 | **Confirmed** |
| Env attack, decay, release | NRPN 01/63h, 01/64h, 01/66h | **Confirmed** |
| Vibrato rate, depth, delay | NRPN 01/08h, 01/09h, 01/0Ah | **Confirmed** |
| Drum note pitch, level, pan, reverb, chorus | NRPN 18h, 1Ah, 1Ch, 1Dh, 1Eh / note | **Confirmed** |
| Reverb type, chorus type (macro) | SysEx 00 40 01 30 / 00 40 01 38 | **Confirmed** (checksums match Roland's worked examples) |
| Expression | CC11 | Likely (GS standard) |
| Reverb / chorus send per part | CC91 / CC93 | Likely (GS standard) |
| Reverb / chorus master level | SysEx 00 40 01 33 / 00 40 01 3A | Experimental, off by default |
| Per-part SysEx (part address map) | 00 40 1x yy | Not used. Roland's part-to-address table needs checking against the PDF. |
| Sound Module mode (16 parts) | hold PLAY MODE at power-on | Confirmed, but v0.1 drives 8 parts |
| Knob CCs sent by the unit itself | n/a | Not documented in what I read; MIDI-in learn is a later step |

Delay is a reverb *type* (Delay, Pan Delay) on this unit. It is not a separate send.

## Host notes

- **Reaper, Bitwig, Cubase, Studio One, FL Studio:** load Outrigger as an instrument on a track
  whose audio input is the interface pair wired to the unit's outputs, or route that input to
  the plugin's input bus. Choose *Direct* and pick the MIDI port.
- **Ableton Live:** VST3 instruments get no audio input and no MIDI out. Use *Direct* for MIDI.
  For the return, a planned companion "Return" effect (see the decisions below) or Live's own
  input monitoring will be needed.
- **Standalone:** set the audio input to the interface pair in *Options → Audio/MIDI Settings*.
  MIDI can go *Direct* or through the standalone's MIDI output selector.

## Not in v0.1 (next steps)

- Tone and rhythm-set names. The 448-tone list is on pages 85–93 of the owner's manual, so
  v0.1 shows numbers.
- Preset/snapshot browser for whole 8-part setups.
- MIDI-learn from the unit's own knobs, and receiving its MIDI.
- Sound Module mode with 16 parts, and per-part SysEx once the address map is verified.
- A companion "Return" audio-effect plugin for Ableton.
- Averaged multi-ping latency measurement.
- Installers (when requested).
