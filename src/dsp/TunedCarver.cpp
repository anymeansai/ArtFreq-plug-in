#include "TunedCarver.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// How the frames line up (as in the baseline, for any overlap):
//
// Every hop (frame / overlap samples) the newest frameSize input samples are one frame. Its
// windowed, carved transform is added into `sum`, whose first hop is then complete (all the
// frames that overlap it are in) and is played out over the next hop, while the input of that
// hop is taken in. A sample therefore comes out exactly frameSize samples after it went in, and
// so does its gate, which picks per sample between that and the input frameSize samples ago.
//
// The window's centre is the start of hop (newest - overlap / 2 + 1); the timeline there gives
// the frame's position in the drawing.
//
// Where frameSize is not a power of 2 (44.1 kHz) the frame fills the start of the next larger
// transform; what a sharp gain spreads beyond the frame lands in the padding and is dropped.
//
// JUCE's FFT is vDSP on the Mac (no locks, no allocation). Its portable fallback (the Linux dev
// build) takes a spin lock of its own per call; only this engine's audio thread ever calls it,
// so it is never contended.

namespace freqart
{

namespace
{
    std::vector<float> powerSine (int size, float power)
    {
        std::vector<float> window (static_cast<size_t> (size));

        for (int n = 0; n < size; ++n)
        {
            const auto s = std::sin (juce::MathConstants<double>::pi * n / size);
            window[static_cast<size_t> (n)] = static_cast<float> (std::pow (s, static_cast<double> (power)));
        }

        return window;
    }
}

TunedCarver::TunedCarver() : TunedCarver (Options()) {}

TunedCarver::TunedCarver (const Options& o) : options (o)
{
    options.overlap = std::clamp (options.overlap, 2, hopRingSize);
    options.windowPower = std::max (0.5f, options.windowPower);
    options.pointSpacing = std::max (16, options.pointSpacing);

    char text[200];
    std::snprintf (text, sizeof (text), "tuned STFT %d (%.1f ms), sin^%g windows, %d x overlap, Carve: mean per bin, "
                                        "the drawing read every %d samples",
                   juce::roundToInt (options.frameSeconds * 48000.0), options.frameSeconds * 1000.0,
                   static_cast<double> (options.windowPower), options.overlap, options.pointSpacing);
    name = text;
}

void TunedCarver::prepareCarver()
{
    // The same frame duration at any rate, a whole number of hops long.
    frameSize = std::max (options.overlap * 16,
                          juce::roundToInt (options.frameSeconds * sampleRate / options.overlap) * options.overlap);
    hop = frameSize / options.overlap;

    // Points about pointSpacing apart, an even number of them (the frame reads the second half
    // of one hop and the first half of the next), each at the centre of its share of the hop.
    pointsPerHop = std::clamp (2 * juce::roundToInt (0.5 * hop / options.pointSpacing), 2, maxPointsPerHop);

    for (int p = 0; p < pointsPerHop; ++p)
        pointOffset[p] = static_cast<int> ((2 * static_cast<int64_t> (p) + 1) * hop / (2 * pointsPerHop));

    fftOrder = 4;
    while ((1 << fftOrder) < frameSize)
        ++fftOrder;

    fftSize = 1 << fftOrder;
    numBins = fftSize / 2 + 1;
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);

    analysisWindow = powerSine (frameSize, options.windowPower);
    synthesisWindow = powerSine (frameSize, options.windowPower);

    // Divide the synthesis window by the sum of the window products that overlap each sample,
    // so the frames add back up to the input exactly (for sin^3 at 75 % overlap that sum is the
    // constant 1.25; for other windows it ripples, and this undoes the ripple too).
    std::vector<double> overlapSum (static_cast<size_t> (hop), 0.0);

    for (int n = 0; n < frameSize; ++n)
        overlapSum[static_cast<size_t> (n % hop)] += static_cast<double> (analysisWindow[static_cast<size_t> (n)])
                                                    * static_cast<double> (synthesisWindow[static_cast<size_t> (n)]);

    untouched.resize (static_cast<size_t> (frameSize));

    for (int n = 0; n < frameSize; ++n)
    {
        auto& s = synthesisWindow[static_cast<size_t> (n)];
        s = static_cast<float> (static_cast<double> (s) / overlapSum[static_cast<size_t> (n % hop)]);
        untouched[static_cast<size_t> (n)] = analysisWindow[static_cast<size_t> (n)] * s;
    }

    const auto bins = static_cast<size_t> (numBins);
    fftBuffer.assign (static_cast<size_t> (2 * fftSize), 0.0f);
    gains.assign (bins, 1.0f);
    amounts.assign (bins, 0.0f);
    columnValues.assign (bins, 0.0f);
    besideBefore.assign (bins, 0.0f);
    besideAfter.assign (bins, 0.0f);
    scratch.assign (bins, 0.0f);
    binEdgeRows.assign (bins + 1, 0.0);
    prefix.assign (static_cast<size_t> (Canvas::height + 1), 0.0);
    binRowsCount = -1;

