#pragma once

// Shared P3/P4 fixed-seed Spot game generator; preserve P3's draw order.
#include "solver/spot_letter_oracle.hh"
#include <random>

namespace acacia::testing::spot_games {
  using namespace acacia::spot_letters;
  inline constexpr std::uint32_t seed = 0x50334bdd;
  inline std::vector<bdd> letters (const WorkerAlphabet& alphabet, Variables which) {
    const bdd vars = which == Variables::all ? alphabet.ap_vars
                     : which == Variables::inputs ? alphabet.inputs : alphabet.outputs;
    std::vector<bdd> result {bddtrue};  // zero APs still have ONE total valuation
    for (const int var : alphabet.order) {
      if (bdd_exist (vars, bdd_ithvar (var)) == vars) continue;
      std::vector<bdd> next;
      for (const auto& prefix : result) {
        next.push_back (prefix & bdd_nithvar (var));
        next.push_back (prefix & bdd_ithvar (var));
      }
      result = std::move (next);
    }
    return result;
  }
  inline spot::twa_graph_ptr graph (unsigned n, bool frozen) {
    auto result = spot::make_twa_graph (spot::make_bdd_dict ());
    result->set_buchi ();
    result->prop_state_acc (frozen);
    result->new_states (n);
    result->set_init_state (0);
    return result;
  }
  inline WorkerAlphabet alphabet (const spot::twa_graph_ptr& graph, bool input, bool output) {
    WorkerAlphabet result {bddtrue, bddtrue, bddtrue, {}};
    if (input) {
      const int var = graph->register_ap ("u");
      result.inputs = bdd_ithvar (var);
      result.order.push_back (var);
    }
    if (output) {
      const int var = graph->register_ap ("c");
      result.outputs = bdd_ithvar (var);
      result.order.push_back (var);
    }
    result.ap_vars = graph->ap_vars ();
    return result;
  }

  struct TinySpotGame {
      spot::twa_graph_ptr graph;
      WorkerAlphabet alphabet;
      std::int32_t K;
      std::size_t bool_threshold;
  };
  inline TinySpotGame random_game (std::mt19937& rng, unsigned game, bool frozen) {
    const unsigned n = 1 + rng () % 3;
    const std::int32_t K = 1 + rng () % 2;
    const std::size_t bool_threshold = frozen ? rng () % (n + 1) : n;
    auto g = graph (n, frozen);
    const auto ap = alphabet (g, game % 4 < 2, game % 3 != 0);
    const auto valuations = letters (ap, Variables::all);
    for (unsigned p = 0; p < n; ++p) {
      const bool source_accepting = rng () % 2;
      const unsigned count = rng () % (2 * n + 1);
      for (unsigned i = 0; i < count; ++i) {
        bdd guard = bddfalse;
        for (const auto& letter : valuations) if (rng () % 2) guard |= letter;
        const bool accepting = frozen ? source_accepting : rng () % 2;
        g->new_edge (p, rng () % n, guard,
                     accepting ? spot::acc_cond::mark_t {0} : spot::acc_cond::mark_t {});
      }
    }
    return {g, ap, K, bool_threshold};
  }
}
