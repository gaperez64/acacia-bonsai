#include "solver/symmetry.hh"
#include "tlsf_frontend.hh"

#include <cstdlib>
#include <iostream>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/translate.hh>

namespace utils {
  unsigned verbose = 1;
  voutstream vout;
}

namespace {
  void check (bool condition, const char* message) {
    if (not condition) {
      std::cerr << message << '\n';
      std::exit (1);
    }
  }
}

int main () {
  const auto symmetric = acacia::tlsf_frontend::parse (R"TLSF(
INFO { TITLE: "independent ranges" SEMANTICS: Mealy TARGET: Mealy }
MAIN {
  INPUTS { request[0..2]; shared[0..0]; }
  OUTPUTS { grant[0..2]; }
  GUARANTEES {
    G (shared[0] -> ((request[0] -> X grant[0]) &&
                    (request[1] -> X grant[1]) && (request[2] -> X grant[2])));
  }
}
)TLSF");
  check (symmetric.metadata.tlsf_indexed_families.empty (), "no reduction-syntax hints");
  check (symmetric.metadata.tlsf_symmetries.size () == 1, "nontrivial range reaches the frontend");
  const auto asymmetric = acacia::tlsf_frontend::parse (R"TLSF(
INFO { TITLE: "distinct clients" SEMANTICS: Mealy TARGET: Mealy }
MAIN {
  INPUTS { request[0..2]; shared[0..0]; }
  OUTPUTS { grant[0..2]; }
  GUARANTEES {
    G (shared[0] -> ((request[0] -> X grant[0]) &&
                    (request[1] -> X X grant[1]) && (request[2] -> X X X grant[2])));
  }
}
)TLSF");
  check (asymmetric.metadata.tlsf_symmetries.empty (),
         "asymmetric range supplies no objective generators");
  const auto cyclic = acacia::tlsf_frontend::parse (R"TLSF(
INFO { TITLE: "rotation only" SEMANTICS: Mealy TARGET: Mealy }
MAIN {
  INPUTS { request[0..2]; }
  OUTPUTS { grant[0..2]; }
  GUARANTEES { G ((request[0] -> X grant[1]) &&
                 (request[1] -> X grant[2]) && (request[2] -> X grant[0])); }
}
)TLSF");
  check (cyclic.metadata.tlsf_symmetries.empty (), "cyclic-only groups stay unsupported");

  const auto translate = [] (const auto& spec) {
    spot::translator translator;
    translator.set_type (spot::postprocessor::BA);
    translator.set_pref (spot::postprocessor::Any);
    return translator.run (spot::formula::Not (spot::parse_infix_psl (spec.formula).f));
  };
  const auto partition = [] (const auto& aut, const auto& spec) {
    std::pair<bdd, bdd> cubes {bddtrue, bddtrue};
    for (const auto& name : spec.inputs)
      cubes.first &= bdd_ithvar (aut->register_ap (name));
    for (const auto& name : spec.outputs)
      cubes.second &= bdd_ithvar (aut->register_ap (name));
    return cubes;
  };
  const auto aut = translate (symmetric);
  const auto [inputs, outputs] = partition (aut, symmetric);
  check (symmetry::analyze_indexed_aps (aut, inputs, outputs).indices.size () == 1,
         "unrelated singleton blocks the legacy all-family intersection");
  const auto group = symmetry::detect_objective_generators (aut, inputs, outputs,
                                                            symmetric.metadata.tlsf_symmetries);
  check (group.full_symmetric and group.indices.size () == 3 and group.gens.size () == 2,
         "objective candidates pass automaton verification");
  check (symmetry::detect_objective_generators (aut, outputs, inputs,
                                                symmetric.metadata.tlsf_symmetries)
             .full_symmetric,
         "worker I/O reversal maps candidates using the current partition");

  const auto control = translate (asymmetric);
  const auto [control_inputs, control_outputs] = partition (control, asymmetric);
  check (symmetry::detect_objective_generators (control, control_inputs, control_outputs,
                                                asymmetric.metadata.tlsf_symmetries)
             .empty (),
         "asymmetric control supplies no usable group");
  check (symmetry::detect_objective_generators (control, control_inputs, control_outputs,
                                                symmetric.metadata.tlsf_symmetries)
             .empty (),
         "even objective-verified candidates must pass final automaton verification");
  check (symmetry::detect_objective_generators (aut, inputs, outputs, {}).empty (),
         "absent candidates leave detection alone");
}
