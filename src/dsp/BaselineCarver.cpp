#include "BaselineCarver.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// How the frames line up:
//
// Every hop (a quarter frame) the newest fftSize input samples are one frame. Its windowed,
// carved transform is added into `sum`, whose first hop is then complete (all four frames that
// overlap it are in) and is played out over the next hop, while the input of that hop is taken
// in. A sample therefore comes out exactly fftSize samples after it went in.
//
// The periodic sqrt-Hann window, used twice, is a Hann window, and Hann windows a quarter frame
// apart add up to 2: halving each frame gives the input back exactly where the gains are 1.
//
// The window's centre is the first sample of the hop before the newest one, which is where the
// frame takes its position in the drawing and its gate from.
//
// JUCE's FFT is vDSP on the Mac (no locks, no allocation). Its portable fallback (the Linux dev
// build) takes a spin lock of its own per call; only this engine's audio thread ever calls it,
// so it is never contended.

namespace artfreq
{

namespace
{
    constexpr int framesPerWindow = 4;  // 75 % overlap: four frames cover every sample
    constexpr float overlapGain = 0.5f; // 1 / (sum of the four overlapping Hann windows)
}

void BaselineCarver::prepareCarver()
{
    // About 43 ms per frame at any rate, so the carve looks the same at 44.1 and 96 kHz.
    fftOrder = sampleRate <= 50000.0 ? 11 : (sampleRate <= 100000.0 ? 12 : 13);
    fftSize = 1 << fftOrder;
    hop = fftSize / framesPerWindow;
    numBins = fftSize / 2 + 1;
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);

    window.resize (static_cast<size_t> (fftSize));
    untouched.resize (static_cast<size_t> (fftSize));

    for (int n = 0; n < fftSize; ++n)
    {
        // sqrt (0.5 - 0.5 cos (2 pi n / N)) = sin (pi n / N)
        const auto w = static_cast<float> (std::sin (juce::MathConstants<double>::pi * n / fftSize));
        window[static_cast<size_t> (n)] = w;
        untouched[static_cast<size_t> (n)] = w * w * overlapGain;
    }

    fftBuffer.assign (static_cast<size_t> (2 * fftSize), 0.0f);
    gains.assign (static_cast<size_t> (numBins), 1.0f);
    binRows.assign (static_cast<size_t> (numBins), 0.0);
    binRowsCount = -1;

    channelState.resize (static_cast<size_t> (maxChannels));

    for (auto& state : channelState)
    {
        state.history.assign (static_cast<size_t> (fftSize), 0.0f);
        state.sum.assign (static_cast<size_t> (fftSize), 0.0f);
        state.out.assign (static_cast<size_t> (hop), 0.0f);
    }

    lookUpBinRows();
}

void BaselineCarver::resetCarver() noexcept
{
    for (auto& state : channelState)
    {
        std::fill (state.history.begin(), state.history.end(), 0.0f);
        std::fill (state.sum.begin(), state.sum.end(), 0.0f);
        std::fill (state.out.begin(), state.out.end(), 0.0f);
    }

    writePos = 0;
    hopFill = 0;
    // The silence in the history is the delayed input of a fresh start, so it is played out as is.
    untouchedRun = framesPerWindow;
    currentHop = {};
    previousHop = {};
}

void BaselineCarver::planChanged() noexcept
{
    lookUpBinRows();
}

void BaselineCarver::lookUpBinRows() noexcept
{
    // Only when the plan's scale changed (rowAt is a log per bin on the log scale).
    if (plan == nullptr || binRows.size() != static_cast<size_t> (numBins)
        || (binRowsCount == plan->numRows && binRowsScale == plan->scale))
        return;

    for (int bin = 0; bin < numBins; ++bin)
        binRows[static_cast<size_t> (bin)] = plan->rowAt (bin * sampleRate / fftSize);

    binRowsScale = plan->scale;
    binRowsCount = plan->numRows;
}

