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

        macOS     the IOPlatformUUID (the Mac's hardware UUID), read from the I/O Registry: the
                  same for every user account, across reboots, OS updates and network changes;
                  new only with a new logic board
        Windows   String (hashCode64) of SMBIOS fields JUCE reads with GetSystemFirmwareTable:
                  the system's manufacturer, model and UUID; the motherboard's make, model,
                  version, serial number and asset tag; each processor's make, model, asset tag
                  and part number. Machine-wide (every user account), unchanged by reboots,
                  Windows updates and networks; new with a new motherboard or processor, and
                  possibly with a firmware update that rewrites those fields
        Linux     a hash of the board serial (or the BIOS's vendor, version and dates) and the
                  CPU model reported by lscpu (child processes; Linux isn't a product platform)

    None of them starts a process or waits on anything on macOS or Windows, so reading it on the
    message thread is fine; it is read once per process anyway.

    Limits: on Windows and Linux JUCE computes the hash itself, so should a JUCE update ever change
    that computation, every activated Windows computer would get a new ID: keep JUCE pinned, and
    check this ID on a few machines before changing it. A system that describes nothing gets "0",
    which is refused here (isUsableHardwareIdentifier). Boards whose firmware leaves every one of
    those fields at the maker's placeholder ("To be filled by O.E.M.", a template UUID) give the
    same ID on every such computer; nothing here can tell.

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

    /** The identifier to hash: the first usable one from the system's list, else the device ID,
        else the folder's random fallback (empty only if that can't be stored either).
    */
    static juce::String chooseIdentifier (const juce::StringArray& machineIdentifiers,
                                          const juce::String& uniqueDeviceId,
                                          const juce::File& sharedFolder);

    /** False for an empty ID and for "0", which JUCE gives every computer it can learn nothing
        about (see the .cpp): those get the random fallback instead.
    */
    static bool isUsableHardwareIdentifier (const juce::String& identifier);

    /** True for something shaped like an ID from hash(). */
    static bool looksValid (const juce::String& machineId);
};

} // namespace anymeans::license
