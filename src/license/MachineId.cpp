#include "MachineId.h"

#include "LicenseStore.h"

#include <juce_cryptography/juce_cryptography.h>

namespace anymeans::license
{

juce::String MachineId::hash (const juce::String& identifier)
{
    return juce::SHA256 (("AnyMeans|" + identifier).toUTF8()).toHexString().toLowerCase();
}

bool MachineId::looksValid (const juce::String& machineId)
{
    return machineId.length() == 64 && machineId.containsOnly ("0123456789abcdef");
}

juce::String MachineId::chooseIdentifier (const juce::StringArray& machineIdentifiers,
                                          const juce::String& uniqueDeviceId,
                                          const juce::File& sharedFolder)
{
    for (const auto& id : machineIdentifiers)
        if (id.trim().isNotEmpty())
            return id.trim();

    if (uniqueDeviceId.trim().isNotEmpty())
        return uniqueDeviceId.trim();

    // No hardware ID at all (JUCE couldn't read one): a random ID, made once and kept in the
    // shared folder, stands in for it. Marked so it can never equal a real hardware ID.
    const auto fallback = LicenseStore (sharedFolder).readOrCreateFallbackMachineIdentifier();
    return fallback.isNotEmpty() ? "random:" + fallback : juce::String();
}

juce::String MachineId::get (const juce::File& sharedFolder)
{
    // Reading the hardware ID can be slow (on Linux JUCE runs lscpu), and it can't change while
    // the plug-in runs, so it is read once per process.
    static const juce::String hardwareId = []
    {
        const auto identifiers = juce::SystemStats::getMachineIdentifiers (juce::SystemStats::MachineIdFlags::uniqueId);

        for (const auto& id : identifiers)
            if (id.trim().isNotEmpty())
                return id.trim();

        // In JUCE 8 this is where the list above comes from; asked again in case a later JUCE
        // stops listing it.
        return juce::SystemStats::getUniqueDeviceID().trim();
    }();

    const auto identifier = chooseIdentifier ({ hardwareId }, {}, sharedFolder);
    return identifier.isNotEmpty() ? hash (identifier) : juce::String();
}

} // namespace anymeans::license
