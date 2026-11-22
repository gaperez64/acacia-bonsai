#include <unordered_map>

#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <set>
#include <spot/tl/parse.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/aiger.hh>
#include <spot/twaalgos/translate.hh>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tlsf/convert.hpp>
#include <vector>

namespace {
  struct unsupported : std::runtime_error {
      using std::runtime_error::runtime_error;
  };

  struct rejected : std::runtime_error {
      using std::runtime_error::runtime_error;
  };

  bool same_signals (const std::vector<std::string>& actual,
                     const std::vector<std::string>& expected) {
    return actual.size () == expected.size () &&
           std::set<std::string> (actual.begin (), actual.end ()) ==
               std::set<std::string> (expected.begin (), expected.end ());
  }

  spot::twa_graph_ptr reachable_behavior (const spot::aig& circuit, const spot::bdd_dict_ptr& dict,
                                          bool moore, unsigned max_states) {
    auto behavior = spot::make_twa_graph (dict);
    bdd inputs = bddtrue;
    for (const auto& name : circuit.input_names ())
      inputs &= bdd_ithvar (behavior->register_ap (name));
    std::vector<bdd> outputs;
    for (const auto& name : circuit.output_names ())
      outputs.push_back (bdd_ithvar (behavior->register_ap (name)));

    // Spot's as_automaton uses an unsigned latch bitmask; keys here retain every latch.
    std::vector<std::string> states (1, std::string (circuit.num_latches (), '\0'));
    std::unordered_map<std::string, unsigned> ids {{states[0], behavior->new_state ()}};
    behavior->set_init_state (0);
    for (unsigned src = 0; src < states.size (); ++src) {
      bdd latch_values = bddtrue;
      for (unsigned n = 0; n < circuit.num_latches (); ++n)
        latch_values &= circuit.latch_bdd (n, !states[src][n]);

      bdd letters = bddtrue;
      for (unsigned n = 0; n < outputs.size (); ++n) {
        const auto function = bdd_restrict (circuit.aigvar2bdd (circuit.output (n)), latch_values);
        if (moore && function != bddfalse && function != bddtrue)
          throw rejected ("Moore output depends on current input in a reachable state");
        letters &= bdd_biimp (outputs[n], function);
      }
      std::vector<bdd> next;
      for (auto literal : circuit.next_latches ())
        next.push_back (bdd_restrict (circuit.aigvar2bdd (literal), latch_values));

      // Group all input valuations with the same successor into one symbolic edge.
      bdd remaining = bddtrue;
      while (remaining != bddfalse) {
        const auto sample = bdd_satoneset (remaining, inputs, bddfalse);
        std::string destination;
        bdd condition = bddtrue;
        for (const auto& function : next) {
          const bool value = (sample & function) != bddfalse;
          destination.push_back (value);
          condition &= value ? function : !function;
        }
        auto found = ids.find (destination);
        if (found == ids.end ()) {
          if (states.size () >= max_states)
            throw unsupported ("reachable-state budget exceeded");
          const auto dst = behavior->new_state ();
          found = ids.emplace (destination, dst).first;
          states.push_back (destination);
        }
        behavior->new_edge (src, found->second, condition & letters);
        remaining -= condition;
      }
    }
    return behavior;
  }
}

int main (int argc, char** argv) {
  if (argc != 3 && !(argc == 5 && std::string (argv[3]) == "--max-states")) {
    std::cerr << "usage: controller-check SPEC.tlsf CONTROLLER.aag [--max-states N]\n";
    return 2;
  }
  try {
    unsigned max_states = 200000;
    if (argc == 5) {
      const std::string value = argv[4];
      const auto [end, error] =
          std::from_chars (value.data (), value.data () + value.size (), max_states);
      if (error != std::errc () || end != value.data () + value.size () || !max_states)
        throw std::runtime_error ("max-states must be a positive unsigned integer");
    }
    std::ifstream source (argv[1]);
    if (!source)
      throw std::runtime_error ("cannot open specification");
    std::ostringstream text;
    text << source.rdbuf ();
    // Retain TARGET: the emitted circuit has already undone Mealy lowering.
    const auto spec = tlsf::convert (text.str (), nullptr);
    auto parsed = spot::parse_infix_psl (spec.preprocessed_ltl);
    if (!parsed.f || !parsed.errors.empty ())
      throw std::runtime_error ("cannot parse specification objective");

    std::ifstream circuit_file (argv[2]);
    std::string header, kind, extra;
    unsigned m, i, l, o, a;
    std::getline (circuit_file, header);
    std::istringstream fields (header);
    if (!(fields >> kind >> m >> i >> l >> o >> a) || kind != "aag" || (fields >> extra))
      throw std::runtime_error ("expected basic ASCII AIGER header");
    if (uint64_t (m) != uint64_t (i) + l + a || i != spec.inputs.size () ||
        o != spec.outputs.size ())
      throw std::runtime_error ("invalid circuit dimensions");
    for (uint64_t n = 0; n < uint64_t (i) + l; ++n) {
      std::string line;
      std::getline (circuit_file, line);
      std::istringstream row (line);
      unsigned current, next;
      if (n >= i && (row >> current >> next >> extra))
        throw unsupported ("explicit latch reset fields are unsupported by Spot's parser");
    }
    circuit_file.clear ();
    circuit_file.seekg (0);
    const auto dict = spot::make_bdd_dict ();
    const auto circuit = spot::aig::parse_aag (circuit_file, argv[2], dict);
    if (!same_signals (circuit->input_names (), spec.inputs) ||
        !same_signals (circuit->output_names (), spec.outputs))
      throw std::runtime_error ("circuit input/output partition differs from specification");

    const auto behavior =
        reachable_behavior (*circuit, dict, spec.effective_target == "Moore", max_states);
    spot::translator translator (behavior->get_dict ());
    translator.set_type (spot::postprocessor::BA);
    const auto violation = translator.run (spot::formula::Not (parsed.f));
    if (violation->intersects (behavior)) {
      std::cout << "failed_verification\n";
      return 1;
    }
    std::cout << "verified\n";
    return 0;
  } catch (const unsupported& error) {
    std::cerr << error.what () << '\n';
    std::cout << "verify_unsupported\n";
    return 3;
  } catch (const rejected& error) {
    std::cerr << error.what () << '\n';
    std::cout << "failed_verification\n";
    return 1;
  } catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 2;
  }
}
