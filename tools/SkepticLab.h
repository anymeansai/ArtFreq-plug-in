// The second opinion on the lows, a part of ArtFreqLab (included by LowsLab.cpp, whose helpers it
// uses):
//
//   ArtFreqLab <output-dir> --skeptic [--lows stripes|smooth]
//
// The drip check (DripLab.h) measures eight drawings made to show the drip. This one plays the
// kind of drawings people make (the default text moved into the Low band, small and large low
// text, a picture of soft gradients, brush strokes, a full-height picture, text in the highs),
// at Density 64, 256 and 1024, over 1, 4 and 16 bars, looped and as a single pass, on the log
// and the linear scale, and writes the real output of each for an analysis from outside (any
// tool that reads raw floats; nothing in here decides what is "clean"):
//
//   <case>.f32          the sound, mono, 32-bit floats at 48 kHz: `pad` seconds of looped sound
//                       (silence for a single pass), the pass, `pad` seconds more
//   <case>.json         how it was made (density, seconds, pad, repeat, scale, gain), what it
//                       cost (lines, plan ms, render ms per second of sound) and, per canvas
//                       column, the lowest and the highest frequency drawn (0: nothing)
//   <case>-canvas.png   the drawing, one pixel per canvas cell
//   palette.txt         the spectrogram colours (256 lines of "r g b")
//   strokes.txt         thin horizontal strokes, every height from 40 Hz to 1.7 kHz and 1 to 24 rows
//                       thick, each built into a plan of its own: how many lines play it, how
//                       often two lines alone do (they beat), how far below the stroke the lowest
//                       line lies, and whether any stroke is lost (also printed)

struct SkepticCase
{
    juce::String name, what;
    Canvas canvas;
    int density = 256;
    double seconds = 8.0;
    bool repeat = true;
    FrequencyScale scale;
};

double skepticY (const FrequencyScale& scale, double hz)
{
    return (1.0 - scale.toNorm (hz)) * Canvas::height;
}

void skepticFill (Canvas& canvas, const FrequencyScale& scale, double lowHz, double highHz, double from, double to)
{
    const auto top = juce::jlimit (0, Canvas::height - 1, static_cast<int> (std::ceil (skepticY (scale, highHz))));
    const auto bottom = juce::jlimit (0, Canvas::height - 1, static_cast<int> (std::floor (skepticY (scale, lowHz))) - 1);
    const auto left = juce::jlimit (0, Canvas::width - 1, static_cast<int> (std::lround (from * Canvas::width)));
    const auto right = juce::jlimit (0, Canvas::width - 1, static_cast<int> (std::lround (to * Canvas::width)) - 1);

    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x)
            canvas.at (x, y) = 1.0f;
}

void skepticText (Canvas& canvas, const FrequencyScale& scale, const char* text, double lowHz, double highHz, double from, double to)
{
    ArtDocument document;
    auto object = ArtDocument::makeTextObject (text, static_cast<float> (lowHz), static_cast<float> (highHz));
    object.startTime = static_cast<float> (from);
    object.endTime = static_cast<float> (to);
    document.objects.push_back (object);

    Compositor compositor;
    compositor.render (document, scale, canvas);
}

/** Two soft blobs over 40 Hz - 2 kHz, 20 - 80 % of the time: every level from full to nothing,
    no hard edge anywhere. */
void skepticGradient (Canvas& canvas, const FrequencyScale& scale)
{
    for (int y = 0; y < Canvas::height; ++y)
    {
        const auto hz = scale.toHz (1.0 - (y + 0.5) / Canvas::height);
        const auto u = std::log (hz / 40.0) / std::log (2000.0 / 40.0);

        for (int x = 0; x < Canvas::width; ++x)
        {
            const auto t = (x + 0.5) / Canvas::width;
            const auto first = std::exp (-std::pow ((t - 0.4) / 0.12, 2.0) - std::pow ((u - 0.35) / 0.25, 2.0));
            const auto second = 0.8 * std::exp (-std::pow ((t - 0.62) / 0.08, 2.0) - std::pow ((u - 0.75) / 0.15, 2.0));
            const auto value = std::max (first, second);
            canvas.at (x, y) = u >= 0.0 && u <= 1.0 && value >= 0.03 ? static_cast<float> (value) : 0.0f;
        }
    }
}

