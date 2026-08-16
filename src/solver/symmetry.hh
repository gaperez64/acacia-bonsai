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

#include "configuration.hh"
#include "solver/symmetry_certificate.hh"
#include "utils/verbose.hh"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <spot/tl/relabel.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/relabel.hh>

namespace symmetry {

  // Metadata for one indexed AP family (e.g. "r_" -> {0: ap(r_0), 1: ap(r_1), ...}).
  struct family {
      bool is_input;
      std::map<long, spot::formula> idx2ap;
  };

  using family_map = std::map<std::string, family>;

  struct group {
      std::vector<std::vector<unsigned>> gens;  // verified state-permutation generators
      // gen_pairs[t] is the client-index pair (a, b) whose verified transposition
      // produced gens[t]. Same order and length as gens.
      std::vector<std::pair<long, long>> gen_pairs;
      std::vector<long> indices;                // client indices, sorted (|indices| = n)
      // True iff the verified transpositions generate the full symmetric group
      // Sym(indices). The exhaustive detector verifies every pair; the
      // equivariant fast detector verifies a star generating set.
      bool full_symmetric = false;
      family_map families;                       // AP family metadata, keyed by prefix

      bool empty () const { return gens.empty (); }
      size_t size () const { return gens.size (); }
  };

  struct indexed_ap_analysis {
      family_map families;
      std::vector<long> indices;
      unsigned input_families = 0;
      unsigned output_families = 0;
      bool syntax_certified = false;

      bool empty () const { return families.empty (); }
  };

  inline thread_local const std::vector<indexed_family_certificate>*
      current_indexed_family_certificates = nullptr;

  class scoped_indexed_family_certificates {
    public:
      explicit scoped_indexed_family_certificates (
          const std::vector<indexed_family_certificate>& certificates)
        : previous {current_indexed_family_certificates} {
        current_indexed_family_certificates = &certificates;
      }

      ~scoped_indexed_family_certificates () {
        current_indexed_family_certificates = previous;
      }

    private:
      const std::vector<indexed_family_certificate>* previous;
  };

  inline bool has_indexed_family_certificate_hypothesis () {
    return current_indexed_family_certificates != nullptr and
           not current_indexed_family_certificates->empty ();
  }

  // Pre-formatted fields used by the compact solver diagnostics.  A field is
  // "-" when the corresponding structure is absent.
  struct structure_report {
      std::string families = "-";
      std::string indices = "-";
      std::string matrix = "-";
      std::string subsets = "-";
      std::string selected = "-";
      std::string orbit_sizes = "-";
      std::string blocks = "-";
      std::string shared = "-";
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

    inline std::optional<std::vector<unsigned>> verify_transposition (
        const spot::twa_graph_ptr& aut, const family_map& families,
        long a, long b, const std::vector<std::vector<out_edge>>& oeA,
        const std::vector<std::string>& sigA) {
      const unsigned n = aut->num_states ();
      const unsigned init = aut->get_init_state_number ();

      spot::relabeling_map rm;
      for (auto& [pfx, f] : families) {
        rm[f.idx2ap.at (a)] = f.idx2ap.at (b);
        rm[f.idx2ap.at (b)] = f.idx2ap.at (a);
      }
      auto B = spot::make_twa_graph (aut, spot::twa::prop_set::all ());
      spot::relabel_here (B, &rm);
      if (B->get_init_state_number () != init)
        return std::nullopt;

      const auto oeB = out_edges (B);
      const auto sigB = signatures (B, oeB);

      iso_finder f (n, oeA, oeB, sigA, sigB);
      if (sigA[init] != sigB[init] or not f.consistent (init, init))
        return std::nullopt;
      f.assign (init, init);
      if (not f.search ())
        return std::nullopt;
      return f.phi;
    }

    // Structural automorphisms are closed under conjugation, so verified
    // (a,b) and (b,c) swaps force (a,c).  The verified-transposition graph is
    // therefore a union of cliques: its largest star is its largest component.
    struct transposition_oracle {
        transposition_oracle (const spot::twa_graph_ptr& a, const family_map& f)
          : aut {a}, families {f}, oeA {out_edges (a)}, sigA {signatures (a, oeA)} {}

