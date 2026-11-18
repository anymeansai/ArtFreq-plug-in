#pragma once

#include <juce_core/juce_core.h>

namespace anymeans::license
{

/** juce::JSON::parse, for text this code didn't write itself (the server's answers, the shared
    file), refused when it nests deeper than maxDepth.

    JUCE's parser recurses once per level of [ or {, and a few thousand levels (8 KB of "[") are
    enough to overflow a plug-in thread's stack (512 KB on macOS) and take the host down with it.
    Nothing real comes close: the server's answers nest one level deep, the shared file three. The
    scan follows the parser's own rules for strings (double or single quotes, backslash escapes),
    so brackets inside strings don't count.
*/
inline juce::Result parseShallowJson (const juce::String& text, juce::var& result, int maxDepth = 32)
{
    int depth = 0;
    juce::juce_wchar quote = 0;
    bool escaped = false;

    for (auto p = text.getCharPointer(); ! p.isEmpty();)
    {
        const auto c = p.getAndAdvance();

        if (quote != 0)
        {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == quote)
                quote = 0;

            continue;
        }

        if (c == '"' || c == '\'')
            quote = c;
        else if ((c == '[' || c == '{') && ++depth > maxDepth)
            return juce::Result::fail ("JSON nested too deeply");
        else if (c == ']' || c == '}')
            --depth;
    }

    result = juce::var();
    return juce::JSON::parse (text, result);
}

} // namespace anymeans::license
