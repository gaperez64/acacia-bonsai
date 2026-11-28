#pragma once

// Count-quotiented games for specs symmetric in one index family (#454).
//
// The formula must split, after pushing G over conjunctions and implication
// consequents, into
//   - index-free conjuncts,
//   - local conjuncts: one template, over the index-free APs and index 0's
//     family members, instantiated at every index,
//     with one common position-0 guard expressible in the global formula,
//   - symmetric conjuncts whose index-dependent parts are aggregates over all
//     indices of one local template: letter-level `&&`/`||` of a Boolean
//     template, or `&&`/`||` of `GF b` / `FG b` with a Boolean template b.
// A current-letter Boolean constraint over one family's atoms also qualifies
// when BDD transposition checks and reconstruction prove it is a count predicate.
// Output-only count predicates under positive top-level G conjuncts restrict
// output compositions to accepted weights; their safety monitors remain present.
// With no accepted output, retain one original letter for the monitor to reject.
// Local templates cannot contain generated events; since antecedents use global APs.
// The since-monitor rewrite of G (G gamma -> || G lambda) holds only at position 0.
// The split itself checks the symmetry, so nothing comes from family names.
// Each client runs a deterministic local automaton for the local templates,
// whose acceptance must be a conjunction of single-mark Inf/Fin. Aggregates
// become events: whether some or every client satisfies a predicate now, or a
// breakpoint over clients for `&& GF`. The remaining index-free formula over
// global APs and events is a deterministic parity automaton. A game state is
// that automaton's state and the number of clients in each local state, so it
// is the state's S_n-orbit, and a letter's orbit is the split of each count by
// the clients' local letters. Absorbing states are collapsed by a separate
// parity reduction. The quotient game is solved with Spot and its strategy is
// checked. The abstract theorem in proofs/SymmetryProofs/ transfers
// a verdict provided the concrete action and count-game correspondence hold;
// the formula/monitor encoding and C++ correspondence remain trusted (see
// the Lean proof modules in proofs/SymmetryProofs/).
// Anything outside this fragment is declined.

#include "symmetry_certificate.hh"

#include <cstdint>
#include <spot/tl/formula.hh>
#include <string>
#include <vector>

namespace symmetry {
  struct encoded_certificate;
}

namespace acacia::symcount {

  struct encoded_audit;
  using encoded_auditor = void (*) (const encoded_audit&);

  struct limits {
      size_t max_vertices = size_t (1) << 24;
      size_t max_edges = size_t (1) << 24;
      size_t max_local_letters = 64;
      uint64_t deadline_mono_ns = 0;  // CLOCK_MONOTONIC, zero for none
  };

  enum class verdict { realizable, unrealizable, declined };

  struct result {
      verdict outcome = verdict::declined;
      std::string reason;  // why it declined
      unsigned n = 0, local_states = 0, events = 0, global_states = 0;
      size_t count_states = 0, vertices = 0, edges = 0;
      double build_seconds = 0, solve_seconds = 0, check_seconds = 0;
      bool encoded = false, lifted = false;
  };

  result solve (const spot::formula& spec, const std::vector<std::string>& inputs,
                const std::vector<std::string>& outputs,
                std::span<const symmetry::declared_family> families,
                const limits& bound = {});

  result solve_encoded (const std::string& objective, const std::vector<std::string>& inputs,
                        const std::vector<std::string>& outputs,
                        const symmetry::encoded_certificate& cert,
                        const limits& bound = {}, encoded_auditor audit = nullptr);

}  // namespace acacia::symcount
