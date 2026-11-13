#include "ArtEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined (__SSE2__) || defined (_M_X64) || defined (_M_AMD64)
 #include <emmintrin.h>
#elif defined (__ARM_NEON) || defined (__ARM_NEON__)
 #include <arm_neon.h>
#endif

// How the engine renders:
//
// Playback is cut into chunks that never cross a column boundary of any plan being heard, a
// plan crossfade's end or the end of a jump fade-out, and are at most chunkSize long. Inside
// a chunk every line's amplitude is a straight ramp (the linear interpolation between two
// columns), and the plan's active list for the column says exactly which lines can be
// non-zero, so only those are rendered: their oscillator state is gathered into groups of
// `lanes` lines (padded with a silent dummy line), each group runs through the whole chunk in
// registers, four lines per SSE / NEON instruction (a phasor rotation and a multiply-add per
// line and sample), and the partial sums are added up per sample at the end.
//
// The phasors are single precision and are pulled back onto the unit circle after every
// chunk, so they neither grow nor die away. Lines that are silent keep their phase where it
// was; they start again from an amplitude of 0, so where their phase is does not matter.
//
// The gate, jump and plan fades are gains on the summed output, per sample.

namespace freqart
{

namespace
{
    constexpr double twoPi = 6.283185307179586476925286766559;
    constexpr double nyquistGuard = 0.48;   // lines at or above this x the sample rate are skipped
    constexpr int noLimit = 1 << 30;
    constexpr double endOfPass = 1.0 - 0x1.0p-53;  // the last position before 1

    double wrap (double x) noexcept
    {
        const auto wrapped = x - std::floor (x);
        return wrapped >= 1.0 ? 0.0 : wrapped;
    }

    /** A fixed pseudo-random starting phase for each line index, so that renders are
        reproducible and the lines do not all peak together. */
    double initialPhase (int line) noexcept
    {
        auto x = static_cast<uint32_t> (line) * 0x9E3779B9u + 0x7F4A7C15u;
        x ^= x >> 16;
        x *= 0x85EBCA6Bu;
        x ^= x >> 13;
        x *= 0xC2B2AE35u;
        x ^= x >> 16;
        return twoPi * static_cast<double> (x) / 4294967296.0;
    }

    /** Gives the envelope state its S shape: a gentler start and end than a straight line. */
    float shapeEnvelope (float e) noexcept
    {
        return e * e * (3.0f - 2.0f * e);
    }

    bool isPlayable (const ArtPlan* plan) noexcept
    {
        if (plan == nullptr || plan->silent || plan->numLines <= 0 || plan->numColumns <= 0)
            return false;

        const auto lines = static_cast<size_t> (plan->numLines);
        const auto columns = static_cast<size_t> (plan->numColumns);

        return plan->frequencies.size() >= lines
            && plan->amplitudes.size() >= lines * columns
            && plan->activeStart.size() >= columns + 1;
    }

    /** Four floats side by side: SSE on Intel, NEON on Apple silicon, plain floats elsewhere.
        Loads and stores need no alignment. */
   #if defined (__SSE2__) || defined (_M_X64) || defined (_M_AMD64)
    struct Float4
    {
        __m128 v;

        static Float4 load (const float* p) noexcept           { return { _mm_loadu_ps (p) }; }
        static Float4 fill (float x) noexcept                  { return { _mm_set1_ps (x) }; }
        void store (float* p) const noexcept                   { _mm_storeu_ps (p, v); }
        Float4 operator+ (Float4 o) const noexcept             { return { _mm_add_ps (v, o.v) }; }
        Float4 operator- (Float4 o) const noexcept             { return { _mm_sub_ps (v, o.v) }; }
        Float4 operator* (Float4 o) const noexcept             { return { _mm_mul_ps (v, o.v) }; }
    };
   #elif defined (__ARM_NEON) || defined (__ARM_NEON__)
    struct Float4
    {
        float32x4_t v;

        static Float4 load (const float* p) noexcept           { return { vld1q_f32 (p) }; }
        static Float4 fill (float x) noexcept                  { return { vdupq_n_f32 (x) }; }
        void store (float* p) const noexcept                   { vst1q_f32 (p, v); }
        Float4 operator+ (Float4 o) const noexcept             { return { vaddq_f32 (v, o.v) }; }
        Float4 operator- (Float4 o) const noexcept             { return { vsubq_f32 (v, o.v) }; }
        Float4 operator* (Float4 o) const noexcept             { return { vmulq_f32 (v, o.v) }; }
    };
   #else
    struct Float4
    {
        float v[4];

        static Float4 load (const float* p) noexcept           { return { { p[0], p[1], p[2], p[3] } }; }
        static Float4 fill (float x) noexcept                  { return { { x, x, x, x } }; }
        void store (float* p) const noexcept                   { for (int k = 0; k < 4; ++k) p[k] = v[k]; }
        Float4 operator+ (Float4 o) const noexcept             { return { { v[0] + o.v[0], v[1] + o.v[1], v[2] + o.v[2], v[3] + o.v[3] } }; }
        Float4 operator- (Float4 o) const noexcept             { return { { v[0] - o.v[0], v[1] - o.v[1], v[2] - o.v[2], v[3] - o.v[3] } }; }
        Float4 operator* (Float4 o) const noexcept             { return { { v[0] * o.v[0], v[1] * o.v[1], v[2] * o.v[2], v[3] * o.v[3] } }; }
    };
   #endif

