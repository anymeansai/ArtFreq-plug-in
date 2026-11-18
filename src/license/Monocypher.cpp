// Compiles the vendored Monocypher 4.0.3 (third_party/monocypher/monocypher.c, kept exactly as
// released) as C++, inside its own namespace (see Monocypher.h). Building it this way needs no C
// compiler settings and no per-file build flags on any platform.
//
// It is third-party code, kept byte for byte so it can be compared with the release, so its
// warnings are turned off here, for this file only. With GCC and Clang the settings are left on
// to the end of the file, as some warnings (unused functions, say) are only reported once the
// whole file has been read; nothing of ours follows the include. MSVC gets a matching pop, so it
// has no unbalanced push to report.

#if defined (_MSC_VER) && ! defined (__clang__)
 #pragma warning (push, 0)
#elif defined (__clang__)
 #pragma clang diagnostic ignored "-Weverything"
#elif defined (__GNUC__)
 #pragma GCC diagnostic ignored "-Wpedantic"
 #pragma GCC diagnostic ignored "-Wconversion"
 #pragma GCC diagnostic ignored "-Wsign-conversion"
 #pragma GCC diagnostic ignored "-Wsign-compare"
 #pragma GCC diagnostic ignored "-Wshadow"
 #pragma GCC diagnostic ignored "-Wcast-align"
 #pragma GCC diagnostic ignored "-Wunused-parameter"
 #pragma GCC diagnostic ignored "-Wunused-function"
 #pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
 #pragma GCC diagnostic ignored "-Wold-style-cast"
 #pragma GCC diagnostic ignored "-Wuseless-cast"
#endif

#include "Monocypher.h"
#include "third_party/monocypher/monocypher.c"

#if defined (_MSC_VER) && ! defined (__clang__)
 #pragma warning (pop)
#endif
