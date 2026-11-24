// The drip check, a part of ArtFreqLab (included by LowsLab.cpp, whose helpers it uses):
//
//   ArtFreqLab <output-dir> --drip [--lows stripes|smooth]
//
// The customer, on 0.5: "There's like a 'drip' from the image or text that drips into the lower
// frequencies, even if the canvas doesn't have anything drawn down there." This measures it, in
// the real output, with an analysis that adds nothing of its own:
//
//   1. draws eight drawings with something low in them and nothing under it (see dripDrawings);
//   2. builds each one's plan at Density 256 and plays it with the real ArtEngine, looped, as a
//      DAW plays it (8 s, 48 kHz);
//   3. BELOW and ABOVE: an STFT of 65536 points (0.73 Hz a bin) with a 4-term Blackman-Harris
//      window (side lobes 92 dB down, so a clean tone shows as nothing but its main lobe, 4.5 bins
//      either side). For each frame, the lowest and the highest frequency drawn anywhere under
//      its window; everything more than a sixth of an octave (and that main lobe) below the
//      lowest is "below the drawing", and likewise above. Reported relative to the drawing: the
//      share of the sound's energy there (mean), and the loudest cell there against the loudest
//      cell of the drawing (peak: what the eye finds first);
//   4. BEFORE AND AFTER: the same window at 4096 points (85 ms). Frames with nothing drawn within
//      their window and 0.15 s more either side (the longest fade of a low line is 0.25 s,
//      centred on the edge) are "outside the drawing in time": their mean power against the mean
//      power of the frames with something drawn at their centre, and their loudest cell against
//      the drawing's;
//   5. writes drip.txt (the table, also printed) and, per drawing, what an analyser shows of it:
//      <name>-hann4096.png and <name>-hann8192.png (Hann, log 20 Hz - 20 kHz, the colours over
//      -100 .. 0 dBFS at Level -6 dB, as ArtFreq 0.6's live view), <name>-hann8192-floor80.png
//      (the same over -80 .. 0 dBFS), <name>-bh4096.png and <name>-bh8192.png (Blackman-Harris,
//      -100 .. 0 dBFS: as the canvas and the live view draw it now, see dsp/SpectrumAnalysis.h)
//      and <name>-canvas.png (the drawing).

struct DripDrawing
{
    juce::String name, description;
    Canvas canvas;
};

void dripFill (Canvas& canvas, double lowHz, double highHz, double from, double to, float value = 1.0f)
{
    const auto top = juce::jlimit (0, Canvas::height - 1, static_cast<int> (std::ceil (canvasYForHz (highHz))));
    const auto bottom = juce::jlimit (0, Canvas::height - 1, static_cast<int> (std::floor (canvasYForHz (lowHz))) - 1);
    const auto left = juce::jlimit (0, Canvas::width - 1, static_cast<int> (std::lround (from * Canvas::width)));
    const auto right = juce::jlimit (0, Canvas::width - 1, static_cast<int> (std::lround (to * Canvas::width)) - 1);

    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x)
            canvas.at (x, y) = std::max (canvas.at (x, y), value);
}

void dripText (Canvas& canvas, const char* text, Band band)
{
    const FrequencyScale scale;
    ArtDocument document;
    const auto range = bandRange (band, scale);
    auto object = ArtDocument::makeTextObject (text, static_cast<float> (range.low), static_cast<float> (range.high));
    object.startTime = 0.2f;
    object.endTime = 0.8f;
    document.objects.push_back (object);

    Compositor compositor;
    compositor.render (document, scale, canvas);
}