    /** A group of lines' oscillators (4 x registers phasors), held in registers for a whole
        chunk. Loaded from the bank by line index and put back afterwards, pulled back onto
        the unit circle (one Newton step towards 1 / |z|, plenty this close to 1). */
    template <size_t registers>
    struct Oscillators
    {
        static constexpr size_t size = 4 * registers;
        Float4 r[registers], m[registers], c[registers], s[registers];

        void load (const uint16_t* lines, const float* re, const float* im, const float* cosStep, const float* sinStep) noexcept
        {
            float tr[size], tm[size], tc[size], ts[size];

            for (size_t k = 0; k < size; ++k)
            {
                tr[k] = re[lines[k]];
                tm[k] = im[lines[k]];
                tc[k] = cosStep[lines[k]];
                ts[k] = sinStep[lines[k]];
            }

            for (size_t j = 0; j < registers; ++j)
            {
                r[j] = Float4::load (tr + 4 * j);
                m[j] = Float4::load (tm + 4 * j);
                c[j] = Float4::load (tc + 4 * j);
                s[j] = Float4::load (ts + 4 * j);
            }
        }

        void store (const uint16_t* lines, float* re, float* im) const noexcept
        {
            float tr[size], tm[size];

            for (size_t j = 0; j < registers; ++j)
            {
                r[j].store (tr + 4 * j);
                m[j].store (tm + 4 * j);
            }

            for (size_t k = 0; k < size; ++k)
            {
                const auto g = 1.5f - 0.5f * (tr[k] * tr[k] + tm[k] * tm[k]);
                re[lines[k]] = tr[k] * g;
                im[lines[k]] = tm[k] * g;
            }
        }

        /** Moves every phasor on by one sample. */
        void rotate() noexcept
        {
            for (size_t j = 0; j < registers; ++j)
            {
                const auto rotated = r[j] * c[j] - m[j] * s[j];
                m[j] = r[j] * s[j] + m[j] * c[j];
                r[j] = rotated;
            }
        }

        /** Turns every step by the same small angle: the frequencies all rise by the same amount
            each sample (the rising comb's glide). */
        void glide (Float4 glideCos, Float4 glideSin) noexcept
        {
            for (size_t j = 0; j < registers; ++j)
            {
                const auto turned = c[j] * glideCos - s[j] * glideSin;
                s[j] = c[j] * glideSin + s[j] * glideCos;
                c[j] = turned;
            }
        }
    };

    /** Runs one chunk for a set of lines (count a multiple of 4 x registers), a group at a
        time. Each group's state stays in registers for the whole chunk and adds its output to
        laneSums[sample * 4 + 0..3] (zeroed first; addLanes adds up the four). */
    template <size_t registers>
    void runLanes (int count, const uint16_t* lines, float* re, float* im, const float* cosStep, const float* sinStep,
                   const float* amp, const float* step, float* laneSums, int numSamples) noexcept
    {
        constexpr auto size = Oscillators<registers>::size;

        for (int group = 0; group < count; group += static_cast<int> (size))
        {
            Oscillators<registers> z;
            z.load (lines + group, re, im, cosStep, sinStep);

            Float4 a[registers], d[registers];

            for (size_t j = 0; j < registers; ++j)
            {
                a[j] = Float4::load (amp + group + 4 * j);
                d[j] = Float4::load (step + group + 4 * j);
            }

            auto* sums = laneSums;

            for (int i = 0; i < numSamples; ++i, sums += 4)
            {
                auto total = a[0] * z.m[0];
                a[0] = a[0] + d[0];

                for (size_t j = 1; j < registers; ++j)
                {
                    total = total + a[j] * z.m[j];
                    a[j] = a[j] + d[j];
                }

                (Float4::load (sums) + total).store (sums);
                z.rotate();
            }

            z.store (lines + group, re, im);
        }
    }

    /** The same with two sets of amplitudes on the same oscillators (a crossfade between two
        plans with one layout), summed separately. */
    template <size_t registers>
    void runLanesTwice (int count, const uint16_t* lines, float* re, float* im, const float* cosStep, const float* sinStep,
                        const float* amp, const float* step, const float* amp2, const float* step2,
                        float* laneSums, float* laneSums2, int numSamples) noexcept
    {
        constexpr auto size = Oscillators<registers>::size;

        for (int group = 0; group < count; group += static_cast<int> (size))
        {
            Oscillators<registers> z;
            z.load (lines + group, re, im, cosStep, sinStep);

            Float4 a[registers], d[registers], a2[registers], d2[registers];

            for (size_t j = 0; j < registers; ++j)
            {
                a[j] = Float4::load (amp + group + 4 * j);
                d[j] = Float4::load (step + group + 4 * j);
                a2[j] = Float4::load (amp2 + group + 4 * j);
                d2[j] = Float4::load (step2 + group + 4 * j);
            }

            auto* sums = laneSums;
            auto* sums2 = laneSums2;

            for (int i = 0; i < numSamples; ++i, sums += 4, sums2 += 4)
            {
                auto total = a[0] * z.m[0];
                auto total2 = a2[0] * z.m[0];
                a[0] = a[0] + d[0];
                a2[0] = a2[0] + d2[0];

                for (size_t j = 1; j < registers; ++j)
                {
                    total = total + a[j] * z.m[j];
                    total2 = total2 + a2[j] * z.m[j];
                    a[j] = a[j] + d[j];
                    a2[j] = a2[j] + d2[j];
                }

                (Float4::load (sums) + total).store (sums);
                (Float4::load (sums2) + total2).store (sums2);
                z.rotate();
            }

            z.store (lines + group, re, im);
        }
    }