        const std::optional<std::vector<unsigned>>& verify (long a, long b) {
          const std::pair<long, long> key = std::minmax (a, b);
          auto [it, inserted] = memo.try_emplace (key, std::nullopt);
          if (inserted)
            it->second = verify_transposition (aut, families, key.first, key.second, oeA, sigA);
          return it->second;
        }

      private:
        spot::twa_graph_ptr aut;
        const family_map& families;
        std::vector<std::vector<out_edge>> oeA;
        std::vector<std::string> sigA;
        std::map<std::pair<long, long>, std::optional<std::vector<unsigned>>> memo;
    };
  }  // namespace detail

  inline indexed_ap_analysis analyze_indexed_aps (const spot::twa_graph_ptr& aut,
                                                  bdd all_inputs, bdd all_outputs) {
    indexed_ap_analysis analysis;
    auto dict = aut->get_dict ();

    // Group indexed APs into families (prefix), tagged input/output.
    for (const spot::formula& ap : aut->ap ()) {
      const int v = dict->varnum (ap);
      const bdd vbdd = bdd_ithvar (v);
      const bool is_in = (bdd_exist (all_inputs, vbdd) != all_inputs);
      const bool is_out = (bdd_exist (all_outputs, vbdd) != all_outputs);
      if (not is_in and not is_out)
        continue;
      std::string prefix; long idx;
      if (not detail::parse_indexed (ap.ap_name (), prefix, idx))
        continue;
      auto& f = analysis.families[prefix];
      f.is_input = is_in;
      f.idx2ap[idx] = ap;
    }
    if (current_indexed_family_certificates != nullptr) {
      std::map<std::string, const indexed_family_certificate*> certified;
      for (const auto& certificate : *current_indexed_family_certificates) {
        if (certificate.members.empty ())
          continue;
        const auto expected_size = certificate.hi >= certificate.lo
            ? static_cast<unsigned long long> (certificate.hi - certificate.lo) + 1
            : 0;
        if (expected_size != certificate.members.size ())
          throw std::runtime_error ("TLSF indexed-family certificate has an invalid range");

        std::string family_prefix;
        for (size_t position = 0; position < certificate.members.size (); ++position) {
          std::string prefix;
          long index = -1;
          if (not detail::parse_indexed (certificate.members[position], prefix, index) or
              index != certificate.lo + static_cast<long> (position))
            throw std::runtime_error (
                "TLSF indexed-family certificate disagrees with scalar AP mangling");
          if (position == 0)
            family_prefix = std::move (prefix);
          else if (prefix != family_prefix)
            throw std::runtime_error (
                "TLSF indexed-family certificate spans multiple AP prefixes");
        }
        if (not certified.emplace (family_prefix, &certificate).second)
          throw std::runtime_error ("duplicate TLSF indexed-family certificate prefix");
      }

      for (const auto& [prefix, certificate] : certified) {
        auto family = analysis.families.find (prefix);
        if (family == analysis.families.end ())
          continue;  // This translated/decomposed component does not use it.
        if (family->second.is_input != certificate->is_input)
          throw std::runtime_error (
              "TLSF indexed-family certificate disagrees with the I/O partition");
        for (const auto& [index, ap] : family->second.idx2ap) {
          if (index < certificate->lo or index > certificate->hi or
              certificate->members[index - certificate->lo] != ap.ap_name ())
            throw std::runtime_error (
                "TLSF indexed-family certificate disagrees with name-derived APs");
        }
        analysis.syntax_certified = true;
      }
    }

    if (analysis.families.empty ())
      return analysis;

    // Client indices present in EVERY family.
    std::map<long, int> cnt;
    for (auto& [pfx, f] : analysis.families) {
      if (f.is_input)
        ++analysis.input_families;
      else
        ++analysis.output_families;
      for (auto& [idx, ap] : f.idx2ap)
        ++cnt[idx];
    }
    for (auto& [idx, c] : cnt)
      if (c == (int) analysis.families.size ())
        analysis.indices.push_back (idx);
    std::sort (analysis.indices.begin (), analysis.indices.end ());
    return analysis;
  }

  inline group group_from_analysis (const indexed_ap_analysis& analysis) {
    group G;
    G.families = analysis.families;
    G.indices = analysis.indices;
    return G;
  }