/** A stroke `rows` thick rising from 50 to 800 Hz over 10 - 90 % of the time. */
void skepticDiagonal (Canvas& canvas, const FrequencyScale& scale, int rows)
{
    const auto x0 = Canvas::width / 10, x1 = Canvas::width * 9 / 10;

    for (int x = x0; x < x1; ++x)
    {
        const auto hz = 50.0 * std::pow (800.0 / 50.0, static_cast<double> (x - x0) / (x1 - x0));
        const auto y = static_cast<int> (std::lround (skepticY (scale, hz)));

        for (int row = y - rows / 2; row < y - rows / 2 + rows; ++row)
            if (row >= 0 && row < Canvas::height)
                canvas.at (x, row) = 1.0f;
    }
}

/** Strokes four rows thick: at 62.5 Hz (between two of the comb's lines), at 100 Hz (on one) and
    at 137.5 Hz (between two). */
void skepticHorizontals (Canvas& canvas, const FrequencyScale& scale)
{
    const auto stroke = [&] (double hz, double from, double to)
    {
        const auto y = static_cast<int> (std::lround (skepticY (scale, hz)));

        for (int row = y - 2; row < y + 2; ++row)
            for (int x = static_cast<int> (from * Canvas::width); x < static_cast<int> (to * Canvas::width); ++x)
                canvas.at (x, row) = 1.0f;
    };

    stroke (62.5, 0.05, 0.30);
    stroke (100.0, 0.375, 0.625);
    stroke (137.5, 0.70, 0.95);
}

