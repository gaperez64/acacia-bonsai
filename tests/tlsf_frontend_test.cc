#include "tlsf_frontend.hh"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

  bool expect (const std::string& name, bool condition) {
    if (condition)
      return true;
    std::cerr << "failed: " << name << '\n';
    return false;
  }

}  // namespace

int main () {
  const std::string spec = R"TLSF(
INFO {
  TITLE: "native frontend"
  SEMANTICS: Mealy
  TARGET: Moore
}
MAIN {
  INPUTS { Req; }
  OUTPUTS { Grant; }
  GUARANTEE { G (Req -> Grant); }
}
)TLSF";

  const auto parsed = acacia::tlsf_frontend::parse (spec);
  bool ok = true;
  ok &= expect ("formula emitted", not parsed.formula.empty ());
  ok &= expect ("input lowercase convention", parsed.inputs == std::vector<std::string> {"req"});
  ok &= expect ("output lowercase convention", parsed.outputs == std::vector<std::string> {"grant"});
  ok &= expect ("target adaptation follows TLSF", parsed.formula == "G (req -> X grant)");
  ok &= expect ("source format", parsed.metadata.source_format == "tlsf");
  ok &= expect ("original semantics", parsed.metadata.tlsf_semantics == "Mealy");
  ok &= expect ("original target", parsed.metadata.tlsf_target == "Moore");
  ok &= expect ("effective target", parsed.metadata.tlsf_effective_target == "Mealy");

  const auto strict = acacia::tlsf_frontend::parse (R"TLSF(
INFO {
  TITLE: "strict semantics"
  SEMANTICS: Mealy,Strict
  TARGET: Mealy
}
MAIN {
  INPUTS { a; b; c; }
  OUTPUTS { x; y; z; }
  INITIALLY { a; }
  PRESET { x; }
  REQUIRE { b; }
  ASSERT { y; }
  ASSUME { c; }
  GUARANTEE { z; }
}
)TLSF");
  ok &= expect ("strict sections follow TLSF weak-until semantics",
                strict.formula == "a -> x && y W !b && (G b && c -> z)");
  ok &= expect ("strict metadata", strict.metadata.tlsf_semantics == "Strict,Mealy");

  bool finite_rejected = false;
  try {
    (void) acacia::tlsf_frontend::parse (R"TLSF(
INFO { TITLE: "finite" SEMANTICS: Finite,Mealy TARGET: Mealy }
MAIN { INPUTS { a; } OUTPUTS { b; } GUARANTEE { a -> b; } }
)TLSF");
  }
  catch (const std::runtime_error&) {
    finite_rejected = true;
  }
  ok &= expect ("finite semantics are rejected", finite_rejected);

  const auto indexed = acacia::tlsf_frontend::parse (R"TLSF(
INFO {
  TITLE: "indexed syntax evidence"
  SEMANTICS: Mealy
  TARGET: Mealy
}
GLOBAL { PARAMETERS { n = 3; } }
MAIN {
  INPUTS { Req[n]; }
  OUTPUTS { Grant[n]; }
  GUARANTEE { &&[0 <= i < n] G (Req[i] -> Grant[i]); }
}
)TLSF");
  ok &=
      expect ("indexed input/output expansion",
              indexed.inputs == std::vector<std::string> ({"req_0", "req_1", "req_2"}) and
                  indexed.outputs == std::vector<std::string> ({"grant_0", "grant_1", "grant_2"}));
  ok &= expect ("indexed conjunction provenance hints",
                indexed.metadata.tlsf_indexed_families.size () == 2);
  if (indexed.metadata.tlsf_indexed_families.size () == 2) {
    const auto& input_family = indexed.metadata.tlsf_indexed_families[0];
    const auto& output_family = indexed.metadata.tlsf_indexed_families[1];
    ok &= expect (
        "indexed input hint",
        input_family.is_input and input_family.lo == 0 and input_family.hi == 2 and
            input_family.members == std::vector<std::string> ({"req_0", "req_1", "req_2"}));
    ok &= expect (
        "indexed output hint",
        not output_family.is_input and output_family.lo == 0 and output_family.hi == 2 and
            output_family.members == std::vector<std::string> ({"grant_0", "grant_1", "grant_2"}));
  }

  const auto asymmetric_bus = acacia::tlsf_frontend::parse (R"TLSF(
INFO { TITLE: "asymmetric bus" SEMANTICS: Mealy TARGET: Mealy }
GLOBAL { PARAMETERS { n = 3; } }
MAIN {
  INPUTS { Req[n]; }
  OUTPUTS { Grant[n]; }
  GUARANTEE { G (Req[0] -> Grant[0]); }
}
)TLSF");
  ok &= expect ("bus declarations alone do not create symmetry hints",
                asymmetric_bus.metadata.tlsf_indexed_families.empty ());

  const auto mixed = acacia::tlsf_frontend::parse (R"TLSF(
INFO {
  TITLE: "mixed scalar and bus order"
  DESCRIPTION: "test"
  SEMANTICS: Mealy
  TARGET: Mealy
}
GLOBAL { PARAMETERS { n = 2; } }
MAIN {
  INPUTS { Ready; Req[n]; }
  OUTPUTS { Grant[n]; Done; }
  GUARANTEE { G (Ready -> (Req[0] -> Grant[0])); }
}
)TLSF");
  ok &= expect ("mixed bus sections preserve stable serialization order",
                mixed.inputs == std::vector<std::string> ({"ready", "req_0", "req_1"}) and
                    mixed.outputs == std::vector<std::string> ({"done", "grant_0", "grant_1"}));

  const auto enum_typed = acacia::tlsf_frontend::parse (R"TLSF(
INFO { TITLE: "enum order" SEMANTICS: Mealy TARGET: Mealy }
GLOBAL {
  DEFINITIONS { enum Mode = Idle: 00 Active: 01 Done: 10; }
}
MAIN {
  INPUTS { Req[2]; Lock[2]; Ready; Mode State; }
  OUTPUTS { ok; }
  GUARANTEE { G ok; }
}
)TLSF");
  ok &= expect (
      "enum buses follow ordinary buses in stable serialization order",
      enum_typed.inputs ==
          std::vector<std::string> ({"ready", "lock_0", "lock_1", "req_0", "req_1",
                                     "state_0", "state_1"}));
  ok &= expect (
      "enum validity follows TLSF invariant semantics",
      enum_typed.formula.find (
          "G (!state_0 && !state_1 || !state_0 && state_1 || state_0 && !state_1)") !=
          std::string::npos);

  const auto strict_enum = acacia::tlsf_frontend::parse (R"TLSF(
INFO { TITLE: "strict enum" SEMANTICS: Strict,Mealy TARGET: Mealy }
GLOBAL {
  DEFINITIONS {
    enum InputMode = Active: 10,11;
    enum OutputMode = Ready: 0*;
  }
}
MAIN {
  INPUTS { InputMode Request; }
  OUTPUTS { OutputMode Response; ok; }
  GUARANTEE { G ok; }
}
)TLSF");
  ok &= expect (
      "strict enum roles, wildcard, and multiple valuations follow TLSF",
      strict_enum.formula ==
          "!response_0 W !(request_0 && !request_1 || request_0 && request_1) && (G "
          "(request_0 && !request_1 || request_0 && request_1) -> G ok)");
  return ok ? 0 : 1;
}