    /** runLanes for the rising comb: the same, with every sine's frequency rising by one glide
        step per sample. */
    template <size_t registers>
    void runGlidingLanes (int count, const uint16_t* lines, float* re, float* im, const float* cosStep, const float* sinStep,
                          const float* amp, const float* step, float glideCos, float glideSin, float* laneSums, int numSamples) noexcept
    {
        constexpr auto size = Oscillators<registers>::size;
        const auto gc = Float4::fill (glideCos);
        const auto gs = Float4::fill (glideSin);

        for (int group = 0; group < count; group += static_cast<int> (size))
        {
            Oscillators<registers> z;
            z.load (lines + group, re, im, cosStep, sinStep);

            Float4 a[registers], d[registers];

            for (size_t j = 0; j < registers; ++j)
            {
                a[j] = Float4::load (amp + group + 4 * j);
                d[j] = Float4::load (step + group + 4 * j);
            }

            auto* sums = laneSums;

            for (int i = 0; i < numSamples; ++i, sums += 4)
            {
                auto total = a[0] * z.m[0];
                a[0] = a[0] + d[0];

                for (size_t j = 1; j < registers; ++j)
                {
                    total = total + a[j] * z.m[j];
                    a[j] = a[j] + d[j];
                }

                (Float4::load (sums) + total).store (sums);
                z.rotate();
                z.glide (gc, gs);
            }

            z.store (lines + group, re, im);
        }
    }

    /** A complex number of length 1, multiplied out by hand: std::complex's multiplication
        checks for infinities on every call, which costs more than the multiplication. */
    struct Phasor
    {
        double re = 1.0, im = 0.0;

        static Phasor at (double radians) noexcept      { return { std::cos (radians), std::sin (radians) }; }
        Phasor operator* (Phasor o) const noexcept      { return { re * o.re - im * o.im, re * o.im + im * o.re }; }
        Phasor& operator*= (Phasor o) noexcept          { return *this = *this * o; }
    };

    bool hasComb (const ArtPlan* plan) noexcept
    {
        return plan != nullptr && plan->combLines >= 2 && plan->combLines <= plan->numLines && plan->combSines > 0
            && plan->combActive.size() >= static_cast<size_t> (plan->numColumns)
            && std::isfinite (plan->combGridHz) && plan->combGridHz > 0.0f && std::isfinite (plan->combGridLowHz)
            && std::isfinite (plan->combEdgeHz) && plan->combEdgeHz >= 0.0f
            && std::isfinite (plan->combSlope) && plan->combSlope > 0.0f;
    }

    void addLanes (const float* laneSums, float* sum, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i, laneSums += 4)
            sum[i] = (laneSums[0] + laneSums[2]) + (laneSums[1] + laneSums[3]);
    }
} // namespace

ArtEngine::ArtEngine()
{
    prepare (48000.0, 512);
}

void ArtEngine::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate > 0.0 && std::isfinite (newSampleRate) ? newSampleRate : 48000.0;
    maxBlock = std::max (1, maxBlockSize);

    const auto slots = static_cast<size_t> (maxLines + 1);

    for (auto& bank : banks)
    {
        bank.re.assign (slots, 0.0f);
        bank.im.assign (slots, 0.0f);
        bank.cosStep.assign (slots, 1.0f);
        bank.sinStep.assign (slots, 0.0f);
        bank.frequencies.assign (slots, 0.0f);
        bank.usable.assign (slots, 0);
        bank.re[static_cast<size_t> (dummyLine)] = 1.0f;
        bank.valid = false;
        bank.numLines = 0;
        bank.layoutHash = 0;
    }

    initialRe.resize (slots);
    initialIm.resize (slots);

    for (int line = 0; line < maxLines; ++line)
    {
        const auto phase = initialPhase (line);
        initialRe[static_cast<size_t> (line)] = static_cast<float> (std::cos (phase));
        initialIm[static_cast<size_t> (line)] = static_cast<float> (std::sin (phase));
    }

    initialRe[static_cast<size_t> (dummyLine)] = 1.0f;
    initialIm[static_cast<size_t> (dummyLine)] = 0.0f;

    // The comb's sines start from phases of their own, unrelated to the lines'.
    const auto combSlots = static_cast<size_t> (maxCombSines + 1);
    combInitialRe.assign (combSlots, 1.0f);
    combInitialIm.assign (combSlots, 0.0f);

    for (int sine = 0; sine < maxCombSines; ++sine)
    {
        const auto phase = initialPhase (sine + 0x5A17);
        combInitialRe[static_cast<size_t> (sine)] = static_cast<float> (std::cos (phase));
        combInitialIm[static_cast<size_t> (sine)] = static_cast<float> (std::sin (phase));
    }

    combWork.assign (combSlots + static_cast<size_t> (lanes), static_cast<uint16_t> (maxCombSines));
    combAmp.assign (combSlots + static_cast<size_t> (lanes), 0.0f);
    combStep.assign (combSlots + static_cast<size_t> (lanes), 0.0f);
    combRe.assign (combSlots, 0.0f);
    combIm.assign (combSlots, 0.0f);
    combRe[static_cast<size_t> (maxCombSines)] = 1.0f;
    combCos.assign (combSlots, 1.0f);
    combSin.assign (combSlots, 0.0f);
    combSum.assign (static_cast<size_t> (chunkSize), 0.0f);

    const auto workSize = static_cast<size_t> (maxLines + lanes);
    workLines.assign (workSize, static_cast<uint16_t> (dummyLine));
    workAmp.assign (workSize, 0.0f);
    workStep.assign (workSize, 0.0f);
    workAmp2.assign (workSize, 0.0f);
    workStep2.assign (workSize, 0.0f);
    workMark.assign (static_cast<size_t> (maxLines), 0);

    laneSums.assign (static_cast<size_t> (chunkSize * 4), 0.0f);
    laneSums2.assign (static_cast<size_t> (chunkSize * 4), 0.0f);
    sumA.assign (static_cast<size_t> (chunkSize), 0.0f);
    sumB.assign (static_cast<size_t> (chunkSize), 0.0f);
    gain.assign (static_cast<size_t> (chunkSize), 0.0f);

    gateStep = static_cast<float> (1.0 / std::max (1.0, gateFadeSeconds * sampleRate));
    jumpStep = static_cast<float> (1.0 / std::max (1.0, jumpFadeSeconds * sampleRate));
    planFadeLength = std::max (1, static_cast<int> (std::lround (planFadeSeconds * sampleRate)));

    reset();
}