/** A filled blob with a soft rim, 80 - 300 Hz, 35 - 65 % of the time. */
void skepticBlob (Canvas& canvas, const FrequencyScale& scale)
{
    const auto top = skepticY (scale, 300.0), bottom = skepticY (scale, 80.0);
    const auto cy = 0.5 * (top + bottom), ry = 0.5 * (bottom - top);
    const auto cx = 0.5 * Canvas::width, rx = 0.15 * Canvas::width;

    for (int y = 0; y < Canvas::height; ++y)
        for (int x = 0; x < Canvas::width; ++x)
        {
            const auto r = std::hypot ((x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry);
            canvas.at (x, y) = static_cast<float> (juce::jlimit (0.0, 1.0, (1.0 - r) * 8.0));
        }
}

void skepticPicture (Canvas& canvas, const FrequencyScale& scale, double lowHz, double highHz)
{
    ArtDocument document;
    auto object = ArtDocument::makeImageObject (prepareImportedImage (makePortrait()), static_cast<float> (lowHz), static_cast<float> (highHz));
    object.startTime = 0.2f;
    object.endTime = 0.8f;
    document.objects.push_back (object);

    Compositor compositor;
    compositor.render (document, scale, canvas);
}

std::vector<SkepticCase> skepticCases()
{
    std::vector<SkepticCase> cases;
    FrequencyScale linear;
    linear.type = ScaleType::linear;

    const auto add = [&cases] (const juce::String& name, const char* what, int density = 256, double seconds = 8.0, bool repeat = true,
                               FrequencyScale scale = {}) -> SkepticCase&
    {
        cases.push_back ({ name, what, {}, density, seconds, repeat, scale });
        cases.back().canvas.clear();
        return cases.back();
    };

    const auto artfreq = [] (SkepticCase& c) { skepticText (c.canvas, c.scale, "ArtFreq", 40.0, 250.0, 0.04, 0.96); };
    const auto smallText = [] (SkepticCase& c) { skepticText (c.canvas, c.scale, "small text", 60.0, 200.0, 0.3, 0.7); };
    const auto gradient = [] (SkepticCase& c) { skepticGradient (c.canvas, c.scale); };
    const auto diagonal = [] (SkepticCase& c) { skepticDiagonal (c.canvas, c.scale, 3); };
    const auto picture = [] (SkepticCase& c) { skepticPicture (c.canvas, c.scale, 25.0, 18000.0); };
    const auto edges = [] (SkepticCase& c)
    {
        skepticFill (c.canvas, c.scale, 60.0, 250.0, 0.0, 0.25);
        skepticFill (c.canvas, c.scale, 60.0, 250.0, 0.75, 1.0);
    };

    artfreq (add ("artfreq-low", "the default text, ArtFreq, in the Low band (40 - 250 Hz)"));
    smallText (add ("text-small", "small text, 60 - 200 Hz, 30 - 70 % of the time"));
    {
        auto& c = add ("text-large", "large text, BIG, 60 - 200 Hz, 10 - 90 % of the time");
        skepticText (c.canvas, c.scale, "BIG", 60.0, 200.0, 0.1, 0.9);
    }
    gradient (add ("gradient", "two soft blobs, 40 Hz - 2 kHz: every level, no hard edge"));
    diagonal (add ("diagonal", "a stroke 3 rows thick rising from 50 to 800 Hz"));
    {
        auto& c = add ("horizontals", "strokes 4 rows thick at 62.5, 100 and 137.5 Hz");
        skepticHorizontals (c.canvas, c.scale);
    }
    {
        auto& c = add ("blob", "a filled blob with a soft rim, 80 - 300 Hz");
        skepticBlob (c.canvas, c.scale);
    }
    picture (add ("picture", "a portrait over the whole height, 25 Hz - 18 kHz"));
    {
        auto& c = add ("high-text", "the word HIGH in the High band (3.5 - 16 kHz): must not change");
        skepticText (c.canvas, c.scale, "HIGH", 3500.0, 16000.0, 0.2, 0.8);
    }
    {
        // Everything drawn, at full level: steadiness, and the joins between the comb's two grids
        // and between the comb and the lines above it.
        auto& c = add ("drone", "a block over 30 Hz - 6 kHz, the whole pass");
        skepticFill (c.canvas, c.scale, 30.0, 6000.0, 0.0, 1.0);
    }
    edges (add ("edges-loop", "blocks 60 - 250 Hz touching both ends of the pass, looped"));
    edges (add ("edges-once", "blocks 60 - 250 Hz touching both ends of the pass, a single pass", 256, 8.0, false));

    artfreq (add ("artfreq-low-d64", "ArtFreq in the Low band, Density 64", 64));
    artfreq (add ("artfreq-low-d1024", "ArtFreq in the Low band, Density 1024", 1024));
    gradient (add ("gradient-d64", "the soft blobs, Density 64", 64));
    gradient (add ("gradient-d1024", "the soft blobs, Density 1024", 1024));
    picture (add ("picture-d64", "the full-height portrait, Density 64", 64));
    picture (add ("picture-d1024", "the full-height portrait, Density 1024", 1024));

    artfreq (add ("artfreq-low-1bar", "ArtFreq in the Low band, 1 bar (2 s)", 256, 2.0));
    artfreq (add ("artfreq-low-16bars", "ArtFreq in the Low band, 16 bars (32 s)", 256, 32.0));
    smallText (add ("text-small-1bar", "small low text, 1 bar (2 s)", 256, 2.0));
    smallText (add ("text-small-16bars", "small low text, 16 bars (32 s)", 256, 32.0));
    diagonal (add ("diagonal-1bar", "the rising stroke, 1 bar (2 s)", 256, 2.0));
    diagonal (add ("diagonal-16bars", "the rising stroke, 16 bars (32 s)", 256, 32.0));
    artfreq (add ("artfreq-low-once", "ArtFreq in the Low band, a single pass", 256, 8.0, false));

    {
        auto& c = add ("linear-text", "linear scale: ArtFreq over 200 Hz - 3 kHz", 256, 8.0, true, linear);
        skepticText (c.canvas, c.scale, "ArtFreq", 200.0, 3000.0, 0.04, 0.96);
    }
    {
        auto& c = add ("linear-text-d1024", "linear scale, Density 1024: ArtFreq over 200 Hz - 3 kHz", 1024, 8.0, true, linear);
        skepticText (c.canvas, c.scale, "ArtFreq", 200.0, 3000.0, 0.04, 0.96);
    }
    picture (add ("linear-picture-d1024", "linear scale, Density 1024: the full-height portrait", 1024, 8.0, true, linear));

    return cases;
}

constexpr double skepticPadSeconds = 1.0;

PlanSettings skepticSettings (const SkepticCase& c, LowEnd lows, float contrastDb)
{
    PlanSettings settings;
    settings.scale = c.scale;
    settings.density = c.density;
    settings.contrastDb = contrastDb;
    settings.maxFrequencyHz = 22000.0f;
    settings.lowEnd = lows;
   #ifndef ARTFREQ_SKEPTIC_BEFORE   // (0.6 had no fades in time to work out)
    settings.passSeconds = c.seconds;
    settings.repeats = c.repeat;
   #endif
    return settings;
}

/** The pass as it is heard: looped as a DAW plays it (the plan's level), or once, as a bounce and
    a one-shot play it (PassRenderer; at `gain`, low enough to stay under the safety clipper). */
std::vector<float> skepticRender (const ArtPlan& plan, const SkepticCase& c, float& gain, double& msPerSecond)
{
    const auto pad = static_cast<int> (std::lround (skepticPadSeconds * sampleRate));
    const auto pass = static_cast<int> (std::lround (c.seconds * sampleRate));
    const auto total = pass + 2 * pad;
    std::vector<float> audio (idx (total), 0.0f);
    msPerSecond = 0.0;   // (a single pass is not timed)

    if (! c.repeat)
    {
        gain = 0.25f;
        PassRenderer renderer (plan, sampleRate, c.seconds, gain);

        for (int done = 0; done < pass;)
        {
            const auto made = renderer.render (audio.data() + pad + done, pass - done);

            if (made <= 0)
                break;

            done += made;
        }

        return audio;
    }

    gain = 1.0f;
    msPerSecond = 1.0e9;

    // (the quickest of three: the cost without whatever else the machine was doing)
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        ArtEngine engine;
        engine.prepare (sampleRate, blockSize);
        engine.setPlan (&plan);
        const auto start = juce::Time::getMillisecondCounterHiRes();

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

        msPerSecond = std::min (msPerSecond, (juce::Time::getMillisecondCounterHiRes() - start) / (total / sampleRate));
    }

    return audio;
}

