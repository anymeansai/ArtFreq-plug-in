// The entry point of the ArtFreqFX target: ArtFreq FX, the audio effect. (ArtFreqEntry.cpp is
// the instrument's; everything else is shared, see PluginProcessor.h.)

#include "PluginProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new artfreq::ArtFreqProcessor (artfreq::ArtFreqProcessor::Variant::effect);
}