void ArtEngine::reset() noexcept
{
    envelope = 0.0f;
    gateOn = false;
    once = false;
    fastFadeIn = false;
    jumpPending = false;
    position = 0.0;
    rate = 0.0;
    combClock = 0;

    fadingPlan = nullptr;
    fadingSharesBank = false;
    planFadeRemaining = 0;

    if (hasQueuedPlan)
    {
        currentPlan = queuedPlan;
        hasQueuedPlan = false;
        queuedPlan = nullptr;
    }

    // Starting from the same phases every time keeps renders reproducible.
    if (currentPlan != nullptr)
        setUpBank (banks[currentBank], *currentPlan);
}

void ArtEngine::setPlan (const ArtPlan* plan) noexcept
{
    if (planFadeRemaining > 0)
    {
        // A crossfade is running: this plan follows once it has finished.
        queuedPlan = plan;
        hasQueuedPlan = plan != currentPlan;
        return;
    }

    startPlan (plan);
}

void ArtEngine::startPlan (const ArtPlan* plan) noexcept
{
    if (plan == currentPlan)
        return;

    auto& bank = banks[currentBank];
    const auto sameLayout = plan != nullptr && bank.valid && hasLayout (bank, *plan);

    if (envelope <= 0.0f)
    {
        // Nothing is heard: switch at once.
        if (plan != nullptr && ! sameLayout)
            setUpBank (bank, *plan);

        currentPlan = plan;
        fadingPlan = nullptr;
        fadingSharesBank = false;
        planFadeRemaining = 0;
        return;
    }

    fadingPlan = currentPlan;
    fadingSharesBank = sameLayout;

    if (! sameLayout)
    {
        currentBank = 1 - currentBank;

        if (plan != nullptr)
            setUpBank (banks[currentBank], *plan);
    }

    currentPlan = plan;
    planFadeRemaining = planFadeLength;
}

void ArtEngine::finishPlanFade() noexcept
{
    fadingPlan = nullptr;
    fadingSharesBank = false;
    planFadeRemaining = 0;

    if (hasQueuedPlan)
    {
        const auto* next = queuedPlan;
        hasQueuedPlan = false;
        queuedPlan = nullptr;
        startPlan (next);
    }
}

void ArtEngine::setUpBank (Bank& bank, const ArtPlan& plan) noexcept
{
    const auto numLines = std::min ({ plan.numLines, maxLines, static_cast<int> (plan.frequencies.size()) });
    const auto limit = nyquistGuard * sampleRate;

    for (int line = 0; line < std::max (0, numLines); ++line)
    {
        const auto index = static_cast<size_t> (line);
        const auto frequency = static_cast<double> (plan.frequencies[index]);
        const auto usable = std::isfinite (frequency) && frequency > 0.0 && frequency < limit;
        const auto omega = usable ? twoPi * frequency / sampleRate : 0.0;

        bank.frequencies[index] = plan.frequencies[index];
        bank.usable[index] = usable ? 1 : 0;
        bank.cosStep[index] = static_cast<float> (std::cos (omega));
        bank.sinStep[index] = static_cast<float> (std::sin (omega));
        bank.re[index] = initialRe[index];
        bank.im[index] = initialIm[index];
    }

    bank.numLines = std::max (0, numLines);
    bank.layoutHash = plan.layoutHash;
    bank.valid = true;
}

bool ArtEngine::hasLayout (const Bank& bank, const ArtPlan& plan) const noexcept
{
    const auto numLines = std::min ({ plan.numLines, maxLines, static_cast<int> (plan.frequencies.size()) });

    if (numLines != bank.numLines || plan.layoutHash != bank.layoutHash)
        return false;

    // The hash says so; the frequencies themselves are cheap to compare, and a wrong match
    // would play the wrong pitches.
    return numLines <= 0
        || std::memcmp (plan.frequencies.data(), bank.frequencies.data(), sizeof (float) * static_cast<size_t> (numLines)) == 0;
}

