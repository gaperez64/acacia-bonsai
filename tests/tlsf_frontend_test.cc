#include "tlsf_frontend.hh"
#include "arg_parser.hh"

#include "solver/restriction_prepass.hh"
#include "solver/symmetry.hh"

#include <algorithm>
#include <bddx.h>
#include <iostream>
#include <spot/tl/parse.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/translate.hh>
#include <stdexcept>
#include <string>

namespace {

  bool expect (const std::string& name, bool condition) {
    if (condition)
      return true;
    std::cerr << "failed: " << name << '\n';
    return false;
  }

  bool expect_family_views (const acacia::tlsf_frontend::specification& spec, bool indexed) {
    const auto& declared = spec.metadata.tlsf_declared_families;
    const auto hints = spec.metadata.tlsf_indexed_families ();
    bool ok = expect ("syntax-gated count/equivariant view",
                      spec.metadata.tlsf_has_indexed_conjunction == indexed &&
                          hints.size () == (indexed ? declared.size () : 0) &&
                          (!indexed || hints.data () == declared.data ()));
    ok &= expect ("both physical declarations retained", declared.size () == 2);
    if (declared.size () != 2)
      return false;
    for (size_t i = 0; i < declared.size (); ++i) {
      const auto& family = declared[i];
      ok &= expect ("physical element family",
                    family.is_input == (i == 0) && family.lo == 0 && family.hi == 2 &&
                        family.members == (i == 0 ? spec.inputs : spec.outputs) &&
                        family.encoding == symmetry::declared_family::kind::element);
    }
    const auto restrictions = acacia::restriction::candidates (spec.metadata, spec.outputs, true);
    ok &= expect ("prepass receives all declared input members",
                  restrictions.size () == 1 && restrictions[0].values.size () == 3);
    if (restrictions.size () == 1)
      for (const auto& member : spec.inputs) {
        const auto found = restrictions[0].values.find (member);
        ok &= expect ("prepass preserves physical representative",
                      found != restrictions[0].values.end () &&
                          found->second == spot::formula::ap (spec.inputs.front ()));
      }
    ok &= expect ("prepass excludes declared output members from scalar restriction",
                  acacia::restriction::candidates (spec.metadata, spec.outputs, false).empty ());
    return ok;
  }

}  // namespace

