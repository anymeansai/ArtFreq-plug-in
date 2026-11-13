#include "Parameters.h"

#include <cmath>

namespace freqart::params
{

namespace
{

juce::String formatDb (float value, int decimals)
{
    // Avoid "-0.0 dB".
    if (std::abs (value) < 0.05f)
        value = 0.0f;

    return juce::String (value, decimals) + " dB";
}

juce::String formatHzValue (float value)
{
    return formatHz (static_cast<double> (value));
}

float parseNumber (const juce::String& text)
{
    // Accept "4.5k", "4500 Hz", "-6 dB", "120 bpm" and so on.
    const auto trimmed = text.trim().toLowerCase();
    auto value = trimmed.getFloatValue();

    if (trimmed.containsChar ('k') && ! trimmed.contains ("db"))
        value *= 1000.0f;

    return value;
}

juce::NormalisableRange<float> densityRange()
{
    // 16..1024 lines, skewed so the middle of the knob sits near 200 lines.
    juce::NormalisableRange<float> range (16.0f, 1024.0f, 1.0f);
    range.setSkewForCentre (200.0f);
    return range;
}

juce::NormalisableRange<float> frequencyRange (float low, float high, float centre)
{
    juce::NormalisableRange<float> range (low, high, 1.0f);
    range.setSkewForCentre (centre);
    return range;
}

} // namespace

juce::StringArray lengthNames()
{
    return { "1 beat", "2 beats", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars", "32 bars" };
}

juce::StringArray triggerNames()     { return { "Transport", "MIDI note", "Free loop" }; }
juce::StringArray tempoSourceNames() { return { "Host", "Manual" }; }
juce::StringArray scaleNames()       { return { "Log", "Linear", "Mel" }; }
juce::StringArray slopeNames()       { return { "0 dB/oct", "3 dB/oct", "4.5 dB/oct", "6 dB/oct" }; }

double lengthInQuarterNotes (int lengthIndex, double quarterNotesPerBar) noexcept
{
    if (! std::isfinite (quarterNotesPerBar) || quarterNotesPerBar <= 0.0)
        quarterNotesPerBar = 4.0;

    quarterNotesPerBar = juce::jlimit (0.5, 32.0, quarterNotesPerBar);

    switch (juce::jlimit (0, 7, lengthIndex))
    {
        case 0:  return 1.0;
        case 1:  return 2.0;
        case 2:  return quarterNotesPerBar;
        case 3:  return quarterNotesPerBar * 2.0;
        case 4:  return quarterNotesPerBar * 4.0;
        case 5:  return quarterNotesPerBar * 8.0;
        case 6:  return quarterNotesPerBar * 16.0;
        default: return quarterNotesPerBar * 32.0;
    }
}

float slopeForIndex (int slopeIndex) noexcept
{
    static constexpr float slopes[] { 0.0f, 3.0f, 4.5f, 6.0f };
    return slopes[juce::jlimit (0, 3, slopeIndex)];
}

ScaleType scaleForIndex (int scaleIndex) noexcept
{
    switch (scaleIndex)
    {
        case 1:  return ScaleType::linear;
        case 2:  return ScaleType::mel;
        default: return ScaleType::log;
    }
}

TriggerMode triggerForIndex (int triggerIndex) noexcept
{
    switch (triggerIndex)
    {
        case 1:  return TriggerMode::midiNote;
        case 2:  return TriggerMode::freeLoop;
        default: return TriggerMode::transport;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using Float = juce::AudioParameterFloat;
    using Choice = juce::AudioParameterChoice;
    using FloatAttributes = juce::AudioParameterFloatAttributes;
    using ChoiceAttributes = juce::AudioParameterChoiceAttributes;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ---- sound ---------------------------------------------------------------------------
    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::level, 1 }, "Level",
        juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -6.0f,
        FloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return formatDb (v, 1); })
            .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); })));

    // Density and Contrast reshape the plan, which is rebuilt on the message thread (~25 Hz, by
    // wall-clock time), not in step with the audio. Automated, they would land late, and at a
    // different place on every faster-than-real-time bounce, so (like the analyser settings
    // below) they are not automation targets.
    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::density, 1 }, "Density",
        densityRange(), 256.0f,
        FloatAttributes()
            .withAutomatable (false)
            .withLabel ("lines")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " lines"; })
            .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); })));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::contrast, 1 }, "Contrast",
        juce::NormalisableRange<float> (12.0f, 72.0f, 0.5f), 40.0f,
        FloatAttributes()
            .withAutomatable (false)
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return formatDb (v, 0); })
            .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); })));

    // ---- timing --------------------------------------------------------------------------
    layout.add (std::make_unique<Choice> (
        juce::ParameterID { id::length, 1 }, "Length", lengthNames(), defaultLengthIndex));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { id::trigger, 1 }, "Trigger", triggerNames(), 0));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { id::tempoSource, 1 }, "Tempo", tempoSourceNames(), 0));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::bpm, 1 }, "BPM",
        juce::NormalisableRange<float> (20.0f, 300.0f, 0.01f), 120.0f,
        FloatAttributes()
            .withLabel ("BPM")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " BPM"; })
            .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); })));

    // ---- analyser match: how the user's spectrogram draws frequency. Changing these mid-song
    // would move the whole drawing, so they are settings, not automation targets. ----------
    layout.add (std::make_unique<Choice> (
        juce::ParameterID { id::scale, 1 }, "Analyser scale", scaleNames(), 0,
        ChoiceAttributes().withAutomatable (false)));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::rangeLow, 1 }, "Analyser low",
        frequencyRange (10.0f, 1000.0f, 50.0f), 20.0f,
        FloatAttributes()
            .withAutomatable (false)
            .withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int) { return formatHzValue (v); })
            .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); })));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::rangeHigh, 1 }, "Analyser high",
        frequencyRange (2000.0f, 24000.0f, 10000.0f), 20000.0f,
        FloatAttributes()
            .withAutomatable (false)
            .withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int) { return formatHzValue (v); })
            .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); })));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { id::slope, 1 }, "Analyser slope", slopeNames(), 0,
        ChoiceAttributes().withAutomatable (false)));

    return layout;
}

} // namespace freqart::params