void ArtEngine::render (float* out, int numSamples, const PlaySegment* segments, int numSegments) noexcept
{
    if (out == nullptr || numSamples <= 0)
        return;

    const juce::ScopedNoDenormals noDenormals;
    int done = 0;

    for (int index = 0; segments != nullptr && index < numSegments; ++index)
    {
        const auto& segment = segments[index];
        const auto start = juce::jlimit (done, numSamples, segment.start);
        const auto end = static_cast<int> (juce::jlimit (static_cast<int64_t> (start), static_cast<int64_t> (numSamples),
                                                         static_cast<int64_t> (segment.start) + segment.length));

        renderRange (out, done, start);   // a gap: carry on as before
        applySegment (segment);
        renderRange (out, start, end);
        done = end;
    }

    renderRange (out, done, numSamples);
}

void ArtEngine::applySegment (const PlaySegment& segment) noexcept
{
    const auto fraction = std::isfinite (segment.fraction) ? wrap (segment.fraction) : 0.0;
    const auto perSample = std::isfinite (segment.fractionPerSample) && segment.fractionPerSample > 0.0
                         ? std::min (segment.fractionPerSample, 1.0) : 0.0;

    if (segment.jump)
    {
        if (envelope > 0.0f)
        {
            // Fade out where we are first; the new position keeps moving meanwhile.
            jumpPending = true;
            jumpTarget = fraction;
            jumpRate = perSample;
        }
        else
        {
            jumpPending = false;
            position = fraction;
            rate = perSample;
        }
    }
    else if (jumpPending)
    {
        jumpTarget = fraction;
        jumpRate = perSample;
    }
    else if (segment.playing || envelope <= 0.0f)
    {
        position = fraction;
        rate = perSample;
    }
    // else: the gate closes while sound is heard; the fade-out runs on where it was.

    gateOn = segment.playing;
    once = segment.once;

    if (! gateOn)
        fastFadeIn = false;
}

void ArtEngine::moveOn (int numSamples) noexcept
{
    combClock += numSamples;
    const auto next = position + rate * numSamples;

    if (once && next >= 1.0)
    {
        // A single pass has ended: it stays on its last column (silent by now, or fading out)
        // instead of wrapping round to its start.
        position = endOfPass;
        rate = 0.0;
        return;
    }

    position = wrap (next);
}

void ArtEngine::renderRange (float* out, int from, int to) noexcept
{
    while (from < to)
        from += renderChunk (out + from, to - from);
}

int ArtEngine::samplesLeftInColumn (const ArtPlan& plan) const noexcept
{
    const auto columns = static_cast<double> (plan.numColumns);
    const auto perSample = rate * columns;

    if (perSample <= 0.0)
        return noLimit;

    const auto x = position * columns;
    const auto boundary = std::floor (x) + 1.0;
    const auto samples = std::ceil ((boundary - x) / perSample);

    return samples >= static_cast<double> (noLimit) ? noLimit : std::max (1, static_cast<int> (samples));
}

int ArtEngine::renderChunk (float* out, int maxSamples) noexcept
{
    const auto fading = planFadeRemaining > 0;
    const auto target = jumpPending || ! gateOn ? 0.0f : 1.0f;

    if (envelope <= 0.0f && target <= 0.0f)
    {
        // Silent, and staying so: nothing to compute, and no crossfade anyone would hear.
        if (fading)
            finishPlanFade();

        std::fill (out, out + maxSamples, 0.0f);
        moveOn (maxSamples);
        return maxSamples;
    }

    const auto* heard = isPlayable (currentPlan) ? currentPlan : nullptr;
    const auto* fadingOut = fading && isPlayable (fadingPlan) ? fadingPlan : nullptr;

    auto n = std::min (maxSamples, chunkSize);

    if (jumpPending)
        n = std::min (n, std::max (1, static_cast<int> (std::ceil (envelope / jumpStep))));

    if (fading)
        n = std::min (n, planFadeRemaining);

    if (heard != nullptr)
        n = std::min (n, samplesLeftInColumn (*heard));

    if (fadingOut != nullptr)
        n = std::min (n, samplesLeftInColumn (*fadingOut));

    // The gate / jump envelope.
    const auto step = jumpPending ? jumpStep : (fastFadeIn && target > 0.0f ? jumpStep : gateStep);
    auto e = envelope;

    for (int i = 0; i < n; ++i)
    {
        gain[static_cast<size_t> (i)] = shapeEnvelope (e);
        e = e < target ? std::min (target, e + step) : std::max (target, e - step);
    }

    envelope = e;

    // The plans.
    auto* sum = sumA.data();
    auto* fadingSum = sumB.data();

    if (heard != nullptr && fadingOut != nullptr && fadingSharesBank)
    {
        synthesise (heard, fadingOut, banks[currentBank], sum, fadingSum, n);
    }
    else
    {
        if (heard != nullptr)
            synthesise (heard, nullptr, banks[currentBank], sum, nullptr, n);
        else
            std::fill (sum, sum + n, 0.0f);

        if (fadingOut != nullptr)
            synthesise (fadingOut, nullptr, banks[fadingSharesBank ? currentBank : 1 - currentBank], fadingSum, nullptr, n);
        else if (fading)
            std::fill (fadingSum, fadingSum + n, 0.0f);
    }

    // The rising comb: its crossfades are in its amplitudes, so it is added after the mix.
    const auto combSounds = renderComb (heard, fadingOut, n);

    if (! fading)
    {
        for (int i = 0; i < n; ++i)
            out[i] = gain[static_cast<size_t> (i)] * sum[i];
    }
    else
    {
        // The same oscillators: amplitudes cross linearly. Different ones (unrelated
        // phases): equal power, so the level does not dip in the middle.
        const auto done = planFadeLength - planFadeRemaining;
        const auto scale = 1.0 / planFadeLength;

        for (int i = 0; i < n; ++i)
        {
            const auto t = (done + i) * scale;
            float incoming, outgoing;

            if (fadingSharesBank)
            {
                incoming = static_cast<float> (t);
                outgoing = 1.0f - incoming;
            }
            else
            {
                incoming = static_cast<float> (std::sin (t * twoPi * 0.25));
                outgoing = static_cast<float> (std::cos (t * twoPi * 0.25));
            }

            out[i] = gain[static_cast<size_t> (i)] * (incoming * sum[i] + outgoing * fadingSum[i]);
        }
    }

    if (combSounds)
        for (int i = 0; i < n; ++i)
            out[i] += gain[static_cast<size_t> (i)] * combSum[static_cast<size_t> (i)];

    // Move on.
    moveOn (n);

    if (jumpPending)
        jumpTarget = wrap (jumpTarget + jumpRate * n);

    if (fading)
    {
        planFadeRemaining -= n;

        if (planFadeRemaining <= 0)
            finishPlanFade();
    }

    if (jumpPending && envelope <= 0.0f)
    {
        jumpPending = false;
        position = jumpTarget;
        rate = jumpRate;
        fastFadeIn = true;
    }

    if (envelope >= 1.0f)
        fastFadeIn = false;

    return n;
}

