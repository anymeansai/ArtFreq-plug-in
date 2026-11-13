#include "LiveView.h"

#include "CanvasAxes.h"
#include "CanvasView.h"
#include "Theme.h"

#include <cmath>

namespace freqart::ui
{
namespace
{
    constexpr int framesPerSecond = 60;        // while showing
    constexpr int pollsPerSecond = 4;          // while visible but not on screen (e.g. minimised)

    // How much the view shows: one pass of the drawing and a quarter more, so the whole drawing
    // is on screen for a while each pass, in whole seconds (a tempo that wobbles does not redraw
    // it), within limits that keep it readable.
    constexpr double spanPerPass = 1.25;
    constexpr double shortestSpan = 4.0;
    constexpr double longestSpan = 20.0;

    double spanForPass (double passSeconds)
    {
        if (! (passSeconds > 0.0))
            return shortestSpan;

        return juce::jlimit (shortestSpan, longestSpan, std::ceil (passSeconds * spanPerPass - 0.05));
    }

    // The colour window, in dBFS as the analyser reads them (a full-scale sine is 0 dB). Tuned
    // so FreqArt's usual output (Level -6 dB, a few hundred lines) reads like the canvas: its
    // loudest lines near the top of the colour map, Contrast 40 dB reaching well into it.
    constexpr float ceilingDb = -29.0f;
    constexpr float floorDb = -89.0f;

    constexpr float slopePivotHz = 1000.0f;

    // Some hosts stop calling the plug-in while their transport is stopped. The output is then
    // silent, so after this long without new columns the view scrolls silence instead of freezing.
    constexpr double stallSeconds = 0.3;

    double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    /** The analyser's slope as a choice index, read like the processor reads it. */
    float slopeOf (FreqArtProcessor& processor)
    {
        const auto* value = processor.getParameters().getRawParameterValue (params::id::slope);
        return value != nullptr ? params::slopeForIndex (juce::roundToInt (value->load())) : 0.0f;
    }
} // namespace

//==============================================================================
struct LiveView::Impl
{
    Impl (LiveView& ownerToUse, FreqArtProcessor& processorToUse)
        : owner (ownerToUse), processor (processorToUse)
    {
        scale = processor.getScale();
        slope = slopeOf (processor);
        span = spanForPass (processor.getLengthSeconds());
    }

    juce::Rectangle<float> area() const
    {
        return axes::plotArea (owner.getLocalBounds(), CanvasView::axisWidth, CanvasView::axisHeight).toFloat();
    }

    // ---- the analysed columns --------------------------------------------------------------

    /** Makes room for `span` seconds of columns at a sample rate. Another rate clears the
        history; another span keeps the newest columns that still fit. */
    void prepareHistory (double sampleRate)
    {
        const auto needed = static_cast<int> (std::ceil (span * sampleRate / LiveAnalyzer::hopSize)) + 2;

        if (juce::exactlyEqual (sampleRate, historyRate) && capacity == needed)
            return;

        const auto bins = static_cast<size_t> (LiveAnalyzer::numBins);
        std::vector<float> kept;
        auto keptCount = 0;

        if (juce::exactlyEqual (sampleRate, historyRate) && capacity > 0)
        {
            keptCount = juce::jmin (filled, needed);
            kept.resize (static_cast<size_t> (keptCount) * bins);

            for (int i = 0; i < keptCount; ++i)   // oldest first
            {
                const auto* source = column (columnIndexForAge (keptCount - 1 - i));
                std::copy (source, source + bins, kept.begin() + static_cast<std::ptrdiff_t> (static_cast<size_t> (i) * bins));
            }
        }

        historyRate = sampleRate;
        capacity = needed;
        history.assign (static_cast<size_t> (capacity) * bins, -140.0f);
        peaks.assign (static_cast<size_t> (capacity), -140.0f);
        std::copy (kept.begin(), kept.end(), history.begin());
        filled = keptCount;
        writeIndex = keptCount % capacity;
        newColumns.clear();
        imageDirty = true;
    }

    /** Follows the drawing's length (and the tempo): returns true if the span changed. */
    bool updateSpan()
    {
        const auto newSpan = spanForPass (processor.getLengthSeconds());

        if (juce::exactlyEqual (newSpan, span))
            return false;

        span = newSpan;

        if (capacity > 0)
            prepareHistory (historyRate);

        imageDirty = true;
        return true;
    }

