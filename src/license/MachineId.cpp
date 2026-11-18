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

bool MachineId::isUsableHardwareIdentifier (const juce::String& identifier)
{
    // On Windows and Linux JUCE's ID is String (hashCode64 (description)), and the hash of an
    // empty description is 0: a system that describes nothing (no SMBIOS tables JUCE reads, no
    // /sys/class/dmi and no lscpu) gets "0", the same on every such computer. Taken as an ID, one
    // key would unlock all of them as one computer, so it counts as no ID at all.
    const auto trimmed = identifier.trim();
    return trimmed.isNotEmpty() && trimmed != "0";
}

juce::String MachineId::chooseIdentifier (const juce::StringArray& machineIdentifiers,
                                          const juce::String& uniqueDeviceId,
                                          const juce::File& sharedFolder)
{
    for (const auto& id : machineIdentifiers)
        if (isUsableHardwareIdentifier (id))
            return id.trim();

    if (isUsableHardwareIdentifier (uniqueDeviceId))
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
            if (isUsableHardwareIdentifier (id))
                return id.trim();

        // In JUCE 8 this is where the list above comes from; asked again in case a later JUCE
        // stops listing it.
        const auto deviceId = juce::SystemStats::getUniqueDeviceID();
        return isUsableHardwareIdentifier (deviceId) ? deviceId.trim() : juce::String();
    }();

    const auto identifier = chooseIdentifier ({ hardwareId }, {}, sharedFolder);
    return identifier.isNotEmpty() ? hash (identifier) : juce::String();
}

} // namespace anymeans::license