std::vector<DripDrawing> dripDrawings (const Options& options)
{
    std::vector<DripDrawing> drawings;

    const auto add = [&drawings] (const char* name, const char* description) -> Canvas&
    {
        drawings.push_back ({ name, description, {} });
        drawings.back().canvas.clear();
        return drawings.back().canvas;
    };

    {
        // One canvas row: a single steady sine, the simplest thing there is to look clean.
        auto& canvas = add ("tone", "one canvas row at 100 Hz, 25 - 75 % of the time: a single steady tone");
        const auto y = static_cast<int> (std::floor (canvasYForHz (100.0)));

        for (int x = Canvas::width / 4; x < Canvas::width * 3 / 4; ++x)
            canvas.at (x, y) = 1.0f;
    }

    dripFill (add ("block", "a filled block, 60 - 250 Hz, 25 - 75 % of the time"), 60.0, 250.0, 0.25, 0.75);
    dripText (add ("low", "the word LOW in the Low band (40 - 250 Hz)"), "LOW", Band::low);

    {
        // A stroke three rows thick with a soft row either side, waving between 130 and 350 Hz.
        auto& canvas = add ("wave", "a thin wavy stroke, 130 - 350 Hz, 15 - 85 % of the time");

        for (int x = Canvas::width * 15 / 100; x < Canvas::width * 85 / 100; ++x)
        {
            const auto swing = 0.5 + 0.5 * std::sin (twoPi * 3.0 * x / Canvas::width);
            const auto y = static_cast<int> (std::lround (canvasYForHz (130.0 * std::pow (350.0 / 130.0, swing))));

            for (int d = -2; d <= 2; ++d)
                if (y + d >= 0 && y + d < Canvas::height)
                    canvas.at (x, y + d) = std::max (canvas.at (x, y + d), std::abs (d) == 2 ? 0.5f : 1.0f);
        }
    }

    {
        // Round on the canvas: as many columns across as rows tall.
        auto& canvas = add ("dot", "a dot, 40 - 65 Hz, in the middle of the pass");
        const auto top = canvasYForHz (65.0), bottom = canvasYForHz (40.0);
        const auto radius = 0.5 * (bottom - top), cy = 0.5 * (top + bottom), cx = 0.5 * Canvas::width;

        for (int y = 0; y < Canvas::height; ++y)
            for (int x = 0; x < Canvas::width; ++x)
                if (std::hypot (x + 0.5 - cx, y + 0.5 - cy) <= radius)
                    canvas.at (x, y) = 1.0f;
    }

    {
        auto& canvas = add ("frame", "a frame, 20 - 80 % of the time: bars at 80 - 90 Hz and 2 - 2.25 kHz, sides 17 columns wide");
        dripFill (canvas, 80.0, 90.0, 0.2, 0.8);
        dripFill (canvas, 2000.0, 2250.0, 0.2, 0.8);
        dripFill (canvas, 80.0, 2250.0, 0.2, 0.2 + 17.0 / Canvas::width);
        dripFill (canvas, 80.0, 2250.0, 0.8 - 17.0 / Canvas::width, 0.8);
    }

    dripText (add ("mid", "the word MID in the Mid band (300 Hz - 3 kHz), nothing below"), "MID", Band::mid);

    {
        juce::String description;
        auto& canvas = add ("photo", "a portrait over 40 Hz - 2 kHz, 30 - 70 % of the time (Auto mode)");
        drawPhoto (canvas, options.image, description);
    }

    return drawings;
}

/** What is drawn in each canvas column: the lowest and highest frequency (the outer edges of the
    lowest and highest row with anything on it, as the plan sees "anything": above 0.001). */
struct DripColumns
{
    std::vector<double> lowHz, highHz;   // 0 where nothing is drawn

    explicit DripColumns (const Canvas& canvas) : lowHz (idx (Canvas::width), 0.0), highHz (idx (Canvas::width), 0.0)
    {
        for (int x = 0; x < Canvas::width; ++x)
        {
            auto top = -1, bottom = -1;

            for (int y = 0; y < Canvas::height; ++y)
            {
                if (canvas.get (x, y) > 0.001f)
                {
                    top = top < 0 ? y : top;
                    bottom = y;
                }
            }

            if (top >= 0)
            {
                highHz[idx (x)] = hzAtNorm (1.0 - static_cast<double> (top) / Canvas::height);
                lowHz[idx (x)] = hzAtNorm (1.0 - (bottom + 1.0) / Canvas::height);
            }
        }
    }

    /** The lowest and highest drawn frequency within `reach` seconds either side of `seconds`
        into the (looped) pass; false if nothing is drawn there. */
    bool around (double seconds, double reach, double& low, double& high) const
    {
        const auto perColumn = passSeconds / Canvas::width;
        const auto first = static_cast<int> (std::floor ((seconds - reach) / perColumn));
        const auto last = static_cast<int> (std::floor ((seconds + reach) / perColumn));
        auto found = false;

        for (int c = first; c <= last; ++c)
        {
            const auto column = idx (((c % Canvas::width) + Canvas::width) % Canvas::width);

            if (lowHz[column] > 0.0)
            {
                low = found ? std::min (low, lowHz[column]) : lowHz[column];
                high = found ? std::max (high, highHz[column]) : highHz[column];
                found = true;
            }
        }

        return found;
    }
};

