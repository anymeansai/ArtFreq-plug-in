#include "SpectralCarver.h"

#include "BaselineCarver.h"
#include "TunedCarver.h"

#include <algorithm>
#include <cmath>

namespace freqart
{

namespace
{
    constexpr double endOfPass = 1.0 - 0x1.0p-53;  // the last position before 1

    float shapeEnvelope (float e) noexcept
    {
        return e * e * (3.0f - 2.0f * e);
    }
}

void SpectralCarver::prepare (double newSampleRate, int newMaxBlockSize, int numChannels)
{
    sampleRate = newSampleRate > 0.0 && std::isfinite (newSampleRate) ? newSampleRate : 48000.0;
    maxBlockSize = std::max (1, newMaxBlockSize);
    maxChannels = std::max (1, numChannels);

    fractions.assign (static_cast<size_t> (maxBlockSize), 0.0);
    gates.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    chunkChannels.assign (static_cast<size_t> (maxChannels), nullptr);
    gateStep = static_cast<float> (1.0 / (gateFadeSeconds * sampleRate));

    prepareCarver();
    reset();
}

void SpectralCarver::reset() noexcept
{
    segmentStart = 0;
    segmentFraction = 0.0;
    rate = 0.0;
    once = false;
    gateOn = false;
    envelope = 0.0f;

    if (maxBlockSize > 0)
        resetCarver();
}

void SpectralCarver::setPlan (const MaskPlan* newPlan) noexcept
{
    if (newPlan == plan)
        return;

    plan = newPlan;
    planChanged();
}

void SpectralCarver::applySegment (const PlaySegment& segment, int sample) noexcept
{
    // Like the sine engine, a gate that closes while the carving is still open lets the fade
    // run on where it was; anything else (a jump too: the audio jumped with the position, so
    // the mask must follow at once) takes the segment's position.
    if (! (segment.jump || segment.playing || envelope <= 0.0f))
    {
        gateOn = false;
        return;
    }

    const auto fraction = std::isfinite (segment.fraction) ? segment.fraction : 0.0;
    segmentFraction = segment.once ? std::clamp (fraction, 0.0, endOfPass) : fraction - std::floor (fraction);
    rate = std::isfinite (segment.fractionPerSample) && segment.fractionPerSample > 0.0
         ? std::min (segment.fractionPerSample, 1.0) : 0.0;
    segmentStart = std::max (sample, segment.start);
    once = segment.once;
    gateOn = segment.playing;
}

double SpectralCarver::positionAt (int sample) const noexcept
{
    const auto p = segmentFraction + static_cast<double> (sample - segmentStart) * rate;

    if (once)
        return std::clamp (p, 0.0, endOfPass);

    const auto wrapped = p - std::floor (p);
    return wrapped >= 1.0 ? 0.0 : wrapped;
}

void SpectralCarver::process (float* const* channels, int numChannels, int numSamples,
                              const PlaySegment* segments, int numSegments, const CarveSettings& settings) noexcept
{
    if (channels == nullptr || numSamples <= 0 || maxBlockSize <= 0)
        return;

    const auto active = std::min (numChannels, maxChannels);

    auto checked = settings;
    checked.depthDb = std::isfinite (settings.depthDb) ? std::clamp (settings.depthDb, minDepthDb, 0.0f) : 0.0f;

    // With nothing drawn the gate stays shut, so Stencil passes the audio rather than muting it all.
    const auto hasDrawing = plan != nullptr && ! plan->empty;
    auto next = 0;

    for (int chunk = 0; chunk < numSamples; chunk += maxBlockSize)
    {
        const auto length = std::min (maxBlockSize, numSamples - chunk);

        for (int i = 0; i < length; ++i)
        {
            const auto sample = chunk + i;

            while (segments != nullptr && next < numSegments && segments[next].start <= sample)
                applySegment (segments[next++], sample);

            const auto open = gateOn && hasDrawing;
            envelope = open ? std::min (1.0f, envelope + gateStep) : std::max (0.0f, envelope - gateStep);

            fractions[static_cast<size_t> (i)] = positionAt (sample);
            gates[static_cast<size_t> (i)] = shapeEnvelope (envelope);
        }

        for (int channel = 0; channel < active; ++channel)
            chunkChannels[static_cast<size_t> (channel)] = channels[channel] + chunk;

        Timeline timeline;
        timeline.fraction = fractions.data();
        timeline.gate = gates.data();
        carve (chunkChannels.data(), active, length, timeline, checked);
    }

    // The position runs on into the next block from where this one leaves it.
    segmentStart -= numSamples;
}

std::unique_ptr<SpectralCarver> createSpectralCarver (int engine)
{
    switch (engine)
    {
        case 0:  return std::make_unique<BaselineCarver>();
        case 1:  return std::make_unique<TunedCarver>();
        default: return nullptr;
    }
}

int getNumSpectralCarvers() noexcept
{
    return 2;
}

} // namespace freqart