    float* column (int index) noexcept
    {
        return history.data() + static_cast<size_t> (index) * static_cast<size_t> (LiveAnalyzer::numBins);
    }

    /** Index of the column `age` columns before the newest (age 0 = newest). */
    int columnIndexForAge (int age) const noexcept
    {
        return ((writeIndex - 1 - age) % capacity + capacity) % capacity;
    }

    void addColumn (const float* magnitudesDb, int numBins)
    {
        const auto count = juce::jmin (numBins, LiveAnalyzer::numBins);
        auto* target = column (writeIndex);
        std::copy (magnitudesDb, magnitudesDb + count, target);
        std::fill (target + count, target + LiveAnalyzer::numBins, -140.0f);

        peaks[static_cast<size_t> (writeIndex)] = -140.0f;
        writeIndex = (writeIndex + 1) % capacity;
        filled = juce::jmin (filled + 1, capacity);
        newColumns.push_back (columnIndexForAge (0));
    }

    // ---- columns to pixels -----------------------------------------------------------------

    /** How each image row reads the spectrum: the loudest bin it covers, or (where rows are finer
        than bins, in the lows) the spectrum interpolated at the row's centre. */
    struct RowSource
    {
        int first = 0, last = -1;  // bins covered (inclusive); last < first: interpolate
        float position = 0.0f;     // fractional bin to interpolate at
        float offsetDb = 0.0f;     // the analyser's slope at this height
        bool valid = false;        // false above Nyquist
    };

    void updateRows (int height)
    {
        rows.assign (static_cast<size_t> (height), {});

        const auto s = scale.sanitised();
        const auto binHz = historyRate / LiveAnalyzer::fftSize;
        const auto nyquist = historyRate * 0.5;
        const auto h = static_cast<double> (height);

        for (int y = 0; y < height; ++y)
        {
            auto& row = rows[static_cast<size_t> (y)];
            const auto top = s.toHz (1.0 - static_cast<double> (y) / h);
            const auto bottom = s.toHz (1.0 - static_cast<double> (y + 1) / h);
            const auto centre = s.toHz (1.0 - (static_cast<double> (y) + 0.5) / h);

            if (! (centre > 0.0) || centre >= nyquist)
                continue;

            row.valid = true;
            row.offsetDb = slope * static_cast<float> (std::log2 (centre / slopePivotHz));
            row.first = juce::jmax (0, static_cast<int> (std::ceil (bottom / binHz)));
            row.last = juce::jmin (LiveAnalyzer::numBins - 1, static_cast<int> (std::floor (top / binHz)));
            row.position = static_cast<float> (juce::jlimit (0.0, static_cast<double> (LiveAnalyzer::numBins - 2), centre / binHz));
        }
    }

    /** Colour-map levels (0..255) of one column for every image row; also notes its loudest
        reading (for the "nothing playing" hint). */
    void levelsOf (int columnIndex, std::vector<uint8_t>& out)
    {
        const auto* db = column (columnIndex);
        out.resize (rows.size());
        auto loudest = -140.0f;

        for (size_t y = 0; y < rows.size(); ++y)
        {
            const auto& row = rows[y];

            if (! row.valid)
            {
                out[y] = 0;
                continue;
            }

            float value;

            if (row.last >= row.first)
            {
                value = db[row.first];

                for (auto bin = row.first + 1; bin <= row.last; ++bin)
                    value = std::max (value, db[bin]);
            }
            else
            {
                const auto i = static_cast<int> (row.position);
                const auto frac = row.position - static_cast<float> (i);
                value = db[i] + (db[i + 1] - db[i]) * frac;
            }

            value += row.offsetDb;
            loudest = std::max (loudest, value);
            const auto t = (value - floorDb) / (ceilingDb - floorDb);
            out[y] = static_cast<uint8_t> (juce::jlimit (0, 255, static_cast<int> (t * 255.0f + 0.5f)));
        }

        peaks[static_cast<size_t> (columnIndex)] = loudest;
    }