constexpr double dripPadSeconds = 1.0;   // looped sound either side: more than half the 65536 window

/** The drawing's real output, looped: dripPadSeconds, the pass, dripPadSeconds; at the plan's level. */
std::vector<float> dripRender (const ArtPlan& plan)
{
    const auto pad = static_cast<int> (std::lround (dripPadSeconds * sampleRate));
    const auto pass = passSamples();
    const auto total = pass + 2 * pad;
    std::vector<float> audio (idx (total), 0.0f);

    ArtEngine engine;
    engine.prepare (sampleRate, blockSize);
    engine.setPlan (&plan);

    for (int done = 0; done < total; done += blockSize)
    {
        PlaySegment segment;
        segment.length = std::min (blockSize, total - done);
        segment.playing = true;
        const auto position = static_cast<double> (done - pad) / pass;
        segment.fraction = position - std::floor (position);
        segment.fractionPerSample = 1.0 / pass;
        segment.jump = done == 0;
        engine.render (audio.data() + done, segment.length, &segment, 1);
    }

    return audio;
}

/** Power per bin (a sine of amplitude 1 on a bin reads 1) of Blackman-Harris frames over the
    pass, a frame every eighth of the window. */
struct DripSpectrogram
{
    int fftSize = 0, hop = 0, numFrames = 0, numBins = 0;
    std::vector<float> power;   // [frame * numBins + bin]

    double frameSeconds (int frame) const { return (frame + 0.5) * hop / sampleRate; }
    const float* frame (int f) const { return power.data() + idx (f) * idx (numBins); }

