#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <vector>

namespace anymeans::license
{

/** One activation, as kept on this computer. */
struct StoredActivation
{
    juce::String productId;    // the licence's product (field 2 of the licence)
    juce::String key;          // the key in its stored form (LicenseKey::normalise)
    juce::String machineId;    // the hashed machine ID the activation was made for
    juce::String token;        // the server's activation token (an HMAC only the server can check; kept for support)
    juce::String license;      // the signed licence, exactly as the server sent it
    juce::String signature;    // its signature, exactly as the server sent it (base64)
    juce::String activatedAt;  // when, by this computer's clock (ISO 8601)
    juce::String activatedIn;  // which plug-in activated it ("ArtFreq FX"), for support
};

/** The activations of every AnyMeans plug-in for one user, in one shared file:

        macOS     ~/Library/Application Support/AnyMeans/Licenses.json
        Windows   %APPDATA%\AnyMeans\Licenses.json
        Linux     ~/.config/AnyMeans/Licenses.json

    One file for all the plug-ins, so a key entered in one (an ArtFreq Pro key in ArtFreq FX)
    activates every plug-in it covers on this computer (ArtFreq too), and the other AnyMeans
    plug-ins (Creme, Vocalyst) keep theirs next to it, each under its own product ID.

    The file is JSON:

        {
          "format": 1,
          "activations": [
            { "productId": "prod_...", "product": "ArtFreq Pro", "key": "8K3QX7M2PDR9T4WNV6ZH",
              "machineId": "<64 hex>", "token": "<64 hex>",
              "license": "AMLIC1|prod_...|8K3QX7M2PDR9T4WNV6ZH|<64 hex>|2026-11-18T09:15:02.417Z",
              "signature": "<base64>", "activatedAt": "2026-11-18T10:15:03.000+01:00",
              "activatedIn": "ArtFreq FX" }
          ]
        }

    At most one activation per product, key and computer: a second key for the same product keeps
    its own entry, so each key's place on the server can still be freed from here. Activations
    for other computers (the file can arrive on a new computer with a migrated or roaming user
    folder) are kept, not used.
    Readers skip entries they can't read and fields they don't know; writers keep both, so a
    later version can add to the format without breaking this one.

    Writes go to a temporary file in the same folder that then replaces the file, so a reader
    sees the old file or the new one, never half of one; a lock (inter-process, plus one inside
    this process) keeps two plug-ins from writing at the same moment. Reads tolerate a missing,
    unreadable, half-written or corrupt file: it just has no activations in it.

    The file proves nothing by itself: an activation counts only if its signed licence checks out
    (SignedLicense). Editing or copying the file can lock a plug-in, never unlock one.
*/
class LicenseStore
{
public:
    /** A store in this folder (created when something is first written). */
    explicit LicenseStore (juce::File directory);

    /** The shared AnyMeans folder for this user (see above). */
    static juce::File defaultDirectory();

    static constexpr const char* fileName = "Licenses.json";

    const juce::File& getDirectory() const noexcept   { return directory; }
    juce::File getFile() const                        { return directory.getChildFile (fileName); }

    //==============================================================================
    struct Contents
    {
        enum class Status
        {
            missing,     // no file: no activations
            ok,          // read (entries it couldn't make sense of are left out)
            unreadable,  // there is a file but it couldn't be opened (locked, no permission)
            corrupt      // not the JSON this expects (half-written, damaged, far too big)
        };

        Status status = Status::missing;
        std::vector<StoredActivation> activations;
    };

    /** Reads the file. Never throws, whatever is in it. */
    Contents read() const;

    /** Adds an activation, replacing the one for the same product, key and computer. False if
        the file couldn't be written (it is then as it was).
    */
    bool put (const StoredActivation& activation) const;

    /** Removes the activation of this product with this key for this computer. True if it is gone
        afterwards (also when there was none).
    */
    bool remove (const juce::String& productId, const juce::String& key, const juce::String& machineId) const;

    //==============================================================================
    /** A cheap fingerprint of the file (whether it exists, its size, its time and its identity),
        to notice another plug-in changing it without reading it. Every write replaces the file,
        which gives it a new identity even when size and time look the same.
    */
    struct Stamp
    {
        bool exists = false;
        juce::int64 size = 0, modified = 0;
        juce::uint64 identity = 0;

        bool operator== (const Stamp& other) const noexcept
        {
            return exists == other.exists && size == other.size && modified == other.modified && identity == other.identity;
        }

        bool operator!= (const Stamp& other) const noexcept    { return ! operator== (other); }
    };

    Stamp getStamp() const;

    //==============================================================================
    /** A random identifier made once and kept in this folder (MachineId.txt), for a computer whose
        system gives no hardware ID at all. It survives updates and reinstalls of the plug-ins, but
        not deleting the folder: the computer then counts as a new one for the server. Empty if it
        can be neither read nor written.
    */
    juce::String readOrCreateFallbackMachineIdentifier() const;

private:
    class WriteLock;

    bool modify (const std::function<void (juce::Array<juce::var>&)>& change) const;

    juce::File directory;
};

} // namespace anymeans::license
