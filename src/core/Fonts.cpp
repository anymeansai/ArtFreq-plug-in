#include "Fonts.h"

#include "FreqArtAssets.h"

#include <array>

namespace freqart::fonts
{

namespace
{

enum class Face
{
    interRegular,
    interMedium,
    interSemiBold,
    interBold,
    spaceGroteskSemiBold,
    jostMedium,
    frauncesSemiBold,
    dmMonoRegular,
    dmMonoMedium,
    count
};

constexpr auto numFaces = static_cast<size_t> (Face::count);

juce::Typeface::Ptr defaultSansSerif()
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), 12.0f, juce::Font::plain)).getTypefacePtr();
}

/** Every embedded face, created once on first use (thread-safe function-local static). A face
    that fails to load is replaced by the default sans-serif so text never disappears. */
const std::array<juce::Typeface::Ptr, numFaces>& faces()
{
    static const auto table = []
    {
        std::array<juce::Typeface::Ptr, numFaces> result;

        const auto load = [&result] (Face face, const char* data, int size)
        {
            auto typeface = juce::Typeface::createSystemTypefaceFor (data, static_cast<size_t> (size));
            jassert (typeface != nullptr);
            result[static_cast<size_t> (face)] = typeface;
        };

        load (Face::interRegular,         FreqArtAssets::InterRegular_ttf,         FreqArtAssets::InterRegular_ttfSize);
        load (Face::interMedium,          FreqArtAssets::InterMedium_ttf,          FreqArtAssets::InterMedium_ttfSize);
        load (Face::interSemiBold,        FreqArtAssets::InterSemiBold_ttf,        FreqArtAssets::InterSemiBold_ttfSize);
        load (Face::interBold,            FreqArtAssets::InterBold_ttf,            FreqArtAssets::InterBold_ttfSize);
        load (Face::spaceGroteskSemiBold, FreqArtAssets::SpaceGroteskSemiBold_ttf, FreqArtAssets::SpaceGroteskSemiBold_ttfSize);
        load (Face::jostMedium,           FreqArtAssets::JostMedium_ttf,           FreqArtAssets::JostMedium_ttfSize);
        load (Face::frauncesSemiBold,     FreqArtAssets::FrauncesSemiBold_ttf,     FreqArtAssets::FrauncesSemiBold_ttfSize);
        load (Face::dmMonoRegular,        FreqArtAssets::DMMonoRegular_ttf,        FreqArtAssets::DMMonoRegular_ttfSize);
        load (Face::dmMonoMedium,         FreqArtAssets::DMMonoMedium_ttf,         FreqArtAssets::DMMonoMedium_ttfSize);

        for (auto& typeface : result)
            if (typeface == nullptr)
                typeface = defaultSansSerif();

        return result;
    }();

    return table;
}

juce::Typeface::Ptr face (Face which)
{
    return faces()[static_cast<size_t> (which)];
}

juce::Font makeFont (const juce::Typeface::Ptr& typeface, float height)
{
    const auto size = juce::jmax (0.1f, height);
    auto options = typeface != nullptr ? juce::FontOptions (typeface) : juce::FontOptions();
    return juce::Font (options.withPointHeight (size));
}

struct ArtFont
{
    const char* name;
    Face face;
};

constexpr ArtFont artFonts[] = {
    { "Inter Bold",    Face::interBold },
    { "Space Grotesk", Face::spaceGroteskSemiBold },
    { "Jost",          Face::jostMedium },
    { "Fraunces",      Face::frauncesSemiBold },
    { "DM Mono",       Face::dmMonoMedium }
};

constexpr int numArt = static_cast<int> (sizeof (artFonts) / sizeof (artFonts[0]));

const ArtFont& artFont (int index) noexcept
{
    return artFonts[index >= 0 && index < numArt ? index : 0];
}

} // namespace

juce::Font ui (float height, UiWeight weight)
{
    auto which = Face::interRegular;

    switch (weight)
    {
        case UiWeight::regular:  which = Face::interRegular;  break;
        case UiWeight::medium:   which = Face::interMedium;   break;
        case UiWeight::semiBold: which = Face::interSemiBold; break;
        case UiWeight::bold:     which = Face::interBold;     break;
    }

    return makeFont (face (which), height);
}

juce::Font mono (float height, bool medium)
{
    return makeFont (face (medium ? Face::dmMonoMedium : Face::dmMonoRegular), height);
}

int numArtFonts() noexcept
{
    return numArt;
}

juce::String artFontName (int index)
{
    return artFont (index).name;
}

juce::Font art (int index, float height)
{
    return makeFont (artTypeface (index), height);
}

juce::Typeface::Ptr artTypeface (int index)
{
    return face (artFont (index).face);
}

} // namespace freqart::fonts
