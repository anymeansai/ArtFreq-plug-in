#pragma once

// The vendored Monocypher (third_party/monocypher/), seen from C++. Monocypher.cpp compiles its C
// source as C++ inside this namespace (Monocypher supports that through MONOCYPHER_CPP_NAMESPACE),
// so none of its symbols can clash with another copy of Monocypher that a host, or another
// plug-in, loads into the same process. Include this header, never monocypher.h directly: the
// namespace has to be the same on both sides.

#define MONOCYPHER_CPP_NAMESPACE anymeans_monocypher
#include "third_party/monocypher/monocypher.h"
