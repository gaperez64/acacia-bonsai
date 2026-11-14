// Build the upstream contract helper without enabling its entire test/tool suite.
// Meson forbids parent targets from listing a nested subproject's source directly.
#include "../subprojects/tlsf-tools/test/unit/native_contract_api.cpp"
