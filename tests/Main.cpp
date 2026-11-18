#include "LicenseTestSupport.h"
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

    // Every plug-in instance the tests make is activated, in a folder of the runner's own,
    // unless a test says otherwise: none reads the user's real licence file or reaches the
    // real server (see LicenseTestSupport.h).
    licensetest::installRunnerLicensing();
    const auto result = test::runAll (argc, argv);
    licensetest::removeRunnerLicensing();
    return result;
}
