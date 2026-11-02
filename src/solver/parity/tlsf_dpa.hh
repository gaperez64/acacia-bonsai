#pragma once

#include <map>
#include <functional>
#include <memory>
#include <spot/twa/twagraph.hh>
#include <string>
#include <tlsf/dpa.h>
#include <vector>

namespace acacia::tlsf_dpa {
  using context = std::unique_ptr<TlsfDpa, decltype (&tlsf_dpa_free)>;
  struct limits {
      // Experimental ceilings: 100k states, 256 MiB C storage, 1M OxiDD nodes,
      // 256k cache entries, and 256 MiB importer storage. Worker RSS/AS limits
      // also cover the two BDD managers and opaque library allocations.
      TlsfDpaBudget construction {100000, 256u << 20, 1u << 20, 1u << 18};
      size_t import_bytes = 256u << 20;
  };
  struct result {
      spot::twa_graph_ptr automaton;
      std::map<TlsfDpaState, unsigned> states;
      size_t edges = 0, labels = 0, import_bytes = 0;
      TlsfDpaStatus status {TLSF_DPA_OK, TLSF_DPA_NONE};
      const char* stage = "create";
  };
  context create (const TlsfAstNode* root, const std::vector<std::string>& inputs,
                  const std::vector<std::string>& outputs, const limits& budget,
                  TlsfDpaStatus& status);
  // No graph escapes unless every reachable row and label was imported.
  result import (TlsfDpa& source, const limits& budget,
                 spot::bdd_dict_ptr dictionary = spot::make_bdd_dict ());
  struct lazy_result {
      bool decided = false;
      size_t states = 0, expanded = 0, edges = 0, labels = 0, import_bytes = 0, solves = 0;
      TlsfDpaStatus status {TLSF_DPA_OK, TLSF_DPA_NONE};
      const char* stage = "import";
  };
  // Decision only: true targets REAL, false targets UNREAL. A false decided
  // flag is always inconclusive, including a complete game won by the other side.
  lazy_result solve_lazy (TlsfDpa& source, const limits& budget,
                          const std::vector<std::string>& outputs, bool target_real,
                          std::function<void (const lazy_result&)> on_snapshot = {});
  const char* reason (TlsfDpaStatus status);
}