    channelState.resize (static_cast<size_t> (maxChannels));

    for (auto& state : channelState)
    {
        state.history.assign (static_cast<size_t> (frameSize), 0.0f);
        state.sum.assign (static_cast<size_t> (frameSize), 0.0f);
        state.out.assign (static_cast<size_t> (hop), 0.0f);
    }

    gateHistory.assign (static_cast<size_t> (frameSize), 0.0f);
    lookUpBinRows();
}

void TunedCarver::resetCarver() noexcept
{
    for (auto& state : channelState)
    {
        std::fill (state.history.begin(), state.history.end(), 0.0f);
        std::fill (state.sum.begin(), state.sum.end(), 0.0f);
        std::fill (state.out.begin(), state.out.end(), 0.0f);
    }

    std::fill (gateHistory.begin(), gateHistory.end(), 0.0f);

    for (auto& moment : hopRing)
        moment = {};

    hopCount = 0;
    writePos = 0;
    hopFill = 0;
    // The silence in the history is the delayed input of a fresh start, so it is played out as is.
    untouchedRun = options.overlap;
}

void TunedCarver::planChanged() noexcept
{
    lookUpBinRows();
}

void TunedCarver::lookUpBinRows() noexcept
{
    // Only when the plan's scale changed (rowAt is a log per bin edge on the log scale).
    if (plan == nullptr || binEdgeRows.size() != static_cast<size_t> (numBins + 1)
        || (binRowsCount == plan->numRows && binRowsScale == plan->scale))
        return;

    const auto binHz = sampleRate / fftSize;

    for (int edge = 0; edge <= numBins; ++edge)
        binEdgeRows[static_cast<size_t> (edge)] = plan->rowAt (std::max (0.0, (edge - 0.5) * binHz));

    binRowsScale = plan->scale;
    binRowsCount = plan->numRows;
}

const TunedCarver::Hop& TunedCarver::hopAt (int64_t index) const noexcept
{
    return hopRing[static_cast<size_t> ((index % hopRingSize + hopRingSize) % hopRingSize)];
}

void TunedCarver::carve (float* const* channels, int numChannels, int numSamples,
                         const Timeline& timeline, const CarveSettings& settings) noexcept
{
    for (int done = 0; done < numSamples;)
    {
        if (hopFill == 0)
        {
            auto& started = hopRing[static_cast<size_t> (hopCount++ % hopRingSize)];
            started.fraction = timeline.fraction[done];
            started.openAtStart = timeline.gate[done] > 0.0f;
            started.open = false;
            std::fill (started.pointGate, started.pointGate + maxPointsPerHop, 0.0f);
        }

        auto& current = hopRing[static_cast<size_t> ((hopCount - 1) % hopRingSize)];
        const auto length = std::min (hop - hopFill, numSamples - done);

        // Where the gate first opens in this hop: a pass that starts inside it is carved from its
        // first sample, not from the next hop.
        for (int i = 0; i < length && ! current.open; ++i)
        {
            if (timeline.gate[done + i] > 0.0f)
            {
                current.open = true;
                current.fraction = timeline.fraction[done + i];
            }
        }

        // The points of the hop that fall in this stretch.
        for (int p = 0; p < pointsPerHop; ++p)
        {
            const auto at = pointOffset[p] - hopFill;

            if (at >= 0 && at < length)
            {
                current.pointFraction[p] = timeline.fraction[done + at];
                current.pointGate[p] = timeline.gate[done + at];
            }
        }

        auto* gate = gateHistory.data() + writePos;

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

            // The carved audio where the gate (as late as the audio) is open, the input itself
            // where it is shut, and a crossfade while it opens or closes.
            for (int i = 0; i < length; ++i)
            {
                const auto dry = history[i];
                const auto open = gate[i];
                history[i] = io[i];
                io[i] = open >= 1.0f ? out[i] : (open <= 0.0f ? dry : dry + open * (out[i] - dry));
            }
        }

        std::copy (timeline.gate + done, timeline.gate + done + length, gate);

        // A hop starts on a multiple of the hop size, so it never wraps round the rings.
        writePos = (writePos + length) % frameSize;
        hopFill += length;
        done += length;

        if (hopFill == hop)
        {
            runFrame (numChannels, settings);
            hopFill = 0;
        }
    }
}

