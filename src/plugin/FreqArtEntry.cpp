// The entry point of the FreqArt target: the instrument. (FreqArtFXEntry.cpp is the effect's;
// everything else is shared, see PluginProcessor.h.)

#include "PluginProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new freqart::FreqArtProcessor (freqart::FreqArtProcessor::Variant::instrument);
}
