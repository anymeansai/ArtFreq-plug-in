// The entry point of the ArtFreq target: the instrument. (ArtFreqFXEntry.cpp is the effect's;
// everything else is shared, see PluginProcessor.h.)

#include "PluginProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new artfreq::ArtFreqProcessor (artfreq::ArtFreqProcessor::Variant::instrument);
}