    /** Paints image columns [x, x + width) with the levels (width 0: merges them into column
        x - 1, keeping the louder, for columns narrower than a pixel). */
    void paintColumn (int x, int width, const std::vector<uint8_t>& levels)
    {
        const auto& table = theme::spectrumTable();
        const auto merge = width == 0;

        if (merge)
        {
            --x;
            width = 1;
        }

        if (x < 0 || x + width > image.getWidth())
            return;

        juce::Image::BitmapData data (image, x, 0, width, image.getHeight(), juce::Image::BitmapData::writeOnly);
        const auto argb = data.pixelFormat == juce::Image::ARGB;

        for (int y = 0; y < image.getHeight(); ++y)
        {
            auto level = levels[static_cast<size_t> (y)];

            if (merge)
                level = std::max (level, rowLevelCache[static_cast<size_t> (y)]);

            const auto colour = table[level];
            auto* pixel = data.getLinePointer (y);

            for (int i = 0; i < width; ++i, pixel += data.pixelStride)
            {
                if (argb)
                    reinterpret_cast<juce::PixelARGB*> (pixel)->set (colour);
                else
                    reinterpret_cast<juce::PixelRGB*> (pixel)->set (colour);
            }
        }

        // What the last pixel column shows, for merging the next sub-pixel column into it.
        if (merge)
            for (size_t y = 0; y < levels.size(); ++y)
                rowLevelCache[y] = std::max (rowLevelCache[y], levels[y]);
        else
            rowLevelCache = levels;
    }

    float pixelsPerColumn() const
    {
        const auto columnsVisible = span * historyRate / LiveAnalyzer::hopSize;
        return static_cast<float> (image.getWidth() / columnsVisible);
    }

    /** Redraws the whole picture from the history (after a resize or a settings change). */
    void rebuildImage (int width, int height)
    {
        if (! image.isValid() || image.getWidth() != width || image.getHeight() != height)
            image = juce::Image (juce::Image::RGB, width, height, false);  // opaque: drawn by copying

        image.clear (image.getBounds(), theme::spectrum (0.0f));
        rowLevelCache.assign (static_cast<size_t> (height), 0);
        updateRows (height);
        newColumns.clear();
        advance = 0.0f;

        if (capacity == 0)
            return;

        const auto ppc = pixelsPerColumn();

        // Oldest first, so sub-pixel columns merge into the right neighbours.
        const auto count = juce::jmin (filled, static_cast<int> (std::ceil (width / juce::jmax (0.01f, ppc))) + 1);

        for (int age = count - 1; age >= 0; --age)
        {
            const auto right = juce::roundToInt (static_cast<float> (width) - static_cast<float> (age) * ppc);
            const auto left = juce::roundToInt (static_cast<float> (width) - static_cast<float> (age + 1) * ppc);
            levelsOf (columnIndexForAge (age), scratchLevels);

            if (right <= 0)
                continue;

            const auto from = juce::jmax (0, left);
            paintColumn (from, juce::jmax (0, right - from), scratchLevels);
        }

        imageDirty = false;
    }

    /** Scrolls the picture left by the new columns and paints them in on the right. */
    void scrollInNewColumns()
    {
        if (newColumns.empty() || imageDirty || ! image.isValid() || static_cast<int> (newColumns.size()) >= capacity)
        {
            // (More columns than the history holds: their slots were reused. Redraw it all.)
            imageDirty = imageDirty || ! newColumns.empty();
            newColumns.clear();
            return;
        }

        const auto ppc = pixelsPerColumn();
        const auto width = image.getWidth();
        std::vector<int> widths;
        widths.reserve (newColumns.size());
        auto total = 0;

        for (size_t i = 0; i < newColumns.size(); ++i)
        {
            advance += ppc;
            const auto w = static_cast<int> (advance);
            advance -= static_cast<float> (w);
            widths.push_back (w);
            total += w;
        }

        total = juce::jmin (total, width);

        if (total > 0 && total < width)
            image.moveImageSection (0, 0, total, 0, width - total, image.getHeight());

        auto x = width - total;

        for (size_t i = 0; i < newColumns.size(); ++i)
        {
            levelsOf (newColumns[i], scratchLevels);
            const auto w = juce::jmin (widths[i], width - x);
            paintColumn (juce::jmax (0, x), juce::jmax (0, w), scratchLevels);
            x += juce::jmax (0, w);
        }

        newColumns.clear();
    }

    bool hasSignal() const
    {
        for (int age = 0; age < filled; ++age)
            if (peaks[static_cast<size_t> (columnIndexForAge (age))] > floorDb + 6.0f)
                return true;

        return false;
    }