bool TunedCarver::columnAmounts (int column, bool stencil, float* out) noexcept
{
    // Per bin, the drawing's mean over the bin's band (and in Stencil its maximum there), from
    // one column. Returns false, with every value 0, if that column is empty.
    if (! plan->isColumnUsed (column))
    {
        std::fill (out, out + numBins, 0.0f);
        return false;
    }

    const auto* cells = plan->column (column);
    const auto rows = plan->numRows;
    const auto top = static_cast<double> (rows) - 0.5;
    const auto mixMax = stencil ? options.stencilMaskMax : options.carveMaskMax;

    // Row r is a cell from r - 0.5 to r + 0.5; prefix[r] is the sum of the cells below row r.
    prefix[0] = 0.0;

    for (int r = 0; r < rows; ++r)
        prefix[static_cast<size_t> (r + 1)] = prefix[static_cast<size_t> (r)] + static_cast<double> (cells[r]);

    const auto integral = [this, cells] (double y, int cell)  // of the cells from -0.5 to y, inside `cell`
    {
        return prefix[static_cast<size_t> (cell)] + (y + 0.5 - cell) * static_cast<double> (cells[cell]);
    };

    for (int bin = 0; bin < numBins; ++bin)
    {
        const auto low = binEdgeRows[static_cast<size_t> (bin)];
        const auto high = binEdgeRows[static_cast<size_t> (bin + 1)];
        const auto from = std::max (low, -0.5);
        const auto to = std::min (high, top);
        auto value = 0.0f;

        // The band's share outside the canvas's range counts as nothing drawn.
        if (to > from && high > low)
        {
            const auto first = std::clamp (static_cast<int> (std::floor (from + 0.5)), 0, rows - 1);
            const auto last = std::clamp (static_cast<int> (std::ceil (to - 0.5)), first, rows - 1);

            if (mixMax < 1.0f)
                value = static_cast<float> ((integral (to, last) - integral (from, first)) / (high - low));

            if (mixMax > 0.0f)
                value += (*std::max_element (cells + first, cells + last + 1) - value) * mixMax;
        }

        out[bin] = value;
    }

    return true;
}

void TunedCarver::widenAcrossBins (int radius) noexcept
{
    // Each bin carves at least as much as any within `radius` bins of it.
    if (radius <= 0)
        return;

    std::copy (amounts.begin(), amounts.end(), scratch.begin());

    for (int bin = 0; bin < numBins; ++bin)
    {
        const auto from = scratch.begin() + std::max (0, bin - radius);
        const auto to = scratch.begin() + std::min (numBins, bin + radius + 1);
        amounts[static_cast<size_t> (bin)] = *std::max_element (from, to);
    }
}

int TunedCarver::gatherPoints() noexcept
{
    // The drawing over the hop around the frame's centre (the window's centre is the start of
    // hop `centre`: the second half of the hop before it and the first half of that one), at
    // the points there where the gate is open, weighted by the gate (so only what is inside the
    // pass counts, and its fades count less). Neighbouring points in one column are merged.
    const auto centre = hopCount - 1 - options.overlap / 2 + 1;
    const auto half = pointsPerHop / 2;
    auto count = 0;
    auto total = 0.0f;

    const auto read = [&] (const Hop& source, int from, int to)
    {
        for (int p = from; p < to; ++p)
        {
            const auto gate = source.pointGate[p];

            if (! (gate > 0.0f))
                continue;

            const auto column = plan->columnAt (source.pointFraction[p] + columnNudge);
            total += gate;

            if (count > 0 && points[count - 1].column == column)
                points[count - 1].weight += gate;
            else
                points[count++] = { column, gate };
        }
    };

    read (hopAt (centre - 1), half, pointsPerHop);
    read (hopAt (centre), 0, half);

    for (int p = 0; p < count; ++p)
        points[p].weight /= total;

    return count;
}

int TunedCarver::nearestMoment() noexcept
{
    // No point of the frame's own stretch is in the pass, but the pass may still begin or end
    // within the frame: the start of the hop at its centre, or where the gate is shut there,
    // the nearest hop start in the frame where it is open, else the nearest moment in the frame
    // where it opens. A frame reaching over the start or the end of the pass carves as the
    // pass's first or last moment does (the output takes it only where the gate is open); with
    // the gate shut all through the frame it changes nothing.
    const auto newest = hopCount - 1;
    const auto centre = newest - options.overlap / 2 + 1;
    const Hop* moment = nullptr;

    for (const auto atStart : { true, false })
        for (int distance = 0; distance < options.overlap && moment == nullptr; ++distance)
            for (const auto index : { centre + distance, centre - distance })
            {
                const auto& candidate = hopAt (index);

                if (moment == nullptr && index <= newest && index > newest - options.overlap
                    && (atStart ? candidate.openAtStart : candidate.open))
                    moment = &candidate;
            }

    if (moment == nullptr)
        return 0;

    points[0] = { plan->columnAt (moment->fraction + columnNudge), 1.0f };
    return 1;
}

