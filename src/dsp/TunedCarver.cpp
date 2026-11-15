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

namespace artfreq
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
    options.pointSeconds = std::max (16.0 / 48000.0, options.pointSeconds);

    char text[200];
    std::snprintf (text, sizeof (text), "tuned STFT %d (%.1f ms), sin^%g windows, %d x overlap, Carve: mean per bin, "
                                        "the drawing read every %.2f ms",
                   juce::roundToInt (options.frameSeconds * 48000.0), options.frameSeconds * 1000.0,
                   static_cast<double> (options.windowPower), options.overlap, options.pointSeconds * 1000.0);
    name = text;
}

void TunedCarver::prepareCarver()
{
    // The same frame duration at any rate, a whole number of hops long.
    frameSize = std::max (options.overlap * 16,
                          juce::roundToInt (options.frameSeconds * sampleRate / options.overlap) * options.overlap);
    hop = frameSize / options.overlap;

    // Points about pointSeconds apart (the same number at any rate: 8 a hop), an even number of
    // them (the frame reads the second half of one hop and the first half of the next), each at
    // the centre of its share of the hop.
    pointsPerHop = std::clamp (2 * juce::roundToInt (0.5 * hop / (options.pointSeconds * sampleRate)), 2, maxPointsPerHop);

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
    binRowsCount = -1;   // looked up when a frame first reads a plan
    binsInRange = 0;

    channelState.resize (static_cast<size_t> (maxChannels));

    for (auto& state : channelState)
    {
        state.history.assign (static_cast<size_t> (frameSize), 0.0f);
        state.sum.assign (static_cast<size_t> (frameSize), 0.0f);
        state.out.assign (static_cast<size_t> (hop), 0.0f);
    }

    gateHistory.assign (static_cast<size_t> (frameSize), 0.0f);
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
    next = NextGains();
}

void TunedCarver::lookUpBinRows() noexcept
{
    // For the plan the next frame reads, only when its scale changed (rowAt is a log per bin
    // edge on the log scale).
    const auto* source = next.plan;

    if (source == nullptr || binEdgeRows.size() != static_cast<size_t> (numBins + 1)
        || (binRowsCount == source->numRows && binRowsScale == source->scale))
        return;

    const auto binHz = sampleRate / fftSize;

    for (int edge = 0; edge <= numBins; ++edge)
        binEdgeRows[static_cast<size_t> (edge)] = source->rowAt (std::max (0.0, (edge - 0.5) * binHz));

    // A bin whose band starts at the top of the scale or above it carves nothing (the canvas
    // shows nothing there): at high rates most of them, which are then never looked at.
    const auto top = static_cast<double> (source->numRows) - 0.5;
    binsInRange = numBins;

    while (binsInRange > 0 && binEdgeRows[static_cast<size_t> (binsInRange - 1)] >= top)
        --binsInRange;

    binRowsScale = source->scale;
    binRowsCount = source->numRows;
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
            advanceGains (settings, true);
            runFrame (numChannels);
            hopFill = 0;
        }
        else
        {
            advanceGains (settings, false);
        }
    }
}

bool TunedCarver::columnAmounts (int column, bool stencil, float* out) noexcept
{
    // Per bin, the drawing's mean over the bin's band (and in Stencil its maximum there), from
    // one column. Returns false, with every value 0, if that column is empty.
    const auto* source = next.plan;
    work.columnsRead = std::min (work.columnsRead + 1, Work::most);

    if (! source->isColumnUsed (column))
    {
        std::fill (out, out + numBins, 0.0f);
        return false;
    }

    const auto* cells = source->column (column);
    const auto rows = source->numRows;
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

    std::fill (out + binsInRange, out + numBins, 0.0f);

    for (int bin = 0; bin < binsInRange; ++bin)
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

    // Above the bins in range and `radius` more, everything stays 0.
    for (int bin = 0, end = std::min (numBins, binsInRange + radius); bin < end; ++bin)
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

            const auto column = next.plan->columnAt (source.pointFraction[p] + columnNudge);
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

    points[0] = { next.plan->columnAt (moment->fraction + columnNudge), 1.0f };
    return 1;
}

void TunedCarver::advanceGains (const CarveSettings& settings, bool hopComplete) noexcept
{
    // The next frame's gains are worked out while its newest hop comes in, a stage (one column of
    // the drawing, or the last step) at a time, so the callback that completes the hop has only
    // the transforms left to do. Before then, one stage ahead of an even share of the hop: done
    // by the time the hop is, unless a block is longer than a stage's share of it. Once it is
    // complete, whatever is left. The result is the same however the work is cut up.
    if (next.stage == 0)
        startGains (settings);

    while (! next.complete)
    {
        if (! hopComplete)
        {
            // Waiting for the newest hop: settled once the gate opens in it (or the hop ends).
            if (next.waiting && ! hopAt (hopCount - 1).open)
                return;

            const auto due = static_cast<int> (static_cast<int64_t> (next.numStages) * hopFill / hop) + 1;

            if (next.stage >= std::min (next.numStages, due))
                return;
        }

        gainsStage();
    }
}