    // ---- turning the analyser on and off ---------------------------------------------------

    void updateActive()
    {
        const auto showing = owner.isShowing();

        if (showing != active)
        {
            active = showing;
            processor.getAnalyzer().setActive (active);

            // Samples left from an earlier showing are stale: drop them.
            if (active)
            {
                processor.getAnalyzer().process ([] (const float*, int) {});
                lastColumnTime = nowSeconds();
            }
        }

        if (active)
            owner.startTimerHz (framesPerSecond);
        else if (owner.isVisible())
            owner.startTimerHz (pollsPerSecond);  // to notice being shown again (e.g. un-minimised)
        else
            owner.stopTimer();
    }

    void tick()
    {
        if (owner.isShowing() != active)
            updateActive();

        if (! active)
            return;

        auto& analyzer = processor.getAnalyzer();
        const auto spanChanged = updateSpan();
        prepareHistory (analyzer.getSampleRate());

        const auto newScale = processor.getScale();
        const auto newSlope = slopeOf (processor);

        if (spanChanged || newScale != scale || ! juce::exactlyEqual (newSlope, slope))
        {
            scale = newScale;
            slope = newSlope;
            imageDirty = true;
            owner.repaint();
        }

        const auto hadSignal = signalShown;
        auto added = analyzer.process ([this] (const float* magnitudesDb, int numBins) { addColumn (magnitudesDb, numBins); });
        const auto now = nowSeconds();

        if (added > 0)
        {
            lastColumnTime = now;
        }
        else if (historyRate > 0.0 && now - lastColumnTime > stallSeconds)
        {
            const auto secondsPerColumn = LiveAnalyzer::hopSize / historyRate;
            const auto columns = juce::jmin (capacity, static_cast<int> ((now - lastColumnTime) / secondsPerColumn));

            if (silence.size() != static_cast<size_t> (LiveAnalyzer::numBins))
                silence.assign (static_cast<size_t> (LiveAnalyzer::numBins), -140.0f);

            for (int i = 0; i < columns; ++i)
                addColumn (silence.data(), LiveAnalyzer::numBins);

            lastColumnTime = columns >= capacity ? now : lastColumnTime + columns * secondsPerColumn;
            added = columns;
        }

        if (added > 0)
        {
            scrollInNewColumns();
            signalShown = hasSignal();
            owner.repaint (signalShown != hadSignal ? owner.getLocalBounds() : area().getSmallestIntegerContainer());
        }
    }

    // ---- painting --------------------------------------------------------------------------

    void paint (juce::Graphics& g)
    {
        g.fillAll (theme::bg);

        const auto a = area();

        if (a.isEmpty())
            return;

        const auto pixelScale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const auto width = juce::jmax (1, juce::roundToInt (a.getWidth() * pixelScale));
        const auto height = juce::jmax (1, juce::roundToInt (a.getHeight() * pixelScale));

        if (imageDirty || ! image.isValid() || image.getWidth() != width || image.getHeight() != height)
        {
            if (capacity == 0)
            {
                updateSpan();
                prepareHistory (processor.getAnalyzer().getSampleRate());
            }

            rebuildImage (width, height);
            signalShown = hasSignal();
        }

        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImage (image, a);

        axes::drawFrequencyGrid (g, a, scale, juce::Colours::white.withAlpha (0.06f));
        paintSecondsGrid (g, a);

        g.setColour (theme::line);
        g.drawRect (a.expanded (1.0f), 1.0f);

        axes::drawFrequencyLabels (g, { 0.0f, a.getY(), a.getX(), a.getHeight() }, a, scale);
        paintSecondsAxis (g, { a.getX(), a.getBottom() + 1.0f, a.getWidth(), static_cast<float> (CanvasView::axisHeight) - 1.0f }, a);

        paintBadge (g, a);

        if (! signalShown)
        {
            g.setColour (theme::textDim);
            g.setFont (fonts::ui (theme::controlSize));
            g.drawText ("Nothing is playing. Press Preview, or start your DAW.", a, juce::Justification::centred, false);
        }
    }

    float xForSecondsAgo (double seconds, juce::Rectangle<float> a) const
    {
        return a.getRight() - static_cast<float> (seconds / span) * a.getWidth();
    }

