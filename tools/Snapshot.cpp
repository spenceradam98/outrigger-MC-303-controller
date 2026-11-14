// Renders the editor to PNGs (one per theme, plus the rhythm-part view) for docs and review.
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
    return 0;
}
