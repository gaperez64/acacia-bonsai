#include "solver/mealy_to_moore.hh"
#include "solver/parity/buchi_gr1.hh"
#include "utils/verbose.hh"

#include <iostream>
#include <random>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/translate.hh>
#include <stdexcept>

unsigned utils::verbose = 0;
utils::voutstream utils::vout;
namespace route = acacia::parity_route;

void require (bool value, const char* message) {
  if (!value)
    throw std::runtime_error (message);
}

void use_gr1 (route::options& options, const route::request& input) {
  options.buchi_route = "gr1";
  options.solve_buchi = [input, synthesis = options.synthesis] (auto aut, bool moore) {
    return route::solve_buchi_gr1 (aut, input.inputs, input.outputs, moore, synthesis);
  };
}

int main () {
  unsigned trial = 0;
  try {
    std::mt19937 random (0x157abc);
    // 240 DBAs in both timing frames, with partial transitions, nonzero initial
    // states, transition/state acceptance, empty partitions and unused signals.
    for (; trial < 240; ++trial) {
      auto aut = spot::make_twa_graph (spot::make_bdd_dict ());
      aut->set_buchi ();
      const auto i = bdd_ithvar (aut->register_ap (spot::formula::ap ("controllable_i")));
      const auto o = bdd_ithvar (aut->register_ap (spot::formula::ap ("O_0")));
      const unsigned n = 1 + random () % 7;
      aut->new_states (n);
      aut->set_init_state (random () % n);
      const bool state_acc = trial % 3 == 0;
      aut->prop_state_acc (state_acc);
      for (unsigned q = 0; q < n; ++q) {
        const bool accepting = random () % 2;
        for (unsigned letter = 0; letter < 4; ++letter) {
          if (trial % 4 == 0 && random () % 4 == 0)
            continue;
          const auto label = (letter & 1 ? i : !i) & (letter & 2 ? o : !o);
          aut->new_edge (q, random () % n, label,
                         (state_acc ? accepting : random () % 2) ? spot::acc_cond::mark_t {0}
                                                                 : spot::acc_cond::mark_t {});
        }
      }
      for (bool moore : {false, true}) {
        route::request input;
        input.automaton = aut;
        input.source = route::source_language::real_worker;
        input.moore = moore;
        input.inputs = trial % 5 == 0 ? std::vector<std::string> {}
                                      : std::vector<std::string> {"controllable_i", "unused_i"};
        input.outputs = trial % 5 == 1 ? std::vector<std::string> {}
                                       : std::vector<std::string> {"O_0", "unused_o"};
        if (trial % 5 == 0)
          input.outputs.push_back ("controllable_i");
        if (trial % 5 == 1)
          input.inputs.push_back ("O_0");
        auto game = route::encode_buchi_gr1 (aut, input.inputs, input.outputs, moore);
        const uint32_t* goal;
        uint32_t count;
        aig_justice_at (game.get (), 0, &goal, &count);
        require (count == 1 && goal[0] == AIG_FALSE && aig_num_fairness (game.get ()) == 1,
                 "encoding changed the co-Buchi GR(1) shape");
        for (unsigned j = 0; j < aig_num_latches (game.get ()); ++j) {
          uint32_t reset;
          aig_latch_at (game.get (), j, nullptr, nullptr, &reset);
          require (reset == 0, "game is not all-zero initialized");
        }
        if (trial == 0) {
          auto invalid = route::encode_buchi_gr1 (aut, input.inputs, input.outputs, moore);
          Gr1SolveOptions options {};
          options.oxidd = oxidd_solve_options_default ();
          options.oxidd.realizability_only = true;  // unsupported by GR(1)
          OxiddFailure failure {};
          options.oxidd.failure = &failure;
          int unreal = 0;
          route::gr1_game strategy (solve_gr1_oxidd (invalid.release (), &unreal, &options),
                                    aig_free);
          require (!strategy && !unreal && failure.kind == OXIDD_FAILURE_CONFIGURATION,
                   "solver error became a losing verdict");
        }
        route::options reference;
        reference.synthesis = false;
        const auto expected = route::run (input, reference);
        require (expected.tier < 4, "random DBA missed Buchi tiers");
        route::options config;
        use_gr1 (config, input);
        const auto actual = route::run (input, config);
        require (actual.verdict == expected.verdict, "GR(1) disagrees with Spot solve_game");
        require (actual.route == "gr1", "random DBA bypassed GR(1)");
        if (actual.verdict == "REALIZABLE") {
          require (bool (actual.circuit), "winning GR(1) strategy missing");
          auto closed = actual.circuit->as_automaton (false);
          auto forbidden = spot::make_twa_graph (closed->get_dict ());
          std::unique_ptr<bddPair, decltype (&bdd_freepair)> rename (bdd_newpair (), bdd_freepair);
          require (bool (rename), "BDD substitution allocation failed");
          for (const auto& ap : aut->ap ())
            bdd_setpair (rename.get (), aut->get_dict ()->var_map.at (ap),
                         forbidden->register_ap (ap));
          forbidden->copy_acceptance_of (aut);
          forbidden->new_states (aut->num_states ());
          forbidden->set_init_state (aut->get_init_state_number ());
          for (const auto& edge : aut->edges ())
            forbidden->new_edge (edge.src, edge.dst, bdd_replace (edge.cond, rename.get ()),
                                 edge.acc);
          require (!closed->intersects (forbidden), "random DBA accepts exported strategy");
          if (moore) {
            std::vector<bool> depends (actual.circuit->max_var () / 2 + 1);
            for (unsigned j = 0; j < actual.circuit->num_inputs (); ++j)
              depends[actual.circuit->input_var (j) / 2] = true;
            for (unsigned j = 0; j < actual.circuit->gates ().size (); ++j) {
              auto [a, b] = actual.circuit->gates ()[j];
              depends[actual.circuit->gate_var (j) / 2] = depends[a / 2] || depends[b / 2];
            }
            for (auto literal : actual.circuit->outputs ())
              require (!depends[literal / 2], "Moore output reads current input");
          }
        }
      }
    }
    // Initial-round, next-round and input-observation obligations distinguish
    // the Moore pipeline from a simple change of predecessor quantifiers.
    for (bool moore : {false, true})
      for (const char* text : {"1", "0", "o", "!o", "X o", "G o", "G(o <-> i)", "G(o <-> X i)",
                               "G(i -> X o)", "FG !o", "GF i"}) {
        route::request input;
        input.formula = spot::parse_infix_psl (text).f;
        input.specification = input.formula;
        input.inputs = {"i", "unused_i"};
        input.outputs = {"o", "unused_o"};
        input.moore = moore;
        route::options reference;
        reference.verify = true;
        const auto expected = route::run (input, reference);
        auto config = reference;
        use_gr1 (config, input);
        const auto actual = route::run (input, config);
        require (actual.verdict == expected.verdict && actual.checked,
                 "timing fixture or fresh negated-spec product failed");
      }
    std::cout << "480 random DBA games agree with Spot; controllers and timing fixtures pass\n";
  } catch (const std::exception& error) {
    std::cerr << "trial=" << trial << ": " << error.what () << '\n';
    return 1;
  }
}
