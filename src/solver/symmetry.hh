#pragma once

// Detection of client-index permutation symmetries of the (final) UcB automaton,
// verified structurally so that canonicalizing the K-bounded game's counter
// vectors under the induced state-permutation group is SOUND.
//
// Soundness: if phi is a bijection on automaton states and pi is a permutation
// of atomic propositions with pi(inputs)=inputs, pi(outputs)=outputs, such that
// (phi,pi) is a structural automorphism of `aut` -- for every edge q --c--> q'
// there is an edge phi(q) --pi(c)--> phi(q') with the same acceptance, and phi
// fixes the initial state -- then the whole K-bounded safety game built from
// `aut` is phi-equivariant. Hence CPre is equivariant and every fixpoint iterate
// (from the phi-symmetric safe set) is phi-invariant, so collapsing each
// phi-orbit of counter-vectors to a canonical representative preserves the
// winner. We only return generators we VERIFY; unverified candidates are
// discarded (fewer symmetries is always sound).
//
// Detection works for nondeterministic automata: for a candidate client
// transposition pi we build B = pi(A) via spot::relabel_here, then search for a
// label-preserving isomorphism phi : A -> B (which is exactly a (phi,pi)
// automorphism, since B carries the pi-applied edge conditions). The automata
// here are small (linear in the number of clients).

#include "utils/verbose.hh"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <spot/tl/relabel.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/relabel.hh>

namespace symmetry {

  struct group {
      std::vector<std::vector<unsigned>> gens;  // each is a state permutation phi
      bool empty () const { return gens.empty (); }
      size_t size () const { return gens.size (); }
  };

  namespace detail {
    inline bool parse_indexed (const std::string& s, std::string& prefix, long& idx) {
      size_t i = s.size ();
      while (i > 0 and std::isdigit (static_cast<unsigned char> (s[i - 1])))
        --i;
      if (i == s.size ())
        return false;
      prefix = s.substr (0, i);
      idx = std::stol (s.substr (i));
      return true;
    }

    // A single outgoing edge reduced to a hashable label + destination.
    struct out_edge {
        int cond_id;
        size_t acc_hash;
        unsigned dst;
    };

    inline std::vector<std::vector<out_edge>> out_edges (const spot::twa_graph_ptr& a) {
      std::vector<std::vector<out_edge>> res (a->num_states ());
      for (unsigned q = 0; q < a->num_states (); ++q)
        for (const auto& e : a->out (q))
          res[q].push_back ({e.cond.id (), e.acc.hash (), e.dst});
      return res;
    }

    // Per-state signature invariant to the specific state numbering: (accepting,
    // out-degree, sorted (cond,acc) multiset, in-degree). Used to prune matches.
    inline std::vector<std::string> signatures (const spot::twa_graph_ptr& a,
                                                const std::vector<std::vector<out_edge>>& oe) {
      std::vector<unsigned> indeg (a->num_states (), 0);
      for (unsigned q = 0; q < a->num_states (); ++q)
        for (const auto& e : oe[q])
          ++indeg[e.dst];
      std::vector<std::string> sig (a->num_states ());
      for (unsigned q = 0; q < a->num_states (); ++q) {
        std::vector<std::pair<int, size_t>> lbls;
        for (const auto& e : oe[q])
          lbls.push_back ({e.cond_id, e.acc_hash});
        std::sort (lbls.begin (), lbls.end ());
        std::string s = (a->state_is_accepting (q) ? "A" : "-");
        s += ":" + std::to_string (indeg[q]) + ":";
        for (auto& [c, h] : lbls)
          s += std::to_string (c) + "," + std::to_string (h) + ";";
        sig[q] = std::move (s);
      }
      return sig;
    }

    // Backtracking label-preserving isomorphism A -> B. Both share the state set
    // {0..n-1} and initial state. Fills phi (phi[qA] = qB). Returns success.
    struct iso_finder {
        unsigned n;
        const std::vector<std::vector<out_edge>>& A;
        const std::vector<std::vector<out_edge>>& B;
        const std::vector<std::string>& sigA;
        const std::vector<std::string>& sigB;
        // For B, index out-edges of each state by (cond,acc) -> set of dsts.
        std::vector<std::multimap<std::pair<int, size_t>, unsigned>> bindex;
        std::vector<unsigned> phi, inv;

        iso_finder (unsigned n_, const std::vector<std::vector<out_edge>>& A_,
                    const std::vector<std::vector<out_edge>>& B_,
                    const std::vector<std::string>& sA, const std::vector<std::string>& sB)
          : n (n_), A (A_), B (B_), sigA (sA), sigB (sB), bindex (n_),
            phi (n_, -1U), inv (n_, -1U) {
          for (unsigned q = 0; q < n; ++q)
            for (const auto& e : B[q])
              bindex[q].emplace (std::pair {e.cond_id, e.acc_hash}, e.dst);
        }