void TunedCarver::startGains (const CarveSettings& settings) noexcept
{
    // The first stage, once the newest hop of the frame has begun: the frame reads the plan and
    // the settings as they are now to the end (a plan set meanwhile is kept alive by the caller
    // while getFadingPlan() returns this one), and the drawing at the points of the hops on
    // either side of its centre, which are complete.
    next = NextGains();
    next.stage = 1;
    next.plan = plan;
    next.settings = settings;

    if (plan == nullptr || plan->empty || plan->numColumns <= 0 || plan->numRows <= 0
        || static_cast<size_t> (plan->numRows) >= prefix.size() || settings.depthDb >= 0.0f)
    {
        next.complete = true;
        return;
    }

    lookUpBinRows();
    std::fill (amounts.begin(), amounts.end(), 0.0f);

    const auto stencil = settings.mode == CarveMode::stencil;
    const auto loudest = ! stencil && settings.holdLoudest;
    const auto centre = hopCount - 1 - options.overlap / 2 + 1;

    // The mean spreads a shape's edge over the frames whose stretch reaches it: the centres
    // decide there (see finishGains). They are known now, the newest hop's from its first sample.
    next.sharp = ! loudest && options.sharpEdges && hopAt (centre).openAtStart;
    next.numPoints = gatherPoints();

    // No point of the frame's stretch is in the pass: what it carves is the pass's nearest
    // moment, which may be where the gate opens in the newest hop (known once it is complete).
    next.waiting = next.numPoints == 0;
    next.numStages = 1 + next.numPoints + (next.sharp ? 3 : 0) + 1;
}

void TunedCarver::gainsStage() noexcept
{
    if (next.waiting)
    {
        // The pass's nearest moment in the frame (nearestMoment) depends on whether the gate opens
        // in the newest hop, and where: known once it has opened (from then on nothing about the
        // hop changes) or once the hop is complete.
        next.waiting = false;
        next.numPoints = nearestMoment();

        if (next.numPoints == 0)
        {
            next.complete = true;   // the gate is shut all through the frame: it changes nothing
            return;
        }

        next.numStages = 1 + next.numPoints + (next.sharp ? 3 : 0) + 1;
    }

    const auto stage = next.stage++;
    const auto stencil = next.settings.mode == CarveMode::stencil;
    const auto inRange = static_cast<size_t> (binsInRange);

    if (stage <= next.numPoints)
    {
        // A point: the points' mean (an empty column adds nothing to it), or with holdLoudest
        // their maximum.
        const auto& point = points[stage - 1];

        if (! columnAmounts (point.column, stencil, columnValues.data()))
            return;

        next.drawn = true;

        if (! stencil && next.settings.holdLoudest)
            for (size_t bin = 0; bin < inRange; ++bin)
                amounts[bin] = std::max (amounts[bin], columnValues[bin]);
        else
            for (size_t bin = 0; bin < inRange; ++bin)
                amounts[bin] += point.weight * columnValues[bin];

        return;
    }

    if (stage < next.numStages - 1)
    {
        // The drawing at the centre of this frame and of the frames either side.
        const auto centre = hopCount - 1 - options.overlap / 2 + 1;
        const auto side = stage - next.numPoints - 1;   // 0: this frame, 1: the one before, 2: after
        const auto& source = hopAt (centre + (side == 0 ? 0 : (side == 1 ? -1 : 1)));
        auto& out = side == 0 ? columnValues : (side == 1 ? besideBefore : besideAfter);
        auto found = false;

        if (source.openAtStart)
            found = columnAmounts (next.plan->columnAt (source.fraction + columnNudge), stencil, out.data());

        if (! found)
            std::fill (out.begin(), out.end(), 0.0f);

        (side == 0 ? next.atCentre : (side == 1 ? next.atBefore : next.atAfter)) = found;
        return;
    }

    finishGains();
}

void TunedCarver::finishGains() noexcept
{
    const auto stencil = next.settings.mode == CarveMode::stencil;
    const auto inRange = static_cast<size_t> (binsInRange);
    next.complete = true;

    // At a shape's edges the centres decide: where the frame's centre is drawn, it carves at
    // least as much as the centre asks; where it is not but the centre of a frame beside it is,
    // that frame carves the shape and this one leaves it alone (as when the drawing was read at
    // the centres only). Only a stroke that falls between the centres is left to the mean.
    if (next.sharp)
    {
        next.drawn = next.drawn || next.atCentre;

        if (next.atCentre || next.atBefore || next.atAfter)
        {
            for (size_t bin = 0; bin < inRange; ++bin)
            {
                if (columnValues[bin] > 0.0f)
                    amounts[bin] = std::max (amounts[bin], columnValues[bin]);
                else if (besideBefore[bin] > 0.0f || besideAfter[bin] > 0.0f)
                    amounts[bin] = 0.0f;
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
        if (! next.drawn)
            return;

        widenAcrossBins (std::clamp (next.settings.haloBins, 0, 64));
    }

    // gain = depth ^ amount, as a natural exponent per unit of amount (most bins are carved by
    // none or all of it)
    const auto perAmount = next.settings.depthDb * 0.11512925f;  // ln (10) / 20
    const auto all = std::exp (perAmount);

    for (size_t bin = 0; bin < amounts.size(); ++bin)
    {
        const auto amount = amounts[bin];
        gains[bin] = juce::exactlyEqual (amount, 0.0f) ? 1.0f
                   : (juce::exactlyEqual (amount, 1.0f) ? all : std::exp (perAmount * amount));
    }

    next.changes = true;
}

void TunedCarver::runFrame (int numChannels) noexcept
{
    // The gains are complete (advanceGains); the next frame's are worked out from the next hop on.
    const auto changes = next.complete && next.changes;
    next = NextGains();

    if (changes)
        work.framesTransformed = std::min (work.framesTransformed + 1, Work::most);

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

} // namespace artfreq