  inline bool full_symmetric_on_indices (const group& G);

  // Detect verified client-transposition symmetries of `aut`. `all_inputs` /
  // `all_outputs` are BDD cubes over the input/output APs (used to keep each
  // transposition within one side of the I/O partition).
  inline group detect (const spot::twa_graph_ptr& aut,
                       const indexed_ap_analysis& analysis) {
    group G = group_from_analysis (analysis);
    if (G.families.empty () or G.indices.size () < 2)
      return G;

    [[maybe_unused]] const unsigned total_pairs =
        (unsigned) (G.indices.size () * (G.indices.size () - 1) / 2);
    detail::transposition_oracle oracle {aut, G.families};

    for (size_t ia = 0; ia < G.indices.size (); ++ia)
      for (size_t ib = ia + 1; ib < G.indices.size (); ++ib) {
        const long a = G.indices[ia], b = G.indices[ib];
        const auto& phi = oracle.verify (a, b);
        if (phi.has_value ()) {
          G.gens.push_back (*phi);
          G.gen_pairs.push_back ({a, b});
        }
      }

    // This compatibility detector remains exhaustive: callers that use
    // symmetry::detect directly still receive every verified pairwise
    // transposition in deterministic order.
    G.full_symmetric = full_symmetric_on_indices (G);

    verb_do (1, vout << "[symmetry] verified " << G.gens.size () << "/" << total_pairs
                     << " client-transposition generators over " << G.indices.size ()
                     << " clients (full_symmetric=" << G.full_symmetric
                     << "), aut has " << aut->num_states () << " states\n");
    return G;
  }

  inline group detect (const spot::twa_graph_ptr& aut, bdd all_inputs, bdd all_outputs) {
    return detect (aut, analyze_indexed_aps (aut, all_inputs, all_outputs));
  }

  // Directly verified AP-index transpositions, including the identity on the
  // diagonal.  This is intentionally based on gen_pairs rather than the state
  // permutations so diagnostics can display the exact recognition evidence.
  inline std::vector<std::vector<bool>> verified_transposition_matrix (const group& G) {
    const size_t n = G.indices.size ();
    std::vector<std::vector<bool>> matrix (n, std::vector<bool> (n, false));
    std::map<long, size_t> position;
    for (size_t i = 0; i < n; ++i) {
      position[G.indices[i]] = i;
      matrix[i][i] = true;
    }
    for (const auto& [a, b] : G.gen_pairs) {
      auto ia = position.find (a), ib = position.find (b);
      if (ia == position.end () or ib == position.end ())
        continue;
      matrix[ia->second][ib->second] = true;
      matrix[ib->second][ia->second] = true;
    }
    return matrix;
  }

  // Verified transpositions generate a full symmetric group on each connected
  // component of this graph.  (The edge transpositions of any connected graph
  // generate S_n.)  Components therefore are the maximal index subsets that
  // the available structural evidence permits us to use symmetrically.
  inline std::vector<std::vector<long>> maximal_full_symmetric_index_subsets (const group& G) {
    const auto matrix = verified_transposition_matrix (G);
    std::vector<bool> seen (G.indices.size (), false);
    std::vector<std::vector<long>> subsets;
    for (size_t start = 0; start < G.indices.size (); ++start) {
      if (seen[start])
        continue;
      std::vector<size_t> todo {start};
      seen[start] = true;
      std::vector<long> subset;
      while (not todo.empty ()) {
        const size_t at = todo.back ();
        todo.pop_back ();
        subset.push_back (G.indices[at]);
        for (size_t next = 0; next < G.indices.size (); ++next)
          if (not seen[next] and matrix[at][next]) {
            seen[next] = true;
            todo.push_back (next);
          }
      }
      std::sort (subset.begin (), subset.end ());
      subsets.push_back (std::move (subset));
    }
    std::sort (subsets.begin (), subsets.end ());
    return subsets;
  }

  inline bool full_symmetric_on_indices (const group& G) {
    if (G.indices.size () < 2)
      return false;
    const auto components = maximal_full_symmetric_index_subsets (G);
    return components.size () == 1 and components.front ().size () == G.indices.size ();
  }