    /** Seconds between grid lines and labels: 1, 2 or 5, whichever leaves room for a label. */
    int secondsStep (juce::Rectangle<float> a) const
    {
        const auto pixelsPerSecond = a.getWidth() / static_cast<float> (span);

        for (const auto step : { 1, 2, 5 })
            if (pixelsPerSecond * static_cast<float> (step) >= 56.0f)
                return step;

        return 10;
    }

    void paintSecondsGrid (juce::Graphics& g, juce::Rectangle<float> a)
    {
        g.setColour (juce::Colours::white.withAlpha (0.035f));
        const auto step = secondsStep (a);

        for (int s = step; s < static_cast<int> (span); s += step)
            g.fillRect (std::floor (xForSecondsAgo (s, a)), a.getY(), 1.0f, a.getHeight());
    }

    void paintSecondsAxis (juce::Graphics& g, juce::Rectangle<float> axis, juce::Rectangle<float> a)
    {
        g.setFont (axes::labelFont());
        const auto step = secondsStep (a);

        for (int s = 0; s <= static_cast<int> (span); s += step)
        {
            const auto x = std::floor (xForSecondsAgo (s, a));
            g.setColour (theme::line);
            g.fillRect (x, axis.getY(), 1.0f, 4.0f);

            if (x < a.getX() + 40.0f)
                continue;  // at the left edge: no room for a label

            const auto label = s == 0 ? juce::String ("now") : "-" + juce::String (s) + " s";
            g.setColour (s == 0 ? theme::textDim : axes::labelColour());
            g.drawText (label, juce::Rectangle<float> (x - 52.0f, axis.getY() + 2.0f, 48.0f, axis.getHeight() - 4.0f),
                        juce::Justification::centredRight, false);
        }
    }

    void paintBadge (juce::Graphics& g, juce::Rectangle<float> a)
    {
        const auto font = theme::labelFont();
        const auto text = juce::String ("LIVE");
        const auto textWidth = juce::GlyphArrangement::getStringWidth (font, text);
        const auto badge = juce::Rectangle<float> (textWidth + 26.0f, 20.0f)
                               .withPosition (a.getRight() - textWidth - 26.0f - 10.0f, a.getY() + 10.0f);

        g.setColour (theme::panel.withAlpha (0.88f));
        g.fillRoundedRectangle (badge, 10.0f);
        g.setColour (theme::accent2.withAlpha (0.55f));
        g.drawRoundedRectangle (badge.reduced (0.5f), 10.0f, 1.0f);

        // The dot is full while columns arrive, dim when the output is silent.
        g.setColour (signalShown ? theme::accent2 : theme::accent2.withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ badge.getX() + 12.0f, badge.getCentreY() }));