bool TunedCarver::computeGains (const CarveSettings& settings) noexcept
{
    if (plan == nullptr || plan->empty || plan->numColumns <= 0 || plan->numRows <= 0
        || static_cast<size_t> (plan->numRows) >= prefix.size() || settings.depthDb >= 0.0f)
        return false;

    auto numPoints = gatherPoints();

    if (numPoints == 0)
        numPoints = nearestMoment();

    if (numPoints == 0)
        return false;

    lookUpBinRows();

    const auto stencil = settings.mode == CarveMode::stencil;
    const auto loudest = ! stencil && settings.holdLoudest;
    auto drawn = false;

    std::fill (amounts.begin(), amounts.end(), 0.0f);

    // The points' mean (an empty column adds nothing to it), or with holdLoudest their maximum.
    for (int p = 0; p < numPoints; ++p)
    {
        if (! columnAmounts (points[p].column, stencil, columnValues.data()))
            continue;

        drawn = true;
        const auto weight = points[p].weight;

        if (loudest)
            for (int bin = 0; bin < numBins; ++bin)
                amounts[static_cast<size_t> (bin)] = std::max (amounts[static_cast<size_t> (bin)], columnValues[static_cast<size_t> (bin)]);
        else
            for (int bin = 0; bin < numBins; ++bin)
                amounts[static_cast<size_t> (bin)] += weight * columnValues[static_cast<size_t> (bin)];
    }

    // The mean spreads a shape's edge over the frames whose stretch reaches it, half a hop
    // either side of where it is drawn. Where the frame's centre is drawn, it carves at least as
    // much as the centre asks; where it is not but the centre of a frame beside it is, that
    // frame carves the shape and this one leaves it alone (as when the drawing was read at the
    // centres only). Only a stroke that falls between the centres is left to the mean.
    if (! loudest && options.sharpEdges)
    {
        const auto centre = hopCount - 1 - options.overlap / 2 + 1;
        const auto valuesAt = [this, stencil] (const Hop& source, std::vector<float>& out)
        {
            if (source.openAtStart && columnAmounts (plan->columnAt (source.fraction + columnNudge), stencil, out.data()))
                return true;

            std::fill (out.begin(), out.end(), 0.0f);
            return false;
        };

        if (hopAt (centre).openAtStart)
        {
            const auto atCentre = valuesAt (hopAt (centre), columnValues);
            const auto before = valuesAt (hopAt (centre - 1), besideBefore);
            const auto after = valuesAt (hopAt (centre + 1), besideAfter);
            drawn = drawn || atCentre;

            if (atCentre || before || after)
            {
                for (size_t bin = 0; bin < amounts.size(); ++bin)
                {
                    if (columnValues[bin] > 0.0f)
                        amounts[bin] = std::max (amounts[bin], columnValues[bin]);
                    else if (besideBefore[bin] > 0.0f || besideAfter[bin] > 0.0f)
                        amounts[bin] = 0.0f;
                }
            }
        }
    }

    if (stencil)
    {
        // Where nothing is drawn, everything goes.
        for (auto& amount : amounts)
            amount = 1.0f - std::min (amount, 1.0f);
    }
    else
    {
        if (! drawn)
            return false;

        widenAcrossBins (std::clamp (settings.haloBins, 0, 64));
    }

    // gain = depth ^ amount, as a natural exponent per unit of amount
    const auto perAmount = settings.depthDb * 0.11512925f;  // ln (10) / 20

    for (size_t bin = 0; bin < amounts.size(); ++bin)
        gains[bin] = std::exp (perAmount * amounts[bin]);

    return true;
}

void TunedCarver::runFrame (int numChannels, const CarveSettings& settings) noexcept
{
    const auto changes = computeGains (settings);
    untouchedRun = changes ? 0 : std::min (untouchedRun + 1, options.overlap);

    // Every frame over the hop about to be played changed nothing: play the input itself.
    const auto exact = untouchedRun >= options.overlap;
    const auto size = static_cast<size_t> (frameSize);
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
                frame[n] *= analysisWindow[n];

            std::fill (frame + size, frame + 2 * static_cast<size_t> (fftSize), 0.0f);
            fft->performRealOnlyForwardTransform (frame, true);

            for (size_t bin = 0; bin < static_cast<size_t> (numBins); ++bin)
            {
                frame[2 * bin] *= gains[bin];
                frame[2 * bin + 1] *= gains[bin];
            }

            fft->performRealOnlyInverseTransform (frame);

            for (size_t n = 0; n < size; ++n)
                sum[n] += frame[n] * synthesisWindow[n];
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

} // namespace freqart
