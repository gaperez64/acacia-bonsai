#pragma once

#include "solver/parity/game.hh"

#include <functional>
#include <memory>
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
  struct options {
      bool synthesis = true;
      bool use_lar = false;
      bool verify = false;
      bool fastpath_phases = false; // opt-in completion/split/solve timings
      // Cooperative checks around opaque Spot calls and before publishing a verdict.
      std::function<void ()> check;
      std::function<void (bool)> on_verdict;
      std::function<void ()> on_solved;
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
  // Complete max-odd DPA for the positive objective; bypass translation and classification.
  result run_positive_dpa (request input, options config = {}, observer observe = {});
  // Classify a negative copy of the same positive DPA; retain the original
  // positive graph for genuine parity fallback. No reconstruction on tier4.
  result run_classified_positive_dpa (request input, options config = {}, observer observe = {});
}