        g.setColour (theme::accent2);
        g.setFont (font);
        g.drawText (text, badge.withTrimmedLeft (19.0f), juce::Justification::centredLeft, false);
    }

    // ---- the snapshot tool ------------------------------------------------------------------

    void fillFromPlan (double seconds)
    {
        const auto plan = processor.getCurrentPlan();

        if (plan == nullptr || ! (seconds > 0.0))
            return;

        // At the processor's analysis rate, so the history survives the view being shown.
        const auto analysisRate = processor.getAnalyzer().getSampleRate();
        const auto sampleRate = analysisRate > 0.0 ? analysisRate : 48000.0;
        constexpr int blockSize = 512;

        auto engine = std::make_unique<ArtEngine>();
        engine->prepare (sampleRate, blockSize);
        engine->setPlan (plan.get());

        auto analyzer = std::make_unique<LiveAnalyzer>();
        analyzer->prepare (sampleRate);
        analyzer->setActive (true);

        updateSpan();
        prepareHistory (sampleRate);

        // One pass of the drawing at its real speed, ending exactly at "now", after silence if
        // the time is longer than the pass: what the view shows once the DAW has played it.
        const auto passSeconds = processor.getLengthSeconds();
        const auto fractionPerSample = 1.0 / (passSeconds * sampleRate);
        const auto totalSamples = static_cast<int> (seconds * sampleRate);
        const auto passStart = static_cast<int64_t> (totalSamples) - static_cast<int64_t> (std::llround (passSeconds * sampleRate));
        const auto gain = processor.getOutputGain();

        std::vector<float> block (static_cast<size_t> (blockSize));
        auto started = false;

        for (int done = 0; done < totalSamples; done += blockSize)
        {
            const auto n = juce::jmin (blockSize, totalSamples - done);
            const auto silent = static_cast<int> (juce::jlimit (static_cast<int64_t> (0), static_cast<int64_t> (n), passStart - done));

            PlaySegment segments[2];
            auto count = 0;

            if (silent > 0)
            {
                auto& quiet = segments[count++];
                quiet.start = 0;
                quiet.length = silent;
                quiet.playing = false;
                quiet.fractionPerSample = fractionPerSample;
            }

            if (silent < n)
            {
                auto& playing = segments[count++];
                playing.start = silent;
                playing.length = n - silent;
                playing.playing = true;
                playing.fraction = static_cast<double> (done + silent - passStart) * fractionPerSample;
                playing.fractionPerSample = fractionPerSample;
                playing.jump = ! started;
                started = true;
            }

            engine->render (block.data(), n, segments, count);

            for (int i = 0; i < n; ++i)
                block[static_cast<size_t> (i)] *= gain;

            analyzer->push (block.data(), n);
            analyzer->process ([this] (const float* magnitudesDb, int numBins) { addColumn (magnitudesDb, numBins); });
        }

        scrollInNewColumns();
        signalShown = hasSignal();
        owner.repaint();
    }

    void fillFromAudio (const std::vector<float>& audio, double sampleRate)
    {
        if (audio.empty() || ! (sampleRate > 0.0))
            return;

        auto analyzer = std::make_unique<LiveAnalyzer>();
        analyzer->prepare (sampleRate);
        analyzer->setActive (true);

        updateSpan();
        prepareHistory (sampleRate);

        constexpr int blockSize = 512;

        for (size_t done = 0; done < audio.size(); done += blockSize)
        {
            const auto n = static_cast<int> (std::min (static_cast<size_t> (blockSize), audio.size() - done));
            analyzer->push (audio.data() + done, n);
            analyzer->process ([this] (const float* magnitudesDb, int numBins) { addColumn (magnitudesDb, numBins); });
        }

        scrollInNewColumns();
        signalShown = hasSignal();
        owner.repaint();
    }

    // ---- state ---------------------------------------------------------------------------

    LiveView& owner;
    FreqArtProcessor& processor;
    FrequencyScale scale;
    float slope = 0.0f;
    double span = shortestSpan;    // seconds across the view (spanForPass)
    bool active = false;
    bool signalShown = false;

    // Analysed columns (dB per bin), a ring of `capacity`
    std::vector<float> history;
    std::vector<float> peaks;  // the loudest row of each column as last drawn
    double historyRate = 0.0;
    int capacity = 0, filled = 0, writeIndex = 0;
    std::vector<int> newColumns;
    double lastColumnTime = 0.0;   // when the last column arrived (seconds, hi-res counter)
    std::vector<float> silence;    // a silent column, for a host that has stopped calling us

    // The picture, scrolled as columns arrive
    juce::Image image;
    bool imageDirty = true;
    float advance = 0.0f;  // sub-pixel scroll carried to the next column
    std::vector<RowSource> rows;
    std::vector<uint8_t> scratchLevels, rowLevelCache;
};

//==============================================================================
LiveView::LiveView (FreqArtProcessor& processor)
    : impl (std::make_unique<Impl> (*this, processor))
{
    setOpaque (true);
    setTitle ("Live spectrogram");
    setDescription ("FreqArt's own output as a scrolling spectrogram.");
    setInterceptsMouseClicks (false, false);
}

LiveView::~LiveView()
{
    stopTimer();

    if (impl->active)
        impl->processor.getAnalyzer().setActive (false);
}

void LiveView::paint (juce::Graphics& g)
{
    impl->paint (g);
}

void LiveView::resized()
{
    impl->imageDirty = true;
    repaint();
}

void LiveView::visibilityChanged()
{
    impl->updateActive();
}

void LiveView::parentHierarchyChanged()
{
    impl->updateActive();
}

double LiveView::getVisibleSeconds() const
{
    return spanForPass (impl->processor.getLengthSeconds());
}

void LiveView::fillFromPlanForSnapshot (double seconds)
{
    impl->fillFromPlan (seconds);
}

void LiveView::fillFromAudioForSnapshot (const std::vector<float>& audio, double sampleRate)
{
    impl->fillFromAudio (audio, sampleRate);
}

void LiveView::timerCallback()
{
    impl->tick();
}

} // namespace freqart::ui