int ArtEngine::gather (const ArtPlan* plan, const ArtPlan* second, const Bank& bank) noexcept
{
    struct Column
    {
        size_t first = 0, next = 0;   // offsets of the two columns in the amplitude array
        uint32_t from = 0, to = 0;    // the active list
        float fraction = 0.0f, perSample = 0.0f;
    };

    const auto locate = [this] (const ArtPlan& p)
    {
        Column column;
        const auto columns = p.numColumns;
        const auto x = position * columns;
        const auto c = juce::jlimit (0, columns - 1, static_cast<int> (std::floor (x)));
        const auto next = c + 1 < columns ? c + 1 : (once ? c : 0);
        const auto lines = static_cast<size_t> (p.numLines);

        column.first = static_cast<size_t> (c) * lines;
        column.next = static_cast<size_t> (next) * lines;
        column.from = p.activeStart[static_cast<size_t> (c)];
        column.to = std::min (p.activeStart[static_cast<size_t> (c) + 1], static_cast<uint32_t> (p.activeLines.size()));
        column.fraction = static_cast<float> (juce::jlimit (0.0, 1.0, x - c));
        column.perSample = static_cast<float> (rate * columns);
        return column;
    };

    const auto numLines = bank.numLines;
    int count = 0;

    const auto collect = [&] (const ArtPlan& p, const Column& column)
    {
        for (auto j = column.from; j < column.to; ++j)
        {
            const auto line = p.activeLines[j];

            if (line >= numLines || bank.usable[line] == 0 || workMark[line] != 0)
                continue;

            workMark[line] = 1;
            workLines[static_cast<size_t> (count++)] = line;
        }
    };

    const auto amplitudes = [&] (const ArtPlan& p, const Column& column, float* amp, float* step)
    {
        for (int k = 0; k < count; ++k)
        {
            const auto line = workLines[static_cast<size_t> (k)];
            const auto a0 = p.amplitudes[column.first + line];
            const auto a1 = p.amplitudes[column.next + line];
            amp[k] = a0 + (a1 - a0) * column.fraction;
            step[k] = (a1 - a0) * column.perSample;
        }
    };

    const auto first = locate (*plan);
    collect (*plan, first);

    Column other;

    if (second != nullptr)
    {
        other = locate (*second);
        collect (*second, other);
    }

    for (int k = 0; k < count; ++k)
        workMark[workLines[static_cast<size_t> (k)]] = 0;

    amplitudes (*plan, first, workAmp.data(), workStep.data());

    if (second != nullptr)
        amplitudes (*second, other, workAmp2.data(), workStep2.data());

    // Pad to whole groups with the silent dummy line.
    while (count % lanes != 0)
    {
        const auto k = static_cast<size_t> (count++);
        workLines[k] = static_cast<uint16_t> (dummyLine);
        workAmp[k] = workStep[k] = workAmp2[k] = workStep2[k] = 0.0f;
    }

    return count;
}

