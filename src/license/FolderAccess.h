#pragma once

#include <juce_core/juce_core.h>

namespace anymeans::license::FolderAccess
{

/** Makes a folder, and every file and folder made in it from then on, readable and writable by
    every account on this computer, so an activation made in one account is seen (and can be
    deactivated) in all of them: the licence server counts computers, not accounts.

    macOS and Linux: the folder's mode becomes rwxrwxrwx (no sticky bit, so any account can
    replace a file another wrote: each write replaces the file whole). Windows: the folder's
    access list gives the Users group Modify, inherited by what is made in it.

    Only the folder's owner (the account that made it, or an administrator) can do this. False if
    it couldn't be done; the folder is then as it was.
*/
bool openToEveryAccount (const juce::File& folder);

/** macOS and Linux: makes a file readable by every account (rw-r--r--), whatever the umask it was
    made with. Windows: nothing to do (a file takes its folder's access list). */
bool makeReadableByEveryAccount (const juce::File& file);

} // namespace anymeans::license::FolderAccess