void BaselineCarver::carve (float* const* channels, int numChannels, int numSamples,
                            const Timeline& timeline, const CarveSettings& settings) noexcept
{
    for (int done = 0; done < numSamples;)
    {
        if (hopFill == 0)
        {
            previousHop = currentHop;
            currentHop = { timeline.fraction[done], timeline.gate[done] };
        }

        const auto length = std::min (hop - hopFill, numSamples - done);

        for (int channel = 0; channel < maxChannels; ++channel)
        {
            auto& state = channelState[static_cast<size_t> (channel)];
            auto* history = state.history.data() + writePos;

            if (channel >= numChannels)
            {
                // A channel the host did not send this time: silence, so it is in step if it returns.
                std::fill (history, history + length, 0.0f);
                continue;
            }

            auto* io = channels[channel] + done;
            const auto* out = state.out.data() + hopFill;

            for (int i = 0; i < length; ++i)
            {
                history[i] = io[i];
                io[i] = out[i];
            }
        }

        // A hop starts on a multiple of the hop size, so it never wraps round the ring.
        writePos = (writePos + length) % fftSize;
        hopFill += length;
        done += length;

        if (hopFill == hop)
        {
            runFrame (numChannels, settings);
            hopFill = 0;
        }
    }
}

bool BaselineCarver::computeGains (const HopStart& centre, const CarveSettings& settings) noexcept
{
    if (plan == nullptr || plan->empty || centre.gate <= 0.0f || settings.depthDb >= 0.0f)
        return false;

    const auto column = plan->columnAt (centre.fraction);
    const auto stencil = settings.mode == CarveMode::stencil;

    if (! stencil && ! plan->isColumnUsed (column))
        return false;

    lookUpBinRows();

    // gain = depth ^ amount, as a natural exponent per unit of amount
    const auto perAmount = settings.depthDb * 0.11512925f;  // ln (10) / 20

    for (int bin = 0; bin < numBins; ++bin)
    {
        const auto mask = plan->valueAtRow (column, binRows[static_cast<size_t> (bin)]);
        const auto amount = centre.gate * (stencil ? 1.0f - mask : mask);
        gains[static_cast<size_t> (bin)] = std::exp (perAmount * amount);
    }

    return true;
}

void BaselineCarver::runFrame (int numChannels, const CarveSettings& settings) noexcept
{
    const auto changes = computeGains (previousHop, settings);
    untouchedRun = changes ? 0 : std::min (untouchedRun + 1, framesPerWindow);

    // Every frame over the hop about to be played changed nothing: play the input itself.
    const auto exact = untouchedRun >= framesPerWindow;
    const auto size = static_cast<size_t> (fftSize);
    const auto hopSize = static_cast<size_t> (hop);
    const auto oldest = static_cast<size_t> (writePos);  // the frame's first sample in the rings

    for (int channel = 0; channel < maxChannels; ++channel)
    {
        auto& state = channelState[static_cast<size_t> (channel)];
        auto* sum = state.sum.data();
        auto* out = state.out.data();

        if (channel >= numChannels)
        {
            std::fill (state.sum.begin(), state.sum.end(), 0.0f);
            std::fill (state.out.begin(), state.out.end(), 0.0f);
            continue;
        }

        // The frame in time order.
        auto* frame = fftBuffer.data();
        const auto* history = state.history.data();
        std::copy (history + oldest, history + size, frame);
        std::copy (history, history + oldest, frame + (size - oldest));

        if (exact)
            std::copy (frame, frame + hopSize, out);

        if (changes)
        {
            for (size_t n = 0; n < size; ++n)
                frame[n] *= window[n];

            fft->performRealOnlyForwardTransform (frame, true);

            for (size_t bin = 0; bin < static_cast<size_t> (numBins); ++bin)
            {
                frame[2 * bin] *= gains[bin];
                frame[2 * bin + 1] *= gains[bin];
            }

            fft->performRealOnlyInverseTransform (frame);

            for (size_t n = 0; n < size; ++n)
                sum[n] += frame[n] * window[n] * overlapGain;
        }
        else
        {
            for (size_t n = 0; n < size; ++n)
                sum[n] += frame[n] * untouched[n];
        }

        if (! exact)
            std::copy (sum, sum + hopSize, out);

        std::memmove (sum, sum + hopSize, (size - hopSize) * sizeof (float));
        std::fill (sum + (size - hopSize), sum + size, 0.0f);
    }
}

} // namespace artfreq