bool skepticWriteCanvas (const Canvas& canvas, const juce::File& file)
{
    juce::Image image (juce::Image::RGB, Canvas::width, Canvas::height, false);
    juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < Canvas::height; ++y)
        for (int x = 0; x < Canvas::width; ++x)
            data.setPixelColour (x, y, juce::Colour::greyLevel (canvas.get (x, y)));

    return writePng (image, file);
}

#ifndef ARTFREQ_SKEPTIC_BEFORE   // (0.6's Smooth lows were not lines of the plan)
/** Thin horizontal strokes, each alone in a plan (no fades): see strokes.txt above. */
juce::String skepticStrokes (LowEnd lows, float contrastDb)
{
    const FrequencyScale scale;
    juce::String table;
    table << "THIN HORIZONTAL STROKES (" << lowsName (lows) << ", Density 256): every height from 40 Hz to 1.7 kHz\n"
          << "rows   strokes   lost   1 line   2 lines   3 lines   more   2 lines beating > 6 dB   lowest line below the stroke, worst (Hz, at Hz)\n";

    Canvas canvas;
    SkepticCase settingsOf;
    auto settings = skepticSettings (settingsOf, lows, contrastDb);
    settings.passSeconds = 0.0;

    const auto firstTop = static_cast<int> (std::ceil (skepticY (scale, 1700.0)));
    const auto lastBottom = static_cast<int> (std::floor (skepticY (scale, 40.0))) - 1;

    for (const auto rows : { 1, 2, 3, 4, 6, 8, 12, 16, 24 })
    {
        auto strokes = 0, lost = 0, one = 0, two = 0, three = 0, more = 0, beating = 0;
        auto worstBelow = 0.0, worstAt = 0.0;

        for (int top = firstTop; top + rows - 1 <= lastBottom; ++top)
        {
            canvas.clear();

            for (int y = top; y < top + rows; ++y)
                for (int x = 384; x < 640; ++x)
                    canvas.at (x, y) = 1.0f;

            const auto plan = buildPlan (canvas, settings);
            const auto strokeLowHz = scale.toHz (1.0 - static_cast<double> (top + rows) / Canvas::height);
            std::vector<std::pair<double, double>> playing;   // Hz, amplitude

            for (int l = 0; l < plan->numLines; ++l)
                if (plan->amplitude (512, l) > 0.0f)
                    playing.emplace_back (static_cast<double> (plan->frequencies[idx (l)]), static_cast<double> (plan->amplitude (512, l)));

            ++strokes;
            lost += playing.empty() ? 1 : 0;
            one += playing.size() == 1 ? 1 : 0;
            two += playing.size() == 2 ? 1 : 0;
            three += playing.size() == 3 ? 1 : 0;
            more += playing.size() > 3 ? 1 : 0;

            if (playing.size() == 2)
            {
                const auto a = playing[0].second, b = playing[1].second;
                beating += std::abs (a - b) < (a + b) / 2.0 ? 1 : 0;   // (a + b) / |a - b| > 2
            }

            if (! playing.empty() && strokeLowHz - playing.front().first > worstBelow)
            {
                worstBelow = strokeLowHz - playing.front().first;
                worstAt = strokeLowHz;
            }
        }

        table << juce::String (rows).paddedLeft (' ', 4) << juce::String (strokes).paddedLeft (' ', 10) << juce::String (lost).paddedLeft (' ', 7)
              << juce::String (one).paddedLeft (' ', 9) << juce::String (two).paddedLeft (' ', 10) << juce::String (three).paddedLeft (' ', 10)
              << juce::String (more).paddedLeft (' ', 7) << juce::String (beating).paddedLeft (' ', 25)
              << "      " << juce::String (worstBelow, 1) << " at " << juce::String (worstAt, 1) << "\n";
    }

    return table;
}
#endif

