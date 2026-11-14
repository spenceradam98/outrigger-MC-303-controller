# Outrigger: design plan (v0.2)

*Outrigger* is a working name. The plugin lets a Roland MC-303 Groovebox act as an external
sound module, hosted in Max Sample (the desktop companion to Max Tracker) or any VST3 host.
"Roland" and "MC-303" are trademarks of Roland Corporation. They are used here only to
describe compatibility and must not appear in the product name, logo or artwork.

There are two builds from the same code:

- **Outrigger** is an instrument (VST3 and standalone).
- **Outrigger FX** is a VST3 effect, for hosts that only feed audio input to effect slots.

## What it does

1. **MIDI out to the unit.** DAW MIDI goes out to the unit's part channels: rhythm part R on
   ch 10 and parts 1–7 on ch 1–7. Each part's channel can be changed.
   - *Sound Module mode* (hold PLAY MODE at power-on) gives 16 parts on two pages of strips,
     with the rhythm part on ch 10.
   - *Passthrough* keeps the host's channels. *Focus part* sends everything to one part, so
     you can run one instance per track.
   - Held notes are tracked. Routing changes, mutes, bypass and *Panic* release them, so no
     notes hang.
2. **Per-part sound control.** Each part has:
   - bank and program
   - volume, pan, expression, and reverb and chorus send
   - cutoff and resonance
   - attack, decay and release
   - vibrato rate, depth and delay
   - portamento on and time, and mono
   - bend range, and coarse and fine tune

   All of these can be automated by the host. The rhythm part also has a per-drum-note editor
   for pitch, level, pan, reverb and chorus. Global controls set the reverb and chorus types.
3. **Smart sending.** Only changed values go out. They are merged and kept under about 2,000
   of the 3,125 bytes/s a MIDI cable carries, so knob sweeps never choke the notes. *Send All*
   re-sends everything. Loading a project, loading a setup and connecting a device do the
   same.
4. **Two ways out.**
   - *Direct* opens the MIDI port from the plugin, so it doesn't depend on the host. All
     instances share the port, and each event is timed to the sample.
   - *Host* uses the plugin's MIDI output.
   - *Both* does both.
5. **MIDI from the unit (optional).**
   - *Follow unit knobs* moves the plugin's controls when you turn knobs or change programs
     on the hardware.
   - *Unit MIDI to host* passes the unit's notes and controllers to the host so they can be
     recorded.
   - A second LED on each strip lights for MIDI arriving from the unit.
6. **Audio return.** The plugin's stereo input is the unit's audio arriving through your
   interface. The return has trim, output, monitor, mono sum, a peak/RMS meter with a clip
   LED, and note LEDs on each part.
7. **Latency compensation.** The hardware's round-trip delay is reported to the host as
   plugin latency.
   - *Measure* plays three side-stick hits on part R and uses the median arrival time. It also
     shows the spread between hits, so you can see how steady the timing is.
   - You can also set the latency by hand in ms.
8. **MIDI clock (optional).** Sends 24 PPQ clock, start, stop, continue and song position from
   the host transport.
9. **Setups.** A setup is a whole 8 or 16-part state: programs, mix, tone edits, drum edits
   and effects.
   - *Save setup* writes a `.outrigger` file to `Documents/Outrigger/Setups`.
   - The *Setups* menu loads one, and *Init* resets everything.
   - Return levels, devices and latency are not part of a setup.
10. **Tone and rhythm-set names.** These come from two CSV files in `Documents/Outrigger`:
    - `tones.csv` holds `bank,program,name`.
    - `rhythm.csv` holds `program,name`.

    Program numbers run 1–128, as on the unit. The plugin creates both files as commented
    templates. *Names* reloads them, or opens the folder if they're still empty.
11. **Setup warnings.** A footer line says when something is wrong:
    - the host isn't running audio
    - the slot gets no audio input
    - notes went out but nothing came back
    - the host sends no tempo for the MIDI clock
12. **Themes.** Four themes: Studio Grey, Midnight, Acid and Paper. They are saved with the
    project.

## Hosting in Max Sample

I couldn't find public documentation for Max Sample. Outrigger therefore relies only on what
any VST3 host does:

- **MIDI to the unit** goes *Direct*: the plugin opens the port itself, whether or not Max
  Sample passes plugin MIDI out. Pick *Host* or *Both* only if Max Sample routes plugin MIDI
  out to a port.
- **Notes in** need only MIDI sent to the plugin. Use *Passthrough* if Max Tracker sends on
  channels 1–7 and 10. Use *Focus part* if each track sends on one channel.
- **Audio return:**
  1. Load **Outrigger** in an instrument slot.
  2. If the footer says "No audio input in this slot", Max Sample isn't feeding instrument
     slots. Load **Outrigger FX** in an effect slot on a channel whose input is the interface
     pair wired to the unit's outputs. Either build can drive the unit.
- **Latency:** press *Measure* once the return is wired. If Max Sample doesn't compensate
  plugin latency, the measured value is still the offset to use in Max Tracker.
- **Clock:** if the footer says "no tempo or transport", leave *Send MIDI clock* off and let
  the unit use its own tempo.
- **Standalone fallback:** the standalone Outrigger opens the interface and MIDI ports itself,
  with no host routing needed.

## Layout

The code is laid out like Crate12 and Tapehead Echo:

- `Source/engine` is plain C++ with no JUCE. It holds the protocol table, part sync with the
  bandwidth budget, the router, the clock, the meter, the latency probe and the parser for
  MIDI from the unit. Its tests are in `tests/` (14 groups).
- `Source/plugin` holds the JUCE wrapper, the shared MIDI-port hub (output and input), the UI
  and the themes.
- `tools/Snapshot.cpp` renders screenshots, smoke-tests the processor, and loads both built
  VST3 bundles the way a host does.
- The build uses JUCE 8.0.4 and CMake. It targets macOS 11+ universal with no AVX2 or
  `-march` flags, and Windows with the static CRT.

## MC-303 MIDI: what is confirmed

Sources:
- Roland *MC-303 Supplemental Notes* (10 Apr 1997), [cdn.roland.com/assets/media/pdf/MC303_Adv.pdf](http://cdn.roland.com/assets/media/pdf/MC303_Adv.pdf)
- *MC-303 Owner's Manual*, [archive.org text](https://archive.org/stream/synthmanual-roland-mc-303-owners-manual/rolandmc-303ownersmanual_djvu.txt)
- [Sweetwater: Editing tips for the MC-303](https://www.sweetwater.com/insync/editing-tips-roland-mc-303/)

| Control | Message | Status |
|---|---|---|
| Part channels R=10, 1–7 = ch 1–7 | n/a | **Confirmed** (owner's manual) |
| Sound Module mode, 16 parts | hold PLAY MODE at power-on | **Confirmed** |
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
| Messages sent by the unit's own knobs | n/a | Not documented in what I read. *Follow unit knobs* understands the CC, NRPN and RPN messages in this table, so it follows whatever the unit sends from that set. |

Delay is a reverb *type* (Delay, Pan Delay) on this unit. It is not a separate send.

## Still open

These need Adam or the hardware:

- **Tone names:** the 448-tone and rhythm-set lists are in the owner's manual
  (pp. 85–93). They still need typing into `tones.csv` and `rhythm.csv`. I couldn't fetch the
  list itself, so I haven't filled in any names.
- **Hardware checks:** confirm CC11/91/93 and the GS level SysEx on the hardware. Per-part
  SysEx stays off until Roland's part address table is checked.
- **Max Sample:** confirm which slot gives audio input and whether it compensates plugin
  latency.
- **Installers:** to be built when requested.
