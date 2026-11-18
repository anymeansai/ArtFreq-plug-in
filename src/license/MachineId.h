#pragma once

#include <juce_core/juce_core.h>

namespace anymeans::license
{

/** The ID this computer activates under: SHA-256 of "AnyMeans|" + the system's hardware ID, as 64
    lowercase hex digits. Only the hash leaves the computer, never the hardware ID itself; the
    "AnyMeans|" prefix keeps it from matching other vendors' hashes of the same ID.

    It has to be the same in every AnyMeans plug-in on the computer (they share activations) and
    from one launch to the next (a new ID uses up another of the key's computers). The hardware ID
    is JUCE's SystemStats::getMachineIdentifiers (uniqueId), which in JUCE 8 is getUniqueDeviceID():

        macOS     the IOPlatformUUID (the Mac's hardware UUID): the same for every user account
        Windows   a hash JUCE makes of the SMBIOS tables: the system's manufacturer, model and
                  UUID, the motherboard's and the processors' make, model and serial numbers
                  (so replacing the motherboard or the processor makes a new ID)
        Linux     a hash of the board serial (or the BIOS's vendor, version and dates) and the
                  CPU model reported by lscpu

    Note that on Windows and Linux JUCE computes the hash itself: should a JUCE update ever change
    that computation, every activated Windows computer would get a new ID. Keep JUCE pinned, and
    check this ID on a few machines before changing it.

    If the system gives no ID at all, a random one is made once and kept in the shared AnyMeans
    folder (LicenseStore::readOrCreateFallbackMachineIdentifier): stable until that folder is
    deleted, after which the computer counts as a new one.
*/
struct MachineId
{
    /** This computer's ID. The hardware part is worked out once per process. */
    static juce::String get (const juce::File& sharedFolder);

    /** SHA-256 of "AnyMeans|" + identifier, lowercase hex. */
    static juce::String hash (const juce::String& identifier);

    /** The identifier to hash: the first non-empty one from the system's list, else the device ID,
        else the folder's random fallback (empty only if that can't be stored either).
    */
    static juce::String chooseIdentifier (const juce::StringArray& machineIdentifiers,
                                          const juce::String& uniqueDeviceId,
                                          const juce::File& sharedFolder);

    /** True for something shaped like an ID from hash(). */
    static bool looksValid (const juce::String& machineId);
};

} // namespace anymeans::license