        // Can qA map to qB given current partial assignment? Check that every
        // already-assigned A-out-edge of qA has a matching B-out-edge of qB.
        bool consistent (unsigned qA, unsigned qB) {
          if (sigA[qA] != sigB[qB] or A[qA].size () != B[qB].size ())
            return false;
          // Count B out-edges per label to compare as multisets.
          std::map<std::pair<int, size_t>, int> need;
          for (const auto& e : A[qA])
            need[{e.cond_id, e.acc_hash}]++;
          std::map<std::pair<int, size_t>, int> have;
          for (const auto& e : B[qB])
            have[{e.cond_id, e.acc_hash}]++;
          if (need != have)
            return false;
          // For edges whose A-dst is already assigned, the B side must contain
          // that image among its dsts for the matching label.
          for (const auto& e : A[qA]) {
            if (phi[e.dst] == -1U)
              continue;
            auto range = bindex[qB].equal_range ({e.cond_id, e.acc_hash});
            bool ok = false;
            for (auto it = range.first; it != range.second; ++it)
              if (it->second == phi[e.dst]) { ok = true; break; }
            if (not ok)
              return false;
          }
          return true;
        }

        bool assign (unsigned qA, unsigned qB) {
          phi[qA] = qB; inv[qB] = qA;
          return true;
        }
        void unassign (unsigned qA, unsigned qB) {
          phi[qA] = -1U; inv[qB] = -1U;
        }

        bool search () {
          // pick unassigned A-state with fewest candidates (MRV)
          unsigned pick = -1U;
          size_t best = SIZE_MAX;
          for (unsigned q = 0; q < n; ++q) {
            if (phi[q] != -1U) continue;
            size_t c = 0;
            for (unsigned q2 = 0; q2 < n; ++q2)
              if (inv[q2] == -1U and sigB[q2] == sigA[q]) ++c;
            if (c == 0) return false;
            if (c < best) { best = c; pick = q; }
          }
          if (pick == -1U)
            return verify_full ();
          for (unsigned q2 = 0; q2 < n; ++q2) {
            if (inv[q2] != -1U or sigB[q2] != sigA[pick]) continue;
            if (not consistent (pick, q2)) continue;
            assign (pick, q2);
            if (search ()) return true;
            unassign (pick, q2);
          }
          return false;
        }

        bool verify_full () {
          // Every A-edge (q,c,acc,dst) must have B-edge (phi q, c, acc, phi dst).
          for (unsigned q = 0; q < n; ++q) {
            for (const auto& e : A[q]) {
              auto range = bindex[phi[q]].equal_range ({e.cond_id, e.acc_hash});
              bool ok = false;
              for (auto it = range.first; it != range.second; ++it)
                if (it->second == phi[e.dst]) { ok = true; break; }
              if (not ok)
                return false;
            }
          }
          return true;
        }
    };
  }  // namespace detail

  // Detect verified client-transposition symmetries of `aut`. `all_inputs` /
  // `all_outputs` are BDD cubes over the input/output APs (used to keep each
  // transposition within one side of the I/O partition).
  inline group detect (const spot::twa_graph_ptr& aut, bdd all_inputs, bdd all_outputs) {
    group G;
    auto dict = aut->get_dict ();

    // Group indexed APs into families (prefix), tagged input/output.
    struct fam { std::map<long, spot::formula> idx2ap; bool is_input; };
    std::map<std::string, fam> families;
    for (const spot::formula& ap : aut->ap ()) {
      const int v = dict->varnum (ap);
      const bdd vbdd = bdd_ithvar (v);
      const bool is_in = (bdd_exist (all_inputs, vbdd) != all_inputs);
      std::string prefix; long idx;
      if (not detail::parse_indexed (ap.ap_name (), prefix, idx))
        continue;
      auto& f = families[prefix];
      f.is_input = is_in;
      f.idx2ap[idx] = ap;
    }
    if (families.empty ())
      return G;

    // Client indices present in EVERY family.
    std::map<long, int> cnt;
    for (auto& [pfx, f] : families)
      for (auto& [idx, ap] : f.idx2ap)
        ++cnt[idx];
    std::vector<long> indices;
    for (auto& [idx, c] : cnt)
      if (c == (int) families.size ())
        indices.push_back (idx);
    std::sort (indices.begin (), indices.end ());
    if (indices.size () < 2)
      return G;

    const auto oeA = detail::out_edges (aut);
    const auto sigA = detail::signatures (aut, oeA);
    const unsigned n = aut->num_states ();

    for (size_t ia = 0; ia < indices.size (); ++ia)
      for (size_t ib = ia + 1; ib < indices.size (); ++ib) {
        const long a = indices[ia], b = indices[ib];
        // Build B = pi(A) with the transposition a<->b across all families.
        spot::relabeling_map rm;
        for (auto& [pfx, f] : families) {
          rm[f.idx2ap.at (a)] = f.idx2ap.at (b);
          rm[f.idx2ap.at (b)] = f.idx2ap.at (a);
        }
        auto B = spot::make_twa_graph (aut, spot::twa::prop_set::all ());
        spot::relabel_here (B, &rm);
        if (B->get_init_state_number () != aut->get_init_state_number ())
          continue;  // shouldn't happen; be safe

        const auto oeB = detail::out_edges (B);
        const auto sigB = detail::signatures (B, oeB);

        detail::iso_finder f (n, oeA, oeB, sigA, sigB);
        // Anchor: init -> init.
        const unsigned init = aut->get_init_state_number ();
        if (sigA[init] != sigB[init] or not f.consistent (init, init))
          continue;
        f.assign (init, init);
        if (f.search ())
          G.gens.push_back (f.phi);
      }

    verb_do (1, vout << "[symmetry] verified " << G.gens.size () << "/"
                     << (indices.size () * (indices.size () - 1) / 2)
                     << " client-transposition generators over " << indices.size ()
                     << " clients, aut has " << n << " states\n");
    return G;
  }

}  // namespace symmetry