    DripSpectrogram (const std::vector<float>& audio, int size) : fftSize (size), hop (size / 8), numBins (size / 2 + 1)
    {
        std::vector<float> window (idx (size));
        auto windowSum = 0.0;

        for (int n = 0; n < size; ++n)
        {
            const auto phase = twoPi * n / size;
            const auto w = 0.35875 - 0.48829 * std::cos (phase) + 0.14128 * std::cos (2.0 * phase) - 0.01168 * std::cos (3.0 * phase);
            window[idx (n)] = static_cast<float> (w);
            windowSum += w;
        }

        const auto toAmplitude = static_cast<float> (2.0 / windowSum);
        const auto pad = static_cast<int> (std::lround (dripPadSeconds * sampleRate));
        numFrames = passSamples() / hop;
        power.resize (idx (numFrames) * idx (numBins));

        juce::dsp::FFT fft (juce::roundToInt (std::log2 (size)));
        std::vector<float> work (idx (size) * 2);

        for (int f = 0; f < numFrames; ++f)
        {
            const auto* source = audio.data() + (pad + f * hop + hop / 2 - size / 2);

            for (size_t n = 0; n < idx (size); ++n)
                work[n] = source[n] * window[n];

            std::fill (work.begin() + size, work.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (work.data(), true);

            auto* out = power.data() + idx (f) * idx (numBins);

            for (size_t bin = 0; bin < idx (numBins); ++bin)
                out[bin] = (work[bin] * toAmplitude) * (work[bin] * toAmplitude);
        }
    }
};

struct DripResult
{
    double belowDb = notMeasured, belowPeakDb = notMeasured;   // energy / loudest cell below the drawing, against the drawing
    double aboveDb = notMeasured, abovePeakDb = notMeasured;   // ... above it
    double outsideDb = notMeasured, outsidePeakDb = notMeasured; // ... before it starts and after it ends
    double peakDbfs = notMeasured;                             // the loudest sample, at the plan's level
    int lines = 0;                                             // sines that play
};

double dripDb (double ratio)
{
    return ratio > 0.0 ? std::max (-160.0, 10.0 * std::log10 (ratio)) : -160.0;
}

DripResult measureDrip (const Canvas& canvas, const std::vector<float>& audio)
{
    DripResult result;
    const DripColumns columns (canvas);
    constexpr auto mainLobeBins = 4.5;
    const auto sixth = std::pow (2.0, 1.0 / 6.0);

    {
        const DripSpectrogram fine (audio, 65536);
        const auto binHz = sampleRate / fine.fftSize;
        auto total = 0.0, below = 0.0, above = 0.0;
        auto loudest = 0.0f, loudestBelow = 0.0f, loudestAbove = 0.0f;

        for (int f = 0; f < fine.numFrames; ++f)
        {
            double low = 0.0, high = 0.0;
            const auto* bins = fine.frame (f);

            for (int bin = 0; bin < fine.numBins; ++bin)
                loudest = std::max (loudest, bins[bin]);

            if (! columns.around (fine.frameSeconds (f), 0.5 * fine.fftSize / sampleRate, low, high))
                continue;

            const auto lastBelow = static_cast<int> (std::floor (std::min (low / sixth, low - mainLobeBins * binHz) / binHz));
            const auto firstAbove = static_cast<int> (std::ceil (std::max (high * sixth, high + mainLobeBins * binHz) / binHz));

            for (int bin = 0; bin < fine.numBins; ++bin)
            {
                const auto p = bins[bin];
                total += static_cast<double> (p);

                if (bin <= lastBelow)
                {
                    below += static_cast<double> (p);
                    loudestBelow = std::max (loudestBelow, p);
                }
                else if (bin >= firstAbove)
                {
                    above += static_cast<double> (p);
                    loudestAbove = std::max (loudestAbove, p);
                }
            }
        }

        if (total > 0.0 && loudest > 0.0f)
        {
            result.belowDb = dripDb (below / total);
            result.aboveDb = dripDb (above / total);
            result.belowPeakDb = dripDb (static_cast<double> (loudestBelow) / static_cast<double> (loudest));
            result.abovePeakDb = dripDb (static_cast<double> (loudestAbove) / static_cast<double> (loudest));
        }
    }

    {
        const DripSpectrogram quick (audio, 4096);
        const auto window = quick.fftSize / sampleRate;
        auto on = 0.0, off = 0.0;
        auto onFrames = 0, offFrames = 0;
        auto loudest = 0.0f, loudestOff = 0.0f;

        for (int f = 0; f < quick.numFrames; ++f)
        {
            double low = 0.0, high = 0.0;
            const auto* bins = quick.frame (f);
            const auto seconds = quick.frameSeconds (f);
            auto power = 0.0;
            auto loudestHere = 0.0f;

            for (int bin = 0; bin < quick.numBins; ++bin)
            {
                power += static_cast<double> (bins[bin]);
                loudestHere = std::max (loudestHere, bins[bin]);
            }

            loudest = std::max (loudest, loudestHere);

            if (columns.around (seconds, 0.0, low, high))
            {
                on += power;
                ++onFrames;
            }
            else if (! columns.around (seconds, 0.5 * window + 0.15, low, high))
            {
                off += power;
                ++offFrames;
                loudestOff = std::max (loudestOff, loudestHere);
            }
        }

        if (onFrames > 0 && offFrames > 0 && on > 0.0)
        {
            result.outsideDb = dripDb ((off / offFrames) / (on / onFrames));
            result.outsidePeakDb = dripDb (static_cast<double> (loudestOff) / static_cast<double> (loudest));
        }
    }

    return result;
}

/** What an analyser shows of the sound at Level -6 dB: `window` (Hann or Blackman-Harris),
    displayRows rows (log, 20 Hz - 20 kHz), the colours over floorDbfs .. 0 dBFS. `audio` as
    dripRender made it. */
juce::Image dripPicture (const std::vector<float>& audio, int fftSize, WindowShape window, float floorDbfs)
{
    // analyse() expects padSeconds of sound before the pass.
    const auto skip = static_cast<std::ptrdiff_t> (std::lround ((dripPadSeconds - padSeconds) * sampleRate));
    const std::vector<float> trimmed (audio.begin() + skip, audio.end() - skip);
    const auto display = toDisplay (analyse (trimmed, fftSize, window), Mapping::interpolate);
    const auto levelDb = juce::Decibels::gainToDecibels (defaultLevel);

    juce::Image image (juce::Image::RGB, pngWidth, displayRows, false);
    juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < displayRows; ++y)
    {
        for (int x = 0; x < pngWidth; ++x)
        {
            const auto frame = std::min (display.numFrames - 1, static_cast<int> ((x + 0.5) / pngWidth * display.numFrames));
            data.setPixelColour (x, y, colourAt ((display.at (y, frame) + levelDb - floorDbfs) / -floorDbfs));
        }
    }

