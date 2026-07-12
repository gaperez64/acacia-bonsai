#define ACACIA_EQUIVARIANT_MIN_BLOCKS 0
#define ACACIA_EQUIVARIANT_MAX_SWEEP_CLIENTS 0

#include "input_pickers/critical.hh"
#include "ios_precomputers/mona.hh"
#include "ios_precomputers/standard.hh"
#include "solver/equivariant_k_bounded_safety_aut.hh"
#include "utils/verbose.hh"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <vector>

#include <posets/downsets.hh>
#include <posets/vectors.hh>

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

  using state = posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>;
  using SetOfStates = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<state>;

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

  fixture make_aut (unsigned n, bool forced_unreal = false) {
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

    fx.aut->new_acc_edge (0, 0, bddtrue, forced_unreal);
    for (unsigned i = 0; i < n; ++i)
      fx.aut->new_acc_edge (0, 1 + i, r[i], false);

    for (unsigned i = 0; i < n; ++i) {
      const unsigned wait = 1 + i;
      const unsigned done = n + 1 + i;
      fx.aut->new_acc_edge (wait, wait, !g[i], false);
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

  SetOfStates safe_downset (unsigned N) { return SetOfStates (state (safe_vector (N))); }

  std::vector<eq::action_vec> direct_actions (const spot::twa_graph_ptr& aut, bdd input,
                                              const std::vector<bdd>& output_letters) {
    std::set<eq::action_vec> uniq;
    for (bdd output : output_letters)
      uniq.insert (eq::detail::compute_action_vec (
          aut, eq::detail::compute_transset (aut, input & output)));
    return std::vector<eq::action_vec> (uniq.begin (), uniq.end ());
  }

  bool same_downset (const SetOfStates& a, const SetOfStates& b) {
    return eq::subset_of (a, b) and eq::subset_of (b, a);
  }

  SetOfStates reference_permute (const SetOfStates& f, const std::vector<unsigned>& phi) {
    return f.apply ([&] (const auto& s) {
      posets::utils::vector_mm<VECTOR_ELT_T> out (s.size (), 0);
      for (size_t q = 0; q < s.size (); ++q)
        out[phi[q]] = s[q];
      return state (out);
    });
  }

  SetOfStates clone_downset (const SetOfStates& f) {
    return f.apply ([] (const auto& maximal) { return maximal.copy (); });
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
                                 const std::vector<eq::input_orbit>& orbits, const SetOfStates& f,
                                 unsigned n) {
    std::vector<std::vector<int>> family_slot_vars;
    std::set<int> indexed_input_vars;
    if (not expect ("build input family vars",
                    eq::detail::build_client_family_vars (fx.aut, G, L, true, family_slot_vars,
                                                          indexed_input_vars)))
      return false;
    const unsigned num_types = 1U << (unsigned) family_slot_vars.size ();
    const auto shared_input_vars =
        eq::detail::shared_vars (fx.aut, fx.all_inputs, indexed_input_vars);
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
        SetOfStates T_reference = reference_permute (T_rep, phi);
        if (not same_downset (T_eq, T_reference)) {
          std::cerr << "FAIL: fast/reference permutation mismatch n=" << n << " orbit=" << oi
                    << " seq=" << sequence_string (seq) << "\n";
          ok = false;
        }

        bdd input =
            eq::detail::input_letter_from_slot_types (family_slot_vars, shared_input_vars, seq, 0);
        auto actions = direct_actions (fx.aut, input, output_letters);
        SetOfStates T_direct = eq::compute_T (f, actions, actioner, fx.aut->num_states ());

        if (not same_downset (T_eq, T_direct)) {
          std::cerr << "FAIL: oracle mismatch n=" << n
                    << " bool_threshold=" << posets::vectors::bool_threshold << " orbit=" << oi
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

  SetOfStates random_downset (std::mt19937& rng, unsigned N) {
    std::uniform_int_distribution<int> val_dist (-1, K - 1);
    std::vector<state> elements;
    for (unsigned sample = 0; sample < 7; ++sample) {
      posets::utils::vector_mm<VECTOR_ELT_T> raw (N, 0);
      for (unsigned q = 0; q < N; ++q)
        raw[q] = q < posets::vectors::bool_threshold
                     ? (VECTOR_ELT_T) val_dist (rng)
                     : (VECTOR_ELT_T) (val_dist (rng) >= 0 ? 0 : -1);
      elements.emplace_back (raw);
    }
    return SetOfStates (std::move (elements));
  }

  bool check_closure_oracle (std::mt19937& rng, const symmetry::group& G,
                             const symmetry::block_layout& L, unsigned N) {
    bool ok = true;
    for (unsigned trial = 0; trial < 8; ++trial) {
      SetOfStates original = random_downset (rng, N);
      SetOfStates closed = clone_downset (original);
      const bool changed = eq::close_under_generators (closed, G);

      const auto sigmas = all_slot_permutations (L.num_clients);
      SetOfStates brute = eq::permute (original, eq::phi_from_sigma (L, sigmas.front ()));
      for (size_t i = 1; i < sigmas.size (); ++i) {
        SetOfStates image = eq::permute (original, eq::phi_from_sigma (L, sigmas[i]));
        brute.intersect_with (std::move (image));
      }
      ok &= expect ("generator closure equals full group intersection",
                    same_downset (closed, brute));
      ok &= expect ("closure is idempotent", not eq::close_under_generators (closed, G));
      if (changed)
        ok &=
            expect ("changed closure is a strict tightening", not same_downset (original, closed));
    }

    SetOfStates invariant = random_invariant_downset (rng, L, N);
    ok &= expect ("invariant closure is a no-op", not eq::close_under_generators (invariant, G));
    return ok;
  }

  std::optional<std::pair<VECTOR_ELT_T, SetOfStates>> reference_sweep (
      const fixture& fx, const symmetry::block_layout& L,
      const std::vector<eq::input_orbit>& orbits, VECTOR_ELT_T kmin, VECTOR_ELT_T kmax,
      VECTOR_ELT_T kinc) {
    const unsigned N = fx.aut->num_states ();
    const unsigned num_types = eq::orbit_type_count (orbits);
    std::vector<std::pair<bdd, std::vector<eq::transset>>> empty_itoios;
    VECTOR_ELT_T k = kmin;
    auto actioner = actioners::standard<state>::make (fx.aut, empty_itoios, k);

    posets::utils::vector_mm<VECTOR_ELT_T> init (N, -1);
    init[fx.aut->get_init_state_number ()] = 0;
    auto safe = posets::utils::vector_mm<VECTOR_ELT_T> (N, k - 1);
    for (size_t q = posets::vectors::bool_threshold; q < N; ++q)
      safe[q] = 0;
    SetOfStates f {state (safe)};

    while (true) {
      bool changed = false;
      bool incremented = false;
      for (const auto& orbit : orbits) {
        SetOfStates representative = eq::compute_T (f, orbit.actions, actioner, N);
        std::vector<unsigned> sequence = orbit.canonical_types;
        do {
          const auto sigma = eq::match_slots (orbit.canonical_types, sequence, num_types);
          SetOfStates member = eq::permute (representative, eq::phi_from_sigma (L, sigma));
          if (not eq::subset_of (f, member)) {
            f.intersect_with (std::move (member));
            changed = true;
          }
        } while (std::next_permutation (sequence.begin (), sequence.end ()));

        if (not f.contains (state (init))) {
          if (k >= kmax)
            return std::nullopt;
          k += kinc;
          actioner.setK (k);
          f = f.apply ([&] (const state& maximal) {
            posets::utils::vector_mm<VECTOR_ELT_T> bumped (maximal.size (), 0);
            for (size_t q = 0; q < posets::vectors::bool_threshold; ++q)
              bumped[q] = maximal[q] + kinc;
            return state (bumped);
          });
          incremented = true;
          break;
        }
      }
      if (incremented)
        continue;
      if (not changed)
        return std::make_optional (std::make_pair (k, std::move (f)));
    }
  }

  bool check_end_to_end (const fixture& fx, const symmetry::group& G,
                         const symmetry::block_layout& L,
                         const std::vector<eq::input_orbit>& orbits) {
    bool ok = true;
    constexpr VECTOR_ELT_T solve_kmax = K;
    auto result = eq::try_solve<SetOfStates> (
        fx.aut, solve_kmax, 2, 1, fx.all_inputs, fx.all_outputs, ios_precomputers::mona (),
        actioners::standard<state> (), input_pickers::critical ());
    auto standard_result = eq::try_solve<SetOfStates> (
        fx.aut, solve_kmax, 2, 1, fx.all_inputs, fx.all_outputs, ios_precomputers::standard (),
        actioners::standard<state> (), input_pickers::critical ());
    ok &= expect ("new solver attempted synthetic arbiter", result.attempted);
    ok &= expect ("new solver wins synthetic arbiter", result.win.has_value ());
    ok &= expect ("standard-precomputer solver attempted synthetic arbiter",
                  standard_result.attempted);
    ok &= expect ("standard-precomputer solver wins synthetic arbiter",
                  standard_result.win.has_value ());
    auto production_sweep =
        eq::solve_orbit_sweep<SetOfStates> (fx.aut, solve_kmax, 2, 1, fx.all_inputs,
                                            fx.all_outputs, G, L, actioners::standard<state> ());
    ok &= expect ("production sweep attempted synthetic arbiter", production_sweep.attempted);
    ok &= expect ("production sweep wins synthetic arbiter", production_sweep.win.has_value ());
    auto reference = reference_sweep (fx, L, orbits, 2, solve_kmax, 1);
    ok &= expect ("reference sweep wins synthetic arbiter", reference.has_value ());
    if (not result.win.has_value () or not standard_result.win.has_value () or
        not production_sweep.win.has_value () or not reference.has_value ())
      return false;

    const auto& [k, f] = *result.win;
    ok &= expect ("new/reference K agrees", k == reference->first);
    ok &= expect ("new/reference downset agrees", same_downset (f, reference->second));
    ok &= expect ("standard-precomputer/reference K agrees",
                  standard_result.win->first == reference->first);
    ok &= expect ("standard-precomputer/reference downset agrees",
                  same_downset (standard_result.win->second, reference->second));
    ok &= expect ("production sweep/reference K agrees",
                  production_sweep.win->first == reference->first);
    ok &= expect ("production sweep/reference downset agrees",
                  same_downset (production_sweep.win->second, reference->second));

    posets::utils::vector_mm<VECTOR_ELT_T> init (fx.aut->num_states (), -1);
    init[fx.aut->get_init_state_number ()] = 0;
    ok &= expect ("returned region contains initial state", f.contains (state (init)));
    for (const auto& phi : G.gens) {
      SetOfStates image = eq::permute (f, phi);
      ok &= expect ("returned region is generator invariant", same_downset (f, image));
    }

    const auto input_letters = eq::detail::enumerate_letters (fx.all_inputs, 1U << L.num_clients);
    const auto output_letters =
        eq::detail::enumerate_letters (fx.all_outputs, ACACIA_EQUIVARIANT_MAX_OUTPUT_LETTERS);
    std::vector<std::pair<bdd, std::vector<eq::transset>>> empty_itoios;
    auto actioner = actioners::standard<state>::make (fx.aut, empty_itoios, k);
    for (bdd input : input_letters) {
      const auto actions = direct_actions (fx.aut, input, output_letters);
      SetOfStates predecessors = eq::compute_T (f, actions, actioner, fx.aut->num_states ());
      ok &= expect ("returned region is closed for every concrete input",
                    eq::subset_of (f, predecessors));
    }
    return ok;
  }

  bool run_case (unsigned n, size_t bool_threshold) {
    posets::vectors::bool_threshold = bool_threshold;
    const fixture fx = make_aut (n);
    posets::vectors::bitset_threshold = fx.aut->num_states ();

    bool ok = true;
    const auto indexed = symmetry::analyze_indexed_aps (fx.aut, fx.all_inputs, fx.all_outputs);
    ok &= expect ("indexed AP families detected", not indexed.empty ());
    ok &= expect ("one indexed input family", indexed.input_families == 1);
    ok &= expect ("one indexed output family", indexed.output_families == 1);
    ok &= expect ("indexed client count", indexed.indices.size () == n);
    if (not ok)
      return false;

    const auto exhaustive = symmetry::detect (fx.aut, indexed);
    ok &= expect ("exhaustive full symmetric group detected", exhaustive.full_symmetric);
    ok &= expect ("exhaustive indices size", exhaustive.indices.size () == n);
    if (not ok)
      return false;

    const auto G = symmetry::detect_full_symmetric_generators (fx.aut, indexed);
    ok &= expect ("full symmetric group detected", G.full_symmetric);
    ok &= expect ("indices size", G.indices.size () == n);
    ok &= expect ("generator pair metadata size", G.gen_pairs.size () == G.gens.size ());
#if !ACACIA_EQUIVARIANT_EXHAUSTIVE_DETECT
    ok &= expect ("star generator count", G.gens.size () == n - 1);
#endif
    ok &= expect ("fast and exhaustive agree on indices", G.indices == exhaustive.indices);
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
    ok &= check_closure_oracle (rng, G, *L, fx.aut->num_states ());
    ok &= check_end_to_end (fx, G, *L, *orbits);
    return ok;
  }

  bool run_unreal_case () {
    constexpr unsigned n = 3;
    posets::vectors::bool_threshold = 1 + 2 * n;
    const fixture fx = make_aut (n, true);
    posets::vectors::bitset_threshold = fx.aut->num_states ();
    const auto indexed = symmetry::analyze_indexed_aps (fx.aut, fx.all_inputs, fx.all_outputs);
    const auto G = symmetry::detect_full_symmetric_generators (fx.aut, indexed);
    auto L = symmetry::compute_block_layout (G, fx.aut->num_states ());
    if (not expect ("unreal fixture symmetry detected", G.full_symmetric and L.has_value ()))
      return false;
    auto orbits = eq::build_orbits (fx.aut, fx.all_inputs, fx.all_outputs, G, *L);
    if (not expect ("unreal fixture orbits built", orbits.has_value ()))
      return false;

    auto result = eq::try_solve<SetOfStates> (
        fx.aut, 2, 2, 1, fx.all_inputs, fx.all_outputs, ios_precomputers::mona (),
        actioners::standard<state> (), input_pickers::critical ());
    auto production_sweep = eq::solve_orbit_sweep<SetOfStates> (
        fx.aut, 2, 2, 1, fx.all_inputs, fx.all_outputs, G, *L, actioners::standard<state> ());
    auto reference = reference_sweep (fx, *L, *orbits, 2, 2, 1);
    return expect ("new solver attempted unreal fixture", result.attempted) and
           expect ("new solver rejects unreal fixture", not result.win.has_value ()) and
           expect ("production sweep attempted unreal fixture", production_sweep.attempted) and
           expect ("production sweep rejects unreal fixture",
                   not production_sweep.win.has_value ()) and
           expect ("reference sweep rejects unreal fixture", not reference.has_value ());
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
  ok &= run_unreal_case ();

  posets::vectors::bool_threshold = old_bool_threshold;
  posets::vectors::bitset_threshold = old_bitset_threshold;

  if (ok) {
    std::cout << "ALL equivariant_pre oracle tests PASSED\n";
    return 0;
  }
  return 1;
}
