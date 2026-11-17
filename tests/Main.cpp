#include "TestFramework.h"

#include <juce_events/juce_events.h>

int main (int argc, char* argv[])
{
   #if JUCE_LINUX || JUCE_BSD
    // The key focus tests put the editor in a real window on the (virtual) display. As a
    // standalone app JUCE installs X error handlers that tolerate a display without a window
    // manager (Xlib's own would end the process). No application object is created.
    juce::JUCEApplicationBase::createInstance = [] () -> juce::JUCEApplicationBase* { return nullptr; };
   #endif

    return test::runAll (argc, argv);
}