void ArtEngine::synthesise (const ArtPlan* plan, const ArtPlan* second, Bank& bank, float* sum, float* secondSum,
                            int numSamples) noexcept
{
    const auto count = gather (plan, second, bank);
    static_assert (lanes % 4 == 0, "groups are whole numbers of 4-float registers");
    const auto laneCount = static_cast<size_t> (numSamples * 4);

    std::fill (laneSums.begin(), laneSums.begin() + static_cast<std::ptrdiff_t> (laneCount), 0.0f);

    if (second == nullptr)
    {
        runLanes<static_cast<size_t> (lanes / 4)> (count, workLines.data(), bank.re.data(), bank.im.data(), bank.cosStep.data(), bank.sinStep.data(),
                         workAmp.data(), workStep.data(), laneSums.data(), numSamples);
        addLanes (laneSums.data(), sum, numSamples);
        return;
    }

    std::fill (laneSums2.begin(), laneSums2.begin() + static_cast<std::ptrdiff_t> (laneCount), 0.0f);
    runLanesTwice<static_cast<size_t> (lanes / 4)> (count, workLines.data(), bank.re.data(), bank.im.data(), bank.cosStep.data(), bank.sinStep.data(),
                          workAmp.data(), workStep.data(), workAmp2.data(), workStep2.data(),
                          laneSums.data(), laneSums2.data(), numSamples);
    addLanes (laneSums.data(), sum, numSamples);
    addLanes (laneSums2.data(), secondSum, numSamples);
}

bool ArtEngine::renderComb (const ArtPlan* heard, const ArtPlan* fadingOut, int numSamples) noexcept
{
    const auto fading = planFadeRemaining > 0;
    const auto heardComb = hasComb (heard);
    const auto fadingComb = fading && hasComb (fadingOut);

    if (! heardComb && ! fadingComb)
        return false;

    // The comb's crossfade gains, as renderChunk's for the lines, at the chunk's start and end
    // (the chunk never runs past the end of the crossfade).
    const auto done = planFadeLength - planFadeRemaining;
    const auto t0 = fading ? static_cast<float> (done) / static_cast<float> (planFadeLength) : 1.0f;
    const auto t1 = fading ? static_cast<float> (std::min (planFadeLength, done + numSamples)) / static_cast<float> (planFadeLength) : 1.0f;
    const auto quarter = static_cast<float> (twoPi * 0.25);
    const auto incoming = [&] (float t) { return ! fading ? 1.0f : (fadingSharesBank ? t : std::sin (t * quarter)); };
    const auto outgoing = [&] (float t) { return fadingSharesBank ? 1.0f - t : std::cos (t * quarter); };

    std::fill (laneSums.begin(), laneSums.begin() + static_cast<std::ptrdiff_t> (numSamples * 4), 0.0f);
    auto sounding = false;

    const auto run = [&] (const ArtPlan& plan, int count)
    {
        if (count <= 0)
            return;

        // The sines' steps turn by this much each sample, so that their frequencies rise by the
        // slope (Hz per second) divided by the sample rate.
        const auto turn = twoPi * static_cast<double> (plan.combSlope) / (sampleRate * sampleRate);
        runGlidingLanes<static_cast<size_t> (lanes / 4)> (count, combWork.data(), combRe.data(), combIm.data(),
                                                          combCos.data(), combSin.data(), combAmp.data(), combStep.data(),
                                                          static_cast<float> (std::cos (turn)), static_cast<float> (std::sin (turn)),
                                                          laneSums.data(), numSamples);
        sounding = true;
    };

    if (heardComb)
    {
        // A crossfade between plans of one layout plays both on the same sines.
        const auto* second = fadingComb && fadingSharesBank ? fadingOut : nullptr;
        run (*heard, gatherComb (*heard, incoming (t0), incoming (t1), second, outgoing (t0), outgoing (t1), numSamples));
    }

    if (fadingComb && ! (heardComb && fadingSharesBank))
        run (*fadingOut, gatherComb (*fadingOut, outgoing (t0), outgoing (t1), nullptr, 0.0f, 0.0f, numSamples));

    if (sounding)
        addLanes (laneSums.data(), combSum.data(), numSamples);

    return sounding;
}