    return image;
}

juce::Image dripCanvasPicture (const Canvas& canvas)
{
    juce::Image image (juce::Image::RGB, pngWidth, displayRows, false);
    juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < displayRows; ++y)
    {
        for (int x = 0; x < pngWidth; ++x)
        {
            // The canvas is on the display's scale: the loudest canvas row inside the display row.
            const auto column = std::min (Canvas::width - 1, x * Canvas::width / pngWidth);
            auto value = 0.0f;

            for (int row = y * Canvas::height / displayRows; row < std::max (y * Canvas::height / displayRows + 1, (y + 1) * Canvas::height / displayRows); ++row)
                value = std::max (value, canvas.get (column, std::min (row, Canvas::height - 1)));

            data.setPixelColour (x, y, colourAt (value > 0.001f ? 0.4f + 0.6f * value : 0.0f));
        }
    }

    return image;
}

int runDripLab (const Options& options)
{
    const auto drawings = dripDrawings (options);
    juce::String table;
    table << "DRIP (" << lowsName (options.lows) << "; dB against the drawing; 65536-point Blackman-Harris for below and above, 4096 for outside in time)\n"
          << "drawing     below  (peak)    above  (peak)    outside in time  (peak)    lines  peak dBFS\n";
    auto written = true;

    for (const auto& drawing : drawings)
    {
        const auto plan = buildPlan (drawing.canvas, planSettingsFor (256, options.contrastDb, options.lows));
        const auto audio = dripRender (*plan);
        auto result = measureDrip (drawing.canvas, audio);

        for (int l = 0; l < plan->numLines; ++l)
            for (int c = 0; c < plan->numColumns; ++c)
                if (plan->amplitude (c, l) > 0.0f)
                {
                    ++result.lines;
                    break;
                }

        auto peak = 0.0f;

        for (const auto sample : audio)
            peak = std::max (peak, std::abs (sample));

        result.peakDbfs = juce::Decibels::gainToDecibels (static_cast<double> (peak), -200.0);

        table << drawing.name.paddedRight (' ', 10) << cell (result.belowDb, 7, 1) << cell (result.belowPeakDb, 8, 1)
              << cell (result.aboveDb, 9, 1) << cell (result.abovePeakDb, 8, 1)
              << cell (result.outsideDb, 15, 1) << cell (result.outsidePeakDb, 10, 1)
              << juce::String (result.lines).paddedLeft (' ', 9) << cell (result.peakDbfs, 11, 1) << "\n";

        const auto file = [&options, &drawing] (const char* rest) { return options.outputDir.getChildFile (drawing.name + rest); };
        written = writePng (dripCanvasPicture (drawing.canvas), file ("-canvas.png")) && written;
        written = writePng (dripPicture (audio, 4096, WindowShape::hann, -100.0f), file ("-hann4096.png")) && written;
        written = writePng (dripPicture (audio, 8192, WindowShape::hann, -100.0f), file ("-hann8192.png")) && written;
        written = writePng (dripPicture (audio, 8192, WindowShape::hann, -80.0f), file ("-hann8192-floor80.png")) && written;
        written = writePng (dripPicture (audio, 8192, WindowShape::blackmanHarris, -100.0f), file ("-bh8192.png")) && written;
        written = writePng (dripPicture (audio, 8192, WindowShape::blackmanHarris, -80.0f), file ("-bh8192-floor80.png")) && written;
        written = writePng (dripPicture (audio, 4096, WindowShape::hann, -80.0f), file ("-hann4096-floor80.png")) && written;
        written = writePng (dripPicture (audio, 4096, WindowShape::blackmanHarris, -100.0f), file ("-bh4096.png")) && written;
    }

    table << "\nbelow / above     the share of the sound's energy more than a sixth of an octave below the lowest (above the\n"
             "                  highest) frequency drawn under each analysis window; peak: the loudest cell there against\n"
             "                  the loudest cell of the drawing\n"
             "outside in time   frames with nothing drawn within 0.15 s of their window against frames with something drawn\n";

    std::cout << table << std::flush;
    written = options.outputDir.getChildFile ("drip.txt").replaceWithText (table) && written;
    return written ? 0 : 1;
}
