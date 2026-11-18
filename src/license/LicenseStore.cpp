#include "LicenseStore.h"

#include "LicenseConfig.h"
#include "LicenseKey.h"
#include "SafeJson.h"

#include <utility>

#if ! JUCE_WINDOWS
 #include <cerrno>
 #include <fcntl.h>
 #include <sys/file.h>
 #include <unistd.h>
#endif

namespace anymeans::license
{

namespace
{

// Far bigger than any real file (an activation takes about 600 bytes): anything larger is not ours,
// and isn't worth reading into memory on a plug-in's start-up.
constexpr juce::int64 maxFileSize = 1024 * 1024;

const juce::Identifier formatProperty ("format");
const juce::Identifier activationsProperty ("activations");
const juce::Identifier productIdProperty ("productId");
const juce::Identifier productProperty ("product");
const juce::Identifier keyProperty ("key");
const juce::Identifier machineIdProperty ("machineId");
const juce::Identifier tokenProperty ("token");
const juce::Identifier licenseProperty ("license");
const juce::Identifier signatureProperty ("signature");
const juce::Identifier activatedAtProperty ("activatedAt");
const juce::Identifier activatedInProperty ("activatedIn");

juce::String stringProperty (const juce::var& object, const juce::Identifier& name)
{
    const auto value = object.getProperty (name, {});
    return value.isString() ? value.toString() : juce::String();
}

bool isEntryFor (const juce::var& entry, const juce::String& productId, const juce::String& key, const juce::String& machineId)
{
    // Keys compared in their stored form, however the entry spells its key.
    return entry.isObject()
        && stringProperty (entry, productIdProperty) == productId
        && LicenseKey::normalise (stringProperty (entry, keyProperty)) == LicenseKey::normalise (key)
        && stringProperty (entry, machineIdProperty) == machineId;
}

/** Writes text to a temporary file next to the target and swaps it in, checking every step: a
    full disk must leave the old file, not half of a new one.
*/
bool replaceFileAtomically (const juce::File& target, const juce::String& textToWrite)
{
    // The same line endings everywhere (JUCE's JSON writer uses \r\n), so the file reads back
    // exactly as written.
    const auto text = textToWrite.replace ("\r\n", "\n");
    const juce::TemporaryFile temporary (target, juce::TemporaryFile::useHiddenFile);

    {
        juce::FileOutputStream out (temporary.getFile());

        if (! out.openedOk())
            return false;

        if (! out.writeText (text, false, false, nullptr))
            return false;

        out.flush();

        if (out.getStatus().failed())
            return false;
    }

    if (temporary.getFile().loadFileAsString() != text)
        return false;

    return temporary.overwriteTargetFileWithTemporary();
}

} // namespace

//==============================================================================
LicenseStore::FolderLock::FolderLock (const juce::File& folder, int timeoutMs)
   #if JUCE_WINDOWS
    // Named after the folder, so tests working in folders of their own never wait for real plug-ins.
    : mutex ("AnyMeansLicenses-" + juce::String::toHexString (folder.getFullPathName().hashCode64()))
   #endif
{
   #if JUCE_WINDOWS
    locked = mutex.enter (timeoutMs);
   #else
    if (folder.createDirectory().failed())
        return;

    const auto path = folder.getChildFile (lockFileName).getFullPathName();
    fileDescriptor = ::open (path.toRawUTF8(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);

    if (fileDescriptor < 0)
        return;

    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (juce::jmax (0, timeoutMs));

    for (;;)
    {
        if (::flock (fileDescriptor, LOCK_EX | LOCK_NB) == 0)
        {
            locked = true;
            return;
        }

        // Anything but "someone else has it" (no locks on this file system, say): go ahead unlocked.
        if (errno != EWOULDBLOCK && errno != EINTR)
            return;

        if (juce::Time::getMillisecondCounter() >= deadline)
            return;

        juce::Thread::sleep (5);
    }
   #endif
}

LicenseStore::FolderLock::~FolderLock()
{
   #if JUCE_WINDOWS
    if (locked)
        mutex.exit();
   #else
    // The lock file stays: deleting it would let a writer that opened it a moment ago lock a file
    // the next writer can no longer see.
    if (fileDescriptor >= 0)
    {
        if (locked)
            ::flock (fileDescriptor, LOCK_UN);

        ::close (fileDescriptor);
    }
   #endif
}

//==============================================================================
/** Serialises writers: the folder's lock (other plug-in modules in this process, other processes),
    plus a lock inside this module, which keeps its own threads in order even where the file system
    has no locks. If another writer holds the folder for seconds something is wrong there; the write
    then goes ahead rather than hang the plug-in (each write still replaces the file whole).
*/
class LicenseStore::WriteLock
{
public:
    explicit WriteLock (const juce::File& folder)
        : moduleLock (processLock()),
          folderLock (folder, 3000)
    {
    }

private:
    static juce::CriticalSection& processLock()
    {
        static juce::CriticalSection lock;
        return lock;
    }

    const juce::ScopedLock moduleLock;
    const FolderLock folderLock;

    JUCE_DECLARE_NON_COPYABLE (WriteLock)
};

//==============================================================================
LicenseStore::LicenseStore (juce::File directoryToUse)
    : directory (std::move (directoryToUse))
{
}

juce::File LicenseStore::defaultDirectory()
{
    // JUCE's user application data folder is ~/Library on macOS, %APPDATA% on Windows and
    // ~/.config on Linux; macOS keeps application data one level down.
    const auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);

   #if JUCE_MAC
    return base.getChildFile ("Application Support").getChildFile ("AnyMeans");
   #else
    return base.getChildFile ("AnyMeans");
   #endif
}

LicenseStore::Contents LicenseStore::read() const
{
    Contents contents;
    const auto file = getFile();

    if (! file.existsAsFile())
        return contents;

    if (file.getSize() > maxFileSize)
    {
        contents.status = Contents::Status::corrupt;
        return contents;
    }

    juce::String text;

    {
        juce::FileInputStream in (file);

        if (! in.openedOk())
        {
            contents.status = Contents::Status::unreadable;
            return contents;
        }

        text = in.readEntireStreamAsString();
    }

    juce::var root;

    if (parseShallowJson (text, root).failed() || ! root.isObject())
    {
        contents.status = Contents::Status::corrupt;
        return contents;
    }

    contents.status = Contents::Status::ok;

    if (const auto* list = root.getProperty (activationsProperty, {}).getArray())
    {
        for (const auto& entry : *list)
        {
            if (! entry.isObject())
                continue;

            StoredActivation activation;
            activation.productId   = stringProperty (entry, productIdProperty);
            activation.key         = stringProperty (entry, keyProperty);
            activation.machineId   = stringProperty (entry, machineIdProperty);
            activation.token       = stringProperty (entry, tokenProperty);
            activation.license     = stringProperty (entry, licenseProperty);
            activation.signature   = stringProperty (entry, signatureProperty);
            activation.activatedAt = stringProperty (entry, activatedAtProperty);
            activation.activatedIn = stringProperty (entry, activatedInProperty);

            // Without these an entry can't be checked, so it can't count.
            if (activation.productId.isEmpty() || activation.key.isEmpty() || activation.machineId.isEmpty()
                || activation.license.isEmpty() || activation.signature.isEmpty())
                continue;

            contents.activations.push_back (activation);
        }
    }

    return contents;
}

bool LicenseStore::modify (const std::function<void (juce::Array<juce::var>&)>& change) const
{
    const WriteLock lock (directory);

    if (directory.createDirectory().failed())
        return false;

    const auto file = getFile();
    juce::var root;

    if (file.existsAsFile())
    {
        juce::String text;

        if (file.getSize() <= maxFileSize)
        {
            juce::FileInputStream in (file);

            // A file that is there but can't be read might be fine: leave it alone.
            if (! in.openedOk())
                return false;

            text = in.readEntireStreamAsString();
        }

        if (parseShallowJson (text, root).failed() || ! root.isObject())
        {
            // Unusable (it unlocked nothing anyway): keep a copy for support and start afresh.
            file.copyFileTo (file.getSiblingFile ("Licenses.unreadable.json"));
            root = juce::var();
        }
    }

    if (! root.isObject())
        root = juce::var (new juce::DynamicObject());

    auto* object = root.getDynamicObject();

    juce::Array<juce::var> activations;

    if (const auto* existing = object->getProperty (activationsProperty).getArray())
        activations = *existing;

    change (activations);

    if (! object->hasProperty (formatProperty))
        object->setProperty (formatProperty, 1);

    object->setProperty (activationsProperty, activations);

    return replaceFileAtomically (file, juce::JSON::toString (root) + "\n");
}

bool LicenseStore::put (const StoredActivation& activation) const
{
    return modify ([&activation] (juce::Array<juce::var>& activations)
    {
        auto* entry = new juce::DynamicObject();
        const juce::var entryVar (entry);
        const auto* product = findProduct (activation.productId);

        entry->setProperty (productIdProperty, activation.productId);

        if (product != nullptr)
            entry->setProperty (productProperty, juce::String (juce::CharPointer_UTF8 (product->name)));

        entry->setProperty (keyProperty, activation.key);
        entry->setProperty (machineIdProperty, activation.machineId);
        entry->setProperty (tokenProperty, activation.token);
        entry->setProperty (licenseProperty, activation.license);
        entry->setProperty (signatureProperty, activation.signature);
        entry->setProperty (activatedAtProperty, activation.activatedAt);
        entry->setProperty (activatedInProperty, activation.activatedIn);

        for (int i = activations.size(); --i >= 0;)
            if (isEntryFor (activations.getReference (i), activation.productId, activation.key, activation.machineId))
                activations.remove (i);

        activations.add (entryVar);
    });
}

bool LicenseStore::remove (const juce::String& productId, const juce::String& key, const juce::String& machineId) const
{
    const auto contents = read();

    if (contents.status == Contents::Status::missing)
        return true;

    return modify ([&productId, &key, &machineId] (juce::Array<juce::var>& activations)
    {
        for (int i = activations.size(); --i >= 0;)
            if (isEntryFor (activations.getReference (i), productId, key, machineId))
                activations.remove (i);
    });
}

LicenseStore::Stamp LicenseStore::getStamp() const
{
    const auto file = getFile();
    Stamp stamp;
    stamp.exists = file.existsAsFile();

    if (stamp.exists)
    {
        stamp.size = file.getSize();
        stamp.modified = file.getLastModificationTime().toMilliseconds();
        stamp.identity = file.getFileIdentifier();
    }

    return stamp;
}

juce::String LicenseStore::readOrCreateFallbackMachineIdentifier() const
{
    const auto isUsable = [] (const juce::String& id)
    {
        return id.length() == 32 && id.containsOnly ("0123456789abcdef");
    };

    const WriteLock lock (directory);
    const auto file = directory.getChildFile ("MachineId.txt");

    if (const auto existing = file.loadFileAsString().trim(); isUsable (existing))
        return existing;

    if (directory.createDirectory().failed())
        return {};

    // juce::Uuid is random (version 4): 32 hex digits.
    const auto fresh = juce::Uuid().toString().toLowerCase();

    if (! replaceFileAtomically (file, fresh + "\n"))
        return {};

    const auto stored = file.loadFileAsString().trim();
    return isUsable (stored) ? stored : juce::String();
}

} // namespace anymeans::license
