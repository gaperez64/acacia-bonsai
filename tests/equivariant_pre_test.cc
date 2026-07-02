#include "solver/equivariant_k_bounded_safety_aut.hh"
#include "utils/verbose.hh"

#include <posets/downsets.hh>
#include <posets/vectors.hh>

#include <algorithm>
#include <cassert>
#include <iostream>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}

namespace posets::vectors {
  size_t bool_threshold = 0;
  size_t bitset_threshold = 0;
}

namespace {

  namespace eq = acacia::solver_detail::equivariant;

  using state = posets::vectors::vector_backed<VECTOR_ELT_T>;
  using SetOfStates = posets::downsets::vector_backed<state>;

  constexpr VECTOR_ELT_T K = 3;

  struct fixture {
      spot::twa_graph_ptr aut;
      bdd all_inputs = bddtrue;
      bdd all_outputs = bddtrue;
  };

  bool expect (const std::string& what, bool condition) {
    if (condition)
      return true;
    std::cerr << "FAIL: " << what << "\n";
    return false;
  }

  fixture make_aut (unsigned n) {
    fixture fx;
    fx.aut = spot::make_twa_graph (spot::make_bdd_dict ());
    fx.aut->set_acceptance (1, spot::acc_cond::acc_code::buchi ());
    fx.aut->prop_state_acc (true);
    fx.aut->new_states (1 + 2 * n);
    fx.aut->set_init_state (0);

    std::vector<bdd> r (n), g (n);
    for (unsigned i = 0; i < n; ++i) {
      const int rv = fx.aut->register_ap ("r_" + std::to_string (i));
      const int gv = fx.aut->register_ap ("g_" + std::to_string (i));
      r[i] = bdd_ithvar (rv);
      g[i] = bdd_ithvar (gv);
      fx.all_inputs &= r[i];
      fx.all_outputs &= g[i];
    }

    fx.aut->new_acc_edge (0, 0, bddtrue, false);
    for (unsigned i = 0; i < n; ++i)
      fx.aut->new_acc_edge (0, 1 + i, r[i], false);

    for (unsigned i = 0; i < n; ++i) {
      const unsigned wait = 1 + i;
      const unsigned done = n + 1 + i;
      fx.aut->new_acc_edge (wait, wait, !g[i]);
      fx.aut->new_acc_edge (wait, done, g[i]);
      fx.aut->new_acc_edge (done, done, !g[i], false);
      fx.aut->new_acc_edge (done, 0, g[i], false);
    }
    return fx;
  }

  std::vector<std::vector<unsigned>> all_slot_permutations (unsigned n) {
    std::vector<unsigned> sigma (n);
    std::iota (sigma.begin (), sigma.end (), 0);
    std::vector<std::vector<unsigned>> out;
    do {
      out.push_back (sigma);
    } while (std::next_permutation (sigma.begin (), sigma.end ()));
    return out;
  }

  posets::utils::vector_mm<VECTOR_ELT_T> safe_vector (unsigned N) {
    posets::utils::vector_mm<VECTOR_ELT_T> v (N, K - 1);
    for (size_t q = posets::vectors::bool_threshold; q < N; ++q)
      v[q] = 0;
    return v;
  }

  SetOfStates safe_downset (unsigned N) {
    return SetOfStates (state (safe_vector (N)));
  }

  std::vector<eq::action_vec> direct_actions (
      const spot::twa_graph_ptr& aut, bdd input, const std::vector<bdd>& output_letters) {
    std::set<eq::action_vec> uniq;
    for (bdd output : output_letters)
      uniq.insert (eq::detail::compute_action_vec (
          aut, eq::detail::compute_transset (aut, input & output)));
    return std::vector<eq::action_vec> (uniq.begin (), uniq.end ());
  }

  bool same_downset (const SetOfStates& a, const SetOfStates& b) {
    return eq::subset_of (a, b) and eq::subset_of (b, a);
  }

  std::string sequence_string (const std::vector<unsigned>& seq) {
    std::string out;
    for (unsigned x : seq) {
      if (not out.empty ())
        out += ",";
      out += std::to_string (x);
    }
    return out;
  }

