#pragma once

// Helpers for driving the processor without a host.

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <vector>

namespace testutil
{

/** A host transport the test controls. */
struct FakePlayHead final : public juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override { return info; }

    void set (bool playing, double bpm, double ppq, double seconds)
    {
        info = PositionInfo();
        info.setIsPlaying (playing);
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        info.setTimeInSeconds (seconds);
        info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
    }

    PositionInfo info;
};

inline void setParameter (freqart::FreqArtProcessor& processor, const char* id, float plainValue)
{
    if (auto* parameter = processor.getParameters().getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
}

inline float getParameter (freqart::FreqArtProcessor& processor, const char* id)
{
    if (auto* value = processor.getParameters().getRawParameterValue (id))
        return value->load();

    return std::nanf ("");
}

/** Renders `numSamples` in blocks of `blockSize` and returns the left channel. `beforeBlock` is
    called with the block's first sample index and may fill the block's MIDI. */
template <typename BeforeBlock>
std::vector<float> render (freqart::FreqArtProcessor& processor, int numSamples, int blockSize, BeforeBlock&& beforeBlock)
{
    std::vector<float> out;
    out.reserve (static_cast<size_t> (numSamples));

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    for (int start = 0; start < numSamples; start += blockSize)
    {
        const auto length = juce::jmin (blockSize, numSamples - start);
        buffer.setSize (2, length, false, false, true);
        midi.clear();
        beforeBlock (start, length, midi);
        processor.processBlock (buffer, midi);

        for (int i = 0; i < length; ++i)
            out.push_back (buffer.getSample (0, i));
    }

    return out;
}

inline std::vector<float> render (freqart::FreqArtProcessor& processor, int numSamples, int blockSize = 512)
{
    return render (processor, numSamples, blockSize, [] (int, int, juce::MidiBuffer&) {});
}

inline float peak (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
{
    float p = 0.0f;
    for (size_t i = from; i < std::min (to, x.size()); ++i)
        p = std::max (p, std::abs (x[i]));
    return p;
}

inline double rms (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
{
    double sum = 0.0;
    size_t n = 0;
    for (size_t i = from; i < std::min (to, x.size()); ++i, ++n)
        sum += static_cast<double> (x[i]) * static_cast<double> (x[i]);
    return n > 0 ? std::sqrt (sum / static_cast<double> (n)) : 0.0;
}

} // namespace testutil
