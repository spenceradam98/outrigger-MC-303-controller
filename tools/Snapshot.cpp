// Renders the editor to PNGs (one per theme, plus rhythm-part and 16-part views) for docs and review,
// and smoke-tests the processor and, if paths are given, the built VST3 bundles.
#include "../Source/plugin/PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();
    outrigger::OutriggerProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 256);
    proc.prepareToPlay (48000.0, 256);
    // Feed a little audio so the meters show something.
    juce::AudioBuffer<float> buf (2, 256);
    juce::MidiBuffer midi;
    for (int b = 0; b < 40; ++b)
    {
        for (int i = 0; i < 256; ++i) { const float v = 0.4f * std::sin ((b * 256 + i) * 0.05f); buf.setSample (0, i, v); buf.setSample (1, i, v * 0.6f); }
        proc.processBlock (buf, midi);
    }
    for (int t = 0; t < outrigger::themes().size(); ++t)
    {
        proc.setThemeIndex (t);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* e = dynamic_cast<outrigger::OutriggerEditor*> (ed.get());
        e->setSize (1180, 690);

        e->refreshNow();
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        juce::FileOutputStream os (outDir.getChildFile ("theme-" + juce::String (t) + ".png"));
        os.setPosition (0); os.truncate();
        juce::PNGImageFormat().writeImageToStream (img, os);
    }
    {
        proc.setThemeIndex (0);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* e = dynamic_cast<outrigger::OutriggerEditor*> (ed.get());
        e->showPart (0);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        juce::FileOutputStream os (outDir.getChildFile ("rhythm-part.png"));
        os.setPosition (0); os.truncate();
        juce::PNGImageFormat().writeImageToStream (img, os);
    }
    {
        // Sound Module mode, second page of parts.
        if (auto* prm = proc.apvts.getParameter ("module_mode")) prm->setValueNotifyingHost (1.0f);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* e = dynamic_cast<outrigger::OutriggerEditor*> (ed.get());
        e->showPart (12);
        e->refreshNow();
        e->resized();
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        juce::FileOutputStream os (outDir.getChildFile ("module-mode.png"));
        os.setPosition (0); os.truncate();
        juce::PNGImageFormat().writeImageToStream (img, os);
    }

    int failures = 0;
    auto check = [&] (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what); if (! ok) ++failures; };

    // Processor smoke test in host-MIDI mode: a note on ch 3 comes out on ch 3 with part settings.
    {
        outrigger::OutriggerProcessor p2;
        p2.setOutputMode (outrigger::OutputMode::Host);
        p2.setPlayConfigDetails (2, 2, 48000.0, 512);
        p2.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> b (2, 512);
        b.clear();
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (3, 60, (juce::uint8) 100), 10);
        p2.processBlock (b, m);
        bool note = false, pc = false;
        for (const auto meta : m)
        {
            auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getChannel() == 3 && meta.samplePosition == 10) note = true;
            if (msg.isProgramChange()) pc = true;
        }
        check (note, "note passes through on its channel at the right offset");
        check (pc, "initial sync sends program changes");

        juce::MemoryBlock state;
        p2.setHardwareLatency (777);
        p2.setDrum (38, 2, 30);
        p2.getStateInformation (state);
        outrigger::OutriggerProcessor p3;
        p3.setStateInformation (state.getData(), (int) state.getSize());
        check (p3.getHardwareLatency() == 777 && p3.getLatencySamples() == 777, "latency restored from state");
        check (p3.getDrum (38, 2) == 30, "drum edits restored from state");

        auto setup = p2.createSetup();
        check (setup.getNumChildren() > 300, "setup holds all part parameters");
    }

    // Load the built VST3 bundles like a host would.
    juce::VST3PluginFormat vst3;
    for (int i = 2; i < argc; ++i)
    {
        juce::OwnedArray<juce::PluginDescription> found;
        vst3.findAllTypesForFile (found, argv[i]);
        check (found.size() == 1, ("found plugin in " + juce::String (argv[i])).toRawUTF8());
        if (found.isEmpty()) continue;
        juce::String err;
        auto inst = vst3.createInstanceFromDescription (*found[0], 48000.0, 512, err);
        check (inst != nullptr, ("instantiated " + found[0]->name + " " + err).toRawUTF8());
        if (inst == nullptr) continue;
        std::printf ("      %s: instrument=%d inputs=%d outputs=%d midiIn=%d midiOut=%d\n", found[0]->name.toRawUTF8(),
                     (int) found[0]->isInstrument, inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels(),
                     (int) inst->acceptsMidi(), (int) inst->producesMidi());
        inst->prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> b (juce::jmax (2, inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels()), 512);
        b.clear();
        for (int c = 0; c < inst->getTotalNumInputChannels(); ++c) b.setSample (c, 0, 0.25f);
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        for (int k = 0; k < 4; ++k) inst->processBlock (b, m);
        check (inst->getTotalNumInputChannels() >= 1, "plugin exposes an audio input for the return");
        std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
        check (ed != nullptr, "editor opens");
        ed.reset();
        inst->releaseResources();
    }
    return failures == 0 ? 0 : 1;
}