  bool check_oracle_for_downset (const fixture& fx, const symmetry::group& G,
                                 const symmetry::block_layout& L,
                                 const std::vector<eq::input_orbit>& orbits,
                                 const SetOfStates& f, unsigned n) {
    std::vector<std::vector<int>> family_slot_vars;
    std::set<int> indexed_input_vars;
    if (not expect ("build input family vars",
                    eq::detail::build_client_family_vars (fx.aut, G, L, true,
                                                          family_slot_vars,
                                                          indexed_input_vars)))
      return false;
    const unsigned num_types = 1U << (unsigned) family_slot_vars.size ();
    const auto shared_input_vars = eq::detail::shared_vars (fx.aut, fx.all_inputs,
                                                            indexed_input_vars);
    const auto output_letters =
        eq::detail::enumerate_letters (fx.all_outputs, ACACIA_EQUIVARIANT_MAX_OUTPUT_LETTERS);
    if (not expect ("no shared inputs in oracle automaton", shared_input_vars.empty ()) or
        not expect ("output letters enumerated", not output_letters.empty ()))
      return false;

    std::vector<std::pair<bdd, std::vector<eq::transset>>> empty_itoios;
    auto actioner = actioners::standard<state>::make (fx.aut, empty_itoios, K);

    bool ok = true;
    for (size_t oi = 0; oi < orbits.size (); ++oi) {
      const auto& orbit = orbits[oi];
      SetOfStates T_rep = eq::compute_T (f, orbit.actions, actioner, fx.aut->num_states ());
      std::vector<unsigned> seq = orbit.canonical_types;
      do {
        const auto sigma = eq::match_slots (orbit.canonical_types, seq, num_types);
        const auto phi = eq::phi_from_sigma (L, sigma);
        SetOfStates T_eq = eq::permute (T_rep, phi);

        bdd input = eq::detail::input_letter_from_slot_types (family_slot_vars,
                                                              shared_input_vars, seq, 0);
        auto actions = direct_actions (fx.aut, input, output_letters);
        SetOfStates T_direct = eq::compute_T (f, actions, actioner, fx.aut->num_states ());

        if (not same_downset (T_eq, T_direct)) {
          std::cerr << "FAIL: oracle mismatch n=" << n
                    << " bool_threshold=" << posets::vectors::bool_threshold
                    << " orbit=" << oi
                    << " seq=" << sequence_string (seq) << "\n";
          ok = false;
        }
      } while (std::next_permutation (seq.begin (), seq.end ()));
    }
    return ok;
  }

  SetOfStates random_invariant_downset (std::mt19937& rng, const symmetry::block_layout& L,
                                        unsigned N) {
    std::uniform_int_distribution<int> val_dist (-1, K - 1);
    std::vector<state> elements;
    const auto sigmas = all_slot_permutations (L.num_clients);
    elements.reserve (4 * sigmas.size ());
    for (unsigned sample = 0; sample < 4; ++sample) {
      posets::utils::vector_mm<VECTOR_ELT_T> base (N, 0);
      for (unsigned q = 0; q < N; ++q)
        base[q] = (q < posets::vectors::bool_threshold) ? (VECTOR_ELT_T) val_dist (rng) : 0;

      for (const auto& sigma : sigmas) {
        const auto phi = eq::phi_from_sigma (L, sigma);
        posets::utils::vector_mm<VECTOR_ELT_T> out (N, 0);
        for (unsigned q = 0; q < N; ++q)
          out[phi[q]] = base[q];
        elements.push_back (state (out));
      }
    }
    return SetOfStates (std::move (elements));
  }

  bool run_case (unsigned n, size_t bool_threshold) {
    posets::vectors::bool_threshold = bool_threshold;
    const fixture fx = make_aut (n);
    posets::vectors::bitset_threshold = fx.aut->num_states ();

    const auto G = symmetry::detect (fx.aut, fx.all_inputs, fx.all_outputs);
    bool ok = true;
    ok &= expect ("full symmetric group detected", G.full_symmetric);
    ok &= expect ("indices size", G.indices.size () == n);
    ok &= expect ("generator pair metadata size", G.gen_pairs.size () == G.gens.size ());
    if (not ok)
      return false;

    auto L = symmetry::compute_block_layout (G, fx.aut->num_states ());
    ok &= expect ("layout exists", L.has_value ());
    if (not ok)
      return false;
    ok &= expect ("two client blocks", L->num_blocks == 2);
    ok &= expect ("layout client count", L->num_clients == n);
    ok &= expect ("generators match layout", symmetry::generators_match_layout (G, *L));

    auto orbits = eq::build_orbits (fx.aut, fx.all_inputs, fx.all_outputs, G, *L);
    ok &= expect ("orbits built", orbits.has_value ());
    if (not ok)
      return false;
    ok &= expect ("n+1 input orbits", orbits->size () == n + 1);
    if (not ok)
      return false;

    ok &= check_oracle_for_downset (fx, G, *L, *orbits, safe_downset (fx.aut->num_states ()), n);

    std::mt19937 rng (1000 + n * 100 + (unsigned) bool_threshold);
    for (unsigned trial = 0; trial < 5; ++trial) {
      SetOfStates f = random_invariant_downset (rng, *L, fx.aut->num_states ());
      ok &= check_oracle_for_downset (fx, G, *L, *orbits, f, n);
    }
    return ok;
  }

}  // namespace

int main () {
  const size_t old_bool_threshold = posets::vectors::bool_threshold;
  const size_t old_bitset_threshold = posets::vectors::bitset_threshold;

  bool ok = true;
  for (unsigned n : {3U, 4U}) {
    ok &= run_case (n, 1 + 2 * n);
    ok &= run_case (n, 1 + n);
  }

  posets::vectors::bool_threshold = old_bool_threshold;
  posets::vectors::bitset_threshold = old_bitset_threshold;

  if (ok) {
    std::cout << "ALL equivariant_pre oracle tests PASSED\n";
    return 0;
  }
  return 1;
}
