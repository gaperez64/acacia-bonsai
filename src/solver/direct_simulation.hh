#pragma once

#include <bddx.h>
#include <cstddef>
#include <spot/twa/twagraph.hh>
#include <vector>

// Direct simulation on the automaton Acacia solves, in the orientation the
// rank-vector closure needs.
//
// Write `p <= q` when **q simulates p**: q is at least as dangerous for the
// bad-language NBA, so an active run at q covers an active run at p.  The
// greatest such relation satisfies, for every edge
//
//     p --(g, mu)--> p'
//
// that g is covered by matching edges
//
//     q --(h, nu)--> q'      with   nu >= mu   and   p' <= q'.
//
// Acacia's increments are 0/1, so `nu >= mu` means an accepting edge may match
// an accepting or a non-accepting one, but not the reverse: a step that costs a
// rank increase cannot be simulated by one that does not.
//
// The greatest fixed point is computed by starting from all pairs and deleting
// violating ones until stable.  That is quadratic in states and needs a BDD
// disjunction per candidate edge, so callers pass a state cap; above it the
// relation is reported as not computed rather than silently costing minutes.

namespace acacia::direct_simulation {

  struct relation {
      /// simulators[p] lists every q with p <= q, excluding p itself.
      std::vector<std::vector<unsigned>> simulators;
      size_t states = 0;
      /// Ordered pairs p <= q with p != q.
      size_t strict_pairs = 0;
      /// States with at least one strict simulator.
      size_t dominated_states = 0;
      /// Pairs that are simulation-equivalent (p <= q and q <= p), unordered.
      size_t equivalent_pairs = 0;
      /// False when the automaton exceeded the cap and nothing was computed.
      bool computed = false;
      /// Fixed-point iterations actually performed.
      unsigned iterations = 0;
  };

  inline relation compute (const spot::const_twa_graph_ptr& aut, size_t state_cap = 400) {
    relation result;
    if (not aut)
      return result;
    result.states = aut->num_states ();
    if (result.states == 0 or result.states > state_cap)
      return result;

    const unsigned n = aut->num_states ();
    // candidate[p * n + q] is true while p <= q is still possible.
    std::vector<bool> candidate (static_cast<size_t> (n) * n, true);

    bool changed = true;
    while (changed) {
      changed = false;
      ++result.iterations;
      for (unsigned p = 0; p < n; ++p)
        for (unsigned q = 0; q < n; ++q) {
          const size_t index = static_cast<size_t> (p) * n + q;
          if (not candidate[index])
            continue;
          bool survives = true;
          for (const auto& e : aut->out (p)) {
            // Edges of q that may match this one: at least as accepting, and
            // landing somewhere that still simulates e.dst.
            bdd cover = bddfalse;
            for (const auto& f : aut->out (q)) {
              const bool e_accepting = e.acc != spot::acc_cond::mark_t {};
              const bool f_accepting = f.acc != spot::acc_cond::mark_t {};
              if (e_accepting and not f_accepting)
                continue;  // nu >= mu fails
              if (not candidate[static_cast<size_t> (e.dst) * n + f.dst])
                continue;
              cover |= f.cond;
            }
            if ((e.cond & !cover) != bddfalse) {
              survives = false;
              break;
            }
          }
          if (not survives) {
            candidate[index] = false;
            changed = true;
          }
        }
    }

    result.simulators.assign (n, {});
    for (unsigned p = 0; p < n; ++p) {
      bool dominated = false;
      for (unsigned q = 0; q < n; ++q) {
        if (p == q or not candidate[static_cast<size_t> (p) * n + q])
          continue;
        result.simulators[p].push_back (q);
        ++result.strict_pairs;
        dominated = true;
        if (p < q and candidate[static_cast<size_t> (q) * n + p])
          ++result.equivalent_pairs;
      }
      if (dominated)
        ++result.dominated_states;
    }
    result.computed = true;
    return result;
  }

}  // namespace acacia::direct_simulation