int main (int, char** argv) {
  const auto trivial = acacia::tlsf_frontend::parse (R"TLSF(
INFO { TITLE: "no precheck" SEMANTICS: Mealy TARGET: Mealy }
MAIN { INPUTS { i; } OUTPUTS { o; } GUARANTEE { G o; } }
)TLSF");
  if (not expect ("conversion without an OxiDD manager", trivial.formula == "G o"))
    return 1;

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
  ok &= expect ("formula uses effective Mealy target", parsed.formula == "G (req -> grant)");
  ok &= expect ("source format", parsed.metadata.source_format == "tlsf");
  ok &= expect ("original semantics", parsed.metadata.tlsf_semantics == "Mealy");
  ok &= expect ("original target", parsed.metadata.tlsf_target == "Moore");
  ok &= expect ("effective target", parsed.metadata.tlsf_effective_target == "Mealy");

  arg_parse_result args;
  args.tlsf_source = spec;
  args.tlsf_specified = true;
  args.formula_specified = true;
  args.objective.formula = "!grant"; // A worker-transformed formula is not reusable.
  acacia::tlsf_frontend::specification child_local;
  const auto& prepared = args.prepare_objective (child_local);
  ok &= expect ("formula_specified does not identify preparation",
                &prepared == &child_local && prepared.formula == parsed.formula &&
                    prepared.inputs == parsed.inputs && prepared.outputs == parsed.outputs);
  args.objective = std::move (child_local);
  args.original_tlsf_prepared = true;
  const auto& inherited = args.prepare_objective (child_local);
  ok &= expect ("successful original objective is borrowed with source metadata",
                &inherited == &args.objective && inherited.metadata.tlsf_target == "Moore" &&
                    inherited.inputs == parsed.inputs && inherited.outputs == parsed.outputs &&
                    inherited.metadata.tlsf_effective_target == "Mealy" &&
                    inherited.metadata.tlsf_semantics == "Mealy" &&
                    inherited.metadata.tlsf_gr_level == parsed.metadata.tlsf_gr_level &&
                    inherited.metadata.tlsf_normalized_objective == parsed.formula &&
                    args.tlsf_source == spec);

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

  ok &= expect ("exact normalization retained",
                strict.metadata.tlsf_normalized_objective == strict.formula);

  bool finite_rejected = false;
  try {
    (void) acacia::tlsf_frontend::parse (R"TLSF(
INFO { TITLE: "finite" SEMANTICS: Finite,Mealy TARGET: Mealy }
MAIN { INPUTS { a; } OUTPUTS { b; } GUARANTEE { a -> b; } }
)TLSF");
  } catch (const std::runtime_error&) {
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
  ok &= expect_family_views (indexed, true);

  bool malformed_hint_rejected = false;
  try {
    auto malformed_hints = indexed.metadata.tlsf_declared_families;
    malformed_hints.front ().members[1] = "req_9";
    auto aut = spot::make_twa_graph (spot::make_bdd_dict ());
    aut->new_states (1);
    const int req_0 = aut->register_ap ("req_0");
    (void) symmetry::analyze_indexed_aps (
        aut, bdd_ithvar (req_0), bddtrue, malformed_hints);
  }
  catch (const std::runtime_error&) {
    malformed_hint_rejected = true;
  }
  ok &= expect ("malformed indexed-family hints become diagnostics",
                malformed_hint_rejected);

  const auto asymmetric_bus = acacia::tlsf_frontend::parse (R"TLSF(
INFO { TITLE: "asymmetric bus" SEMANTICS: Mealy TARGET: Mealy }
GLOBAL { PARAMETERS { n = 3; } }
MAIN {
  INPUTS { Req[n]; }
  OUTPUTS { Grant[n]; }
  GUARANTEE { G (Req[0] -> Grant[0]); }
}
)TLSF");
  ok &= expect_family_views (asymmetric_bus, false);
  const auto encoded = acacia::tlsf_frontend::load (argv[1]);
  ok &= expect ("encoded declaration stays three physical bits for five clients",
                encoded.inputs == std::vector<std::string> ({"request_0", "request_1", "request_2"}) &&
                    encoded.outputs == std::vector<std::string> ({"grant_0", "grant_1", "grant_2"}));
  ok &= expect_family_views (encoded, true);
  {
    arg_parse_result count;
    std::ifstream fixture (argv[1]);
    count.tlsf_source.assign (std::istreambuf_iterator<char> (fixture), {});
    count.tlsf_specified = true;
    acacia::tlsf_frontend::captured_specification local;
    const auto& captured = count.prepare_count_objective (local);
    const auto* origins = local.ast.value_origins ();
    ok &= expect ("count worker captures equivalent conversion",
                  &captured == &local.objective && origins && origins->complete &&
                      captured.formula == encoded.formula && captured.inputs == encoded.inputs &&
                      captured.outputs == encoded.outputs);
    ok &= expect_family_views (captured, true);
    const auto* bus = origins ? origins->buses : nullptr;
    ok &= expect ("count worker owns the encoded bus record",
                  bus && bus->proved_bit_width && bus->bit_count == encoded.inputs.size () &&
                      bus->parameter_count == 1 && bus->parameter_ids[0] == 1 && bus->next &&
                      bus->next->is_output);
    count.objective = encoded;
    count.original_tlsf_prepared = true;
    acacia::tlsf_frontend::captured_specification mixed_local;
    const auto& borrowed = count.prepare_count_objective (mixed_local);
    ok &= expect (
        "count worker borrows plain parent conversion",
        &borrowed == &count.objective && borrowed.ast == encoded.ast && !mixed_local.ast.get ());
  }

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
