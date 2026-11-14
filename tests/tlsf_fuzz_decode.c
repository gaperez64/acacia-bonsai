// Share the upstream byte grammar without enabling its entire test/tool suite.
// Meson forbids parent targets from listing a nested subproject's source directly.
#include "../subprojects/tlsf-tools/test/fuzz/decode.c"
