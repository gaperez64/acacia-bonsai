#pragma once

#include "solver/parity/game.hh"

#include <functional>
#include <memory>
#include <optional>
#include <spot/twaalgos/aiger.hh>
#include <string>
#include <vector>

namespace acacia::parity_route {
  enum class source_language { original, real_worker };
  enum class timing_frame { original, effective_mealy };
  struct request {
      spot::formula formula;
      spot::formula specification;
      spot::twa_graph_ptr automaton;
      std::vector<std::string> inputs, outputs;
      source_language source = source_language::original;
      timing_frame frame = timing_frame::original;
      bool moore = false;
  };
  struct buchi_solution {
      std::optional<bool> real;
      parity::statistics stats;
      std::function<spot::twa_graph_ptr ()> controller;
  };
  struct options {
      bool synthesis = true;
      bool use_spot = false;
      bool use_lar = false;
      bool verify = false;
      bool cross_check = false;
      parity::limits bounds;
      // Tiers 1-3 otherwise keep the deterministic forbidden-Buchi Spot shortcut.
      bool buchi_strpm = false;
      // A supplied antichain solver takes precedence over buchi_strpm.
      std::function<buchi_solution (spot::twa_graph_ptr, bool)> solve_buchi;
      std::function<void (bool)> on_verdict;
      std::function<void (const parity::statistics&)> on_solved;
      std::function<void (unsigned, const std::string&)> on_classified;
      std::function<void (const parity::arena_game&)> on_game;
      std::function<void (const parity::arena_game&, const parity::solution&)> on_solution;
  };
  struct classification {
      unsigned tier = 0;
      std::string reason;
      spot::twa_graph_ptr buchi;
  };
  classification classify (spot::twa_graph_ptr worker);

  // Seconds < 0 is stage entry; completed stages publish size and elapsed time.
  using observer = std::function<void (const char*, const spot::twa_graph_ptr&, double)>;
  struct result {
      unsigned tier = 0;
      std::string tier_reason, route, verdict = "UNKNOWN", verification = "none";
      bool checked = false;
      parity::solution solution;
      std::unique_ptr<parity::arena_game> game;
      spot::twa_graph_ptr controller;
      spot::aig_ptr circuit;
  };
  result run (request input, options config = {}, observer observe = {});
}
