// The entry point of the FreqArtFX target: FreqArt FX, the audio effect. (FreqArtEntry.cpp is
// the instrument's; everything else is shared, see PluginProcessor.h.)

#include "PluginProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new freqart::FreqArtProcessor (freqart::FreqArtProcessor::Variant::effect);
}