  inline group restrict_to_full_symmetric_subset (const group& G,
                                                   const std::vector<long>& indices) {
    group result;
    result.families = G.families;
    result.indices = indices;
    for (size_t i = 0; i < G.gens.size () and i < G.gen_pairs.size (); ++i) {
      const auto [a, b] = G.gen_pairs[i];
      if (std::binary_search (indices.begin (), indices.end (), a) and
          std::binary_search (indices.begin (), indices.end (), b)) {
        result.gens.push_back (G.gens[i]);
        result.gen_pairs.push_back ({a, b});
      }
    }
    result.full_symmetric = full_symmetric_on_indices (result);
    return result;
  }

  inline group largest_full_symmetric_subgroup (const group& G) {
    const auto subsets = maximal_full_symmetric_index_subsets (G);
    if (subsets.empty ()) {
      group result;
      result.families = G.families;
      return result;
    }
    const auto best = std::max_element (
        subsets.begin (), subsets.end (), [] (const auto& lhs, const auto& rhs) {
          if (lhs.size () != rhs.size ())
            return lhs.size () < rhs.size ();
          return lhs > rhs;  // deterministic tie-break: lexicographically first
        });
    return restrict_to_full_symmetric_subset (G, *best);
  }

  inline group detect_full_symmetric_generators (const spot::twa_graph_ptr& aut,
                                                 const indexed_ap_analysis& analysis) {
#if ACACIA_EQUIVARIANT_EXHAUSTIVE_DETECT
    return largest_full_symmetric_subgroup (detect (aut, analysis));
#else
    const group base = group_from_analysis (analysis);
    if (base.families.empty () or base.indices.size () < 2)
      return base;

    detail::transposition_oracle oracle {aut, base.families};
    group G;
    G.families = base.families;

    // A verified star generates the full symmetric group on its vertices.
    // Try every possible root after a failed edge and keep the largest star;
    // indices outside that star remain ordinary shared AP/state structure.
    for (long root : base.indices) {
      group candidate;
      candidate.families = base.families;
      candidate.indices.push_back (root);
      for (long other : base.indices) {
        if (other == root)
          continue;
        const auto& phi = oracle.verify (root, other);
        if (not phi.has_value ())
          continue;
        candidate.indices.push_back (other);
        candidate.gens.push_back (*phi);
        candidate.gen_pairs.push_back ({root, other});
      }
      std::sort (candidate.indices.begin (), candidate.indices.end ());
      candidate.full_symmetric = full_symmetric_on_indices (candidate);

      if (candidate.indices.size () > G.indices.size () or
          (candidate.indices.size () == G.indices.size () and
           candidate.indices < G.indices))
        G = std::move (candidate);
      if (G.indices.size () == base.indices.size ())
        break;
    }

# if ACACIA_EQUIVARIANT_VALIDATE_FAST_RECOGNITION
    const group exhaustive = largest_full_symmetric_subgroup (detect (aut, analysis));
    const bool agrees = (G.full_symmetric == exhaustive.full_symmetric) and
                        (G.indices == exhaustive.indices);
    if (not agrees)
      verb_do (1, vout << "[symmetry] fast/exhaustive recognition mismatch:"
                       << " fast_full=" << G.full_symmetric
                       << " exhaustive_full=" << exhaustive.full_symmetric
                       << " clients=" << G.indices.size () << "\n");
    assert (agrees);
# endif

    [[maybe_unused]] const unsigned needed =
        G.indices.size () > 0 ? (unsigned) G.indices.size () - 1 : 0;
    verb_do (1, vout << "[symmetry] fast verified " << G.gens.size () << "/" << needed
                     << " star transposition generators over " << G.indices.size ()
                     << " clients (full_symmetric=" << G.full_symmetric
                     << "), aut has " << aut->num_states () << " states\n");
    return G;
#endif
  }

  inline group detect_full_symmetric_generators (const spot::twa_graph_ptr& aut,
                                                 bdd all_inputs, bdd all_outputs) {
    return detect_full_symmetric_generators (
        aut, analyze_indexed_aps (aut, all_inputs, all_outputs));
  }

  // Defined in symmetric_blocks.hh, after block-layout recovery is available.
  inline structure_report describe (const indexed_ap_analysis& analysis, const group& G,
                                    unsigned num_states);

}  // namespace symmetry