int ArtEngine::gatherComb (const ArtPlan& plan, float weightStart, float weightEnd,
                           const ArtPlan* second, float secondStart, float secondEnd, int numSamples) noexcept
{
    // A plan's grid at the column being played and the next, and how far between them the chunk
    // starts and ends (it never crosses a column boundary).
    struct View
    {
        const float* here = nullptr;
        const float* next = nullptr;
        float start = 0.0f, end = 0.0f;
        bool active = false;
    };

    const auto viewOf = [this, numSamples] (const ArtPlan& p)
    {
        View v;
        const auto columns = p.numColumns;
        const auto x = position * columns;
        const auto c = juce::jlimit (0, columns - 1, static_cast<int> (std::floor (x)));
        const auto next = c + 1 < columns ? c + 1 : (once ? c : 0);
        const auto lines = static_cast<size_t> (p.numLines);

        v.here = p.amplitudes.data() + static_cast<size_t> (c) * lines;
        v.next = p.amplitudes.data() + static_cast<size_t> (next) * lines;
        v.start = static_cast<float> (juce::jlimit (0.0, 1.0, x - c));
        v.end = static_cast<float> (juce::jlimit (0.0, 1.0, x - c + rate * columns * numSamples));
        v.active = p.combActive[static_cast<size_t> (c)] != 0;
        return v;
    };

    // The amplitude at a frequency: interpolated between the grid lines around it, fading to
    // silence over combEdgeHz beyond the ends of the grid.
    const auto gridLines = plan.combLines;
    const auto gridLow = static_cast<double> (plan.combGridLowHz);
    const auto gridHz = static_cast<double> (plan.combGridHz);
    const auto gridHigh = gridLow + (gridLines - 1) * gridHz;
    const auto edgeHz = std::max (1.0e-3, static_cast<double> (plan.combEdgeHz));

    const auto amplitudeAt = [=] (const View& v, float between, double hz)
    {
        const auto lerp = [between] (float here, float next) { return here + (next - here) * between; };

        if (hz < gridLow)
            return hz > gridLow - edgeHz ? lerp (v.here[0], v.next[0]) * static_cast<float> (1.0 - (gridLow - hz) / edgeHz) : 0.0f;

        if (hz >= gridHigh)
        {
            const auto last = static_cast<size_t> (gridLines - 1);
            return hz < gridHigh + edgeHz ? lerp (v.here[last], v.next[last]) * static_cast<float> (1.0 - (hz - gridHigh) / edgeHz) : 0.0f;
        }

        const auto u = (hz - gridLow) / gridHz;
        const auto k = std::min (gridLines - 2, static_cast<int> (u));
        const auto f = static_cast<float> (u - k);
        const auto line = static_cast<size_t> (k);
        const auto here = v.here[line] + (v.here[line + 1] - v.here[line]) * f;
        const auto next = v.next[line] + (v.next[line + 1] - v.next[line]) * f;
        return here + (next - here) * between;
    };

    const auto first = viewOf (plan);
    const auto other = second != nullptr ? viewOf (*second) : View {};
    const auto heardHere = first.active && (weightStart > 0.0f || weightEnd > 0.0f);
    const auto otherHere = other.active && (secondStart > 0.0f || secondEnd > 0.0f);
    int count = 0;

    if (heardHere || otherHere)
    {
        // Sine j starts j / sines of the way up the glide, and has moved on by the glide so far;
        // one sine at most has passed the top and wrapped round to the bottom.
        //
        // A sine's phase is a function of time alone: its own starting phase plus what it has
        // gathered since it last left the bottom of the glide. Offset o Hz above the bottom, it
        // left o / slope seconds ago, and has turned by 2 pi (bottom o + o^2 / 2) / slope since.
        // So where the blocks and chunks fall cannot change the sound (a phase carried from chunk
        // to chunk would come out of each wrap wherever the chunk happened to end). The sines'
        // steps and phases are worked out by turning from one sine's to the next, in double
        // precision: a cosine and a sine per sine and chunk would cost more than the comb's
        // synthesis itself.
        const auto sines = std::min (plan.combSines, maxCombSines);
        const auto slope = static_cast<double> (plan.combSlope);
        const auto bottom = gridLow - edgeHz;
        const auto length = gridHigh + edgeHz - bottom;
        const auto spacing = length / sines;
        const auto start = slope * (static_cast<double> (combClock) / sampleRate);
        const auto glideHz = slope * numSamples / sampleRate;
        const auto limit = nyquistGuard * sampleRate;
        const auto radiansPerHz = twoPi / sampleRate;
        const auto phaseAt = [=] (double o) { return twoPi * (bottom + 0.5 * o) * o / slope; };
        const auto phaseTurnAt = [=] (double o) { return twoPi * (bottom + o + 0.5 * spacing) * spacing / slope; };

        auto offset = std::fmod (start, length);
        auto step = Phasor::at ((bottom + offset) * radiansPerHz);
        auto phase = Phasor::at (phaseAt (offset));
        auto phaseTurn = Phasor::at (phaseTurnAt (offset));
        const auto stepTurn = Phasor::at (spacing * radiansPerHz);
        const auto phaseTurnTurn = Phasor::at (twoPi * spacing * spacing / slope);

        for (int j = 0; j < sines; ++j, offset += spacing, step *= stepTurn, phase *= phaseTurn, phaseTurn *= phaseTurnTurn)
        {
            if (offset >= length)
            {
                offset -= length;
                step = Phasor::at ((bottom + offset) * radiansPerHz);
                phase = Phasor::at (phaseAt (offset));
                phaseTurn = Phasor::at (phaseTurnAt (offset));
            }

            const auto hz = bottom + offset;
            auto a0 = heardHere ? weightStart * amplitudeAt (first, first.start, hz) : 0.0f;
            auto a1 = heardHere ? weightEnd * amplitudeAt (first, first.end, hz + glideHz) : 0.0f;

            if (otherHere)
            {
                a0 += secondStart * amplitudeAt (other, other.start, hz);
                a1 += secondEnd * amplitudeAt (other, other.end, hz + glideHz);
            }

            if (! (a0 > 0.0f || a1 > 0.0f) || ! (hz > 0.0 && hz < limit))
                continue;

            const auto k = static_cast<size_t> (count++);
            const auto sine = static_cast<size_t> (j);
            const auto now = phase * Phasor { static_cast<double> (combInitialRe[sine]), static_cast<double> (combInitialIm[sine]) };
            combWork[k] = static_cast<uint16_t> (j);
            combAmp[k] = a0;
            combStep[k] = (a1 - a0) / static_cast<float> (numSamples);
            combRe[sine] = static_cast<float> (now.re);
            combIm[sine] = static_cast<float> (now.im);
            combCos[sine] = static_cast<float> (step.re);
            combSin[sine] = static_cast<float> (step.im);
        }
    }

    if (count == 0)
        return 0;

    // Pad to whole groups with the silent dummy sine.
    while (count % lanes != 0)
    {
        const auto k = static_cast<size_t> (count++);
        combWork[k] = static_cast<uint16_t> (maxCombSines);
        combAmp[k] = combStep[k] = 0.0f;
    }

    return count;
}

} // namespace freqart