int runSkepticLab (const Options& options)
{
    auto written = true;

    {
        juce::String palette;

        for (int i = 0; i < 256; ++i)
        {
            const auto colour = colourAt (static_cast<float> (i) / 255.0f);
            palette << static_cast<int> (colour.getRed()) << " " << static_cast<int> (colour.getGreen()) << " " << static_cast<int> (colour.getBlue()) << "\n";
        }

        written = options.outputDir.getChildFile ("palette.txt").replaceWithText (palette) && written;
    }

    for (const auto& c : skepticCases())
    {
        const auto settings = skepticSettings (c, options.lows, options.contrastDb);
        auto planMs = 1.0e9;
        std::unique_ptr<ArtPlan> plan;

        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const auto start = juce::Time::getMillisecondCounterHiRes();
            plan = buildPlan (c.canvas, settings);
            planMs = std::min (planMs, juce::Time::getMillisecondCounterHiRes() - start);
        }

        auto gain = 1.0f;
        auto msPerSecond = 0.0;
        const auto audio = skepticRender (*plan, c, gain, msPerSecond);

        auto sounding = 0;

        for (int l = 0; l < plan->numLines; ++l)
            for (int column = 0; column < plan->numColumns; ++column)
                if (plan->amplitude (column, l) > 0.0f)
                {
                    ++sounding;
                    break;
                }

        juce::String low, high;

        for (int x = 0; x < Canvas::width; ++x)
        {
            auto top = -1, bottom = -1;

            for (int y = 0; y < Canvas::height; ++y)
                if (c.canvas.get (x, y) > 0.001f)
                {
                    top = top < 0 ? y : top;
                    bottom = y;
                }

            low << (x > 0 ? "," : "") << (top < 0 ? juce::String ("0") : juce::String (c.scale.toHz (1.0 - (bottom + 1.0) / Canvas::height), 3));
            high << (x > 0 ? "," : "") << (top < 0 ? juce::String ("0") : juce::String (c.scale.toHz (1.0 - static_cast<double> (top) / Canvas::height), 3));
        }

        juce::String json;
        json << "{\n  \"name\": \"" << c.name << "\",\n  \"what\": \"" << c.what << "\",\n  \"lows\": \"" << lowsName (options.lows)
             << "\",\n  \"density\": " << c.density << ",\n  \"seconds\": " << c.seconds << ",\n  \"pad\": " << skepticPadSeconds
             << ",\n  \"repeat\": " << (c.repeat ? "true" : "false") << ",\n  \"scale\": \"" << (c.scale.type == ScaleType::linear ? "linear" : "log")
             << "\",\n  \"gain\": " << gain << ",\n  \"lines\": " << sounding << ",\n  \"planLines\": " << plan->numLines
             << ",\n  \"combLines\": " << plan->combLines << ",\n  \"planMs\": " << juce::String (planMs, 3)
             << ",\n  \"renderMsPerSecond\": " << juce::String (c.repeat ? msPerSecond : 0.0, 3)
             << ",\n  \"lowHz\": [" << low << "],\n  \"highHz\": [" << high << "]\n}\n";

        written = options.outputDir.getChildFile (c.name + ".json").replaceWithText (json) && written;
        written = options.outputDir.getChildFile (c.name + ".f32").replaceWithData (audio.data(), audio.size() * sizeof (float)) && written;
        written = skepticWriteCanvas (c.canvas, options.outputDir.getChildFile (c.name + "-canvas.png")) && written;

        std::cout << "  " << c.name << ": " << sounding << " lines, plan " << juce::String (planMs, 1) << " ms, render "
                  << juce::String (msPerSecond, 2) << " ms per second" << std::endl;
    }

   #ifndef ARTFREQ_SKEPTIC_BEFORE
    const auto strokes = skepticStrokes (options.lows, options.contrastDb);
    std::cout << strokes << std::flush;
    written = options.outputDir.getChildFile ("strokes.txt").replaceWithText (strokes) && written;
   #endif

    return written ? 0 : 1;
}
