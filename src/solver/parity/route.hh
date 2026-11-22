#pragma once

#include "solver/parity/game.hh"
#include "solver/parity/mealy_reduction.hh"

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
  struct options {
      bool synthesis = true;
      std::string aiger_mode = "isop";
      mealy_reduction reduction = default_mealy_reduction;
      uint64_t deadline_mono_ns = 0;
      bool use_lar = false;
      bool verify = false;
      // Cooperative checks around opaque Spot calls and before publishing a verdict.
      std::function<void ()> check;
      std::function<void (bool)> on_verdict;
      std::function<void ()> on_solved;
      std::function<spot::aig_ptr (spot::aig_ptr)> adapt_circuit;
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
      // The incumbent stays intact; an improvement is exported without rebuilding BDDs.
      std::string reduced_aiger;
  };
  std::optional<unsigned> checked_aiger_size (const std::string& text, const request& input,
                                              uint64_t deadline = 0);
  // A supplied arena requests reselection. All opaque work stays inside the child.
  void reduce_controller (result& incumbent, const request& input, const options& config,
                          uint64_t deadline, const spot::twa_graph_ptr& arena = nullptr);
  result run (request input, options config = {}, observer observe = {});
  // Complete max-odd DPA for the positive objective; bypass translation and classification.
  result run_positive_dpa (request input, options config = {}, observer observe = {});
}
