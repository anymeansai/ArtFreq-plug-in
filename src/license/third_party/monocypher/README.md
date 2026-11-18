# Monocypher 4.0.3

`monocypher.c` and `monocypher.h` are Monocypher 4.0.3 (https://monocypher.org/), unmodified: they are
byte for byte the files of the 4.0.3 release (`src/` at tag `4.0.3` of
https://github.com/LoupVaillant/Monocypher, with the version line filled in as the release tarball
has it). `LICENCE.md` is the licence from the same tag: BSD-2-Clause or CC0-1.0, your choice.

They are compiled by `../../Monocypher.cpp`, as C++ inside the namespace `anymeans_monocypher`; do not
add `monocypher.c` to a build on its own. The plug-ins use only `crypto_eddsa_reduce` and
`crypto_eddsa_check_equation`, to verify standard Ed25519 signatures (see `../../Crypto.h`).
