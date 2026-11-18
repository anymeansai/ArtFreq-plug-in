#include "FolderAccess.h"

// Kept apart from the rest of the licence code: <windows.h> defines macros (DELETE, ERROR, min,
// max...) that are best confined to the one file that needs its access-list functions.
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <aclapi.h>
 #include <string>

 #if JUCE_MSVC
  #pragma comment (lib, "advapi32.lib")
 #endif
#else
 #include <sys/stat.h>
#endif

namespace anymeans::license::FolderAccess
{

bool openToEveryAccount (const juce::File& folder)
{
    if (! folder.isDirectory())
        return false;

   #if JUCE_WINDOWS
    // BUILTIN\Users, by its well-known SID: the group's name is translated on other languages'
    // systems. Every account that can log on to the computer is in it.
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    PSID users = nullptr;

    if (! AllocateAndInitializeSid (&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_USERS,
                                    0, 0, 0, 0, 0, 0, &users))
        return false;

    // Modify (read, write, create and delete), for the folder and, inherited, for every file and
    // folder made in it. Delete is what lets one account replace a file another account wrote.
    EXPLICIT_ACCESS_W access {};
    access.grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE | DELETE;
    access.grfAccessMode = GRANT_ACCESS;
    access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    access.Trustee.ptstrName = static_cast<LPWSTR> (users);

    // Added to the folder's list as it is (what it inherits from ProgramData stays).
    std::wstring path (folder.getFullPathName().toWideCharPointer());
    PACL current = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    auto done = false;

    if (GetNamedSecurityInfoW (path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                               nullptr, nullptr, &current, nullptr, &descriptor) == ERROR_SUCCESS)
    {
        PACL updated = nullptr;

        if (SetEntriesInAclW (1, &access, current, &updated) == ERROR_SUCCESS)
        {
            done = SetNamedSecurityInfoW (path.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                          nullptr, nullptr, updated, nullptr) == ERROR_SUCCESS;
            LocalFree (updated);
        }

        LocalFree (descriptor);
    }

    FreeSid (users);
    return done;
   #else
    // The umask took some of mkdir's rwxrwxrwx away; give it back.
    return ::chmod (folder.getFullPathName().toRawUTF8(), 0777) == 0;
   #endif
}

bool makeReadableByEveryAccount (const juce::File& file)
{
   #if JUCE_WINDOWS
    return file.existsAsFile();
   #else
    return ::chmod (file.getFullPathName().toRawUTF8(), 0644) == 0;
   #endif
}

} // namespace anymeans::license::FolderAccess
