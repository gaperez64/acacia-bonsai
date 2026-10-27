// Sprint A stage A1: the pre-decoding semantic action quotient.
//
// The claim under test is narrow and exact.  `ios_precomputers::mona` decodes a
// transition set for every output path that reaches the state-variable
// frontier; `ios_precomputers::semantic_mona` decodes only the first path that
// reaches each residual BDD node.  BuDDy nodes are canonical, so the quotient's
// action list must be exactly the duplicate-free subsequence of the baseline's,
// in first-occurrence order -- not a set, not a sorted list.  Order is
// load-bearing because the input pickers scan actions in order and move a
// successful action to the front.

#include "ios_precomputers/semantic_mona.hh"

#include "actioners/standard.hh"
#include "aut_preprocessors/standard.hh"
#include "boolean_states/forward_saturation.hh"
#include "input_pickers/critical.hh"
#include "ios_precomputers/mona.hh"
#include "ios_precomputers/standard.hh"
#include "solver/create_automaton.hh"
#include "solver/forward_k_bounded_safety_aut.hh"
#include "solver/k_bounded_safety_aut.hh"
#include "utils/verbose.hh"

#include <iostream>
#include <map>
#include <ranges>
#include <set>
#include <spot/tl/parse.hh>
#include <string>
#include <vector>

#include <posets/downsets.hh>
#include <posets/vectors.hh>
#include <posets/vectors/traits.hh>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}

namespace posets::vectors {
  size_t bool_threshold = 0;
}

namespace {

  using state = posets::vectors::simd_vector_backed<VECTOR_ELT_T>;
  using SetOfStates = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<state>;
  using transset = std::vector<std::pair<unsigned, unsigned>>;

  constexpr VECTOR_ELT_T K = 3;

  int failures = 0;

  bool expect (const std::string& what, bool condition) {
    if (condition)
      return true;
    std::cerr << "FAIL: " << what << "\n";
    ++failures;
    return false;
  }

  struct fixture {
      spot::twa_graph_ptr aut;
      bdd all_inputs = bddtrue;
      bdd all_outputs = bddtrue;
  };

  /// Duplication does NOT come from unused output propositions: the BDD does
  /// not branch on a variable that occurs in no guard, so a don't-care output
  /// costs no extra path.  It comes from DISJUNCTIVE guards, where two
  /// different output branches lead to the same endpoint relation and the
  /// descent reaches the same residual node twice.
  ///
  /// `disjunctive` guards the grant edge with `g_0 | g_1 | ...`, so every
  /// satisfying branch reaches one residual relation.
  /// `partitioned` guards distinct destinations with `g_0` and `!g_0`, so every
  /// branch reaches a different one and there is nothing to quotient.
  fixture make_aut (unsigned outputs, bool disjunctive) {
    fixture fx;
    fx.aut = spot::make_twa_graph (spot::make_bdd_dict ());
    fx.aut->set_acceptance (1, spot::acc_cond::acc_code::buchi ());
    fx.aut->prop_state_acc (true);
    fx.aut->new_states (3);
    fx.aut->set_init_state (0);

    const int rv = fx.aut->register_ap ("r");
    fx.all_inputs &= bdd_ithvar (rv);
    const bdd r = bdd_ithvar (rv);

    std::vector<bdd> g;
    for (unsigned i = 0; i < outputs; ++i) {
      const int gv = fx.aut->register_ap ("g_" + std::to_string (i));
      fx.all_outputs &= bdd_ithvar (gv);
      g.push_back (bdd_ithvar (gv));
    }

    fx.aut->new_acc_edge (0, 0, !r, false);
    if (disjunctive) {
      bdd any = bddfalse;
      for (const bdd& gi : g)
        any |= gi;
      fx.aut->new_acc_edge (0, 1, r & any, false);
      fx.aut->new_acc_edge (0, 2, r & !any);
    }
    else {
      // One destination per output valuation of g_0, and a further split on
      // g_1 when it exists, so the relations really are pairwise different.
      fx.aut->new_acc_edge (0, 1, r & g[0], false);
      fx.aut->new_acc_edge (0, 2, r & !g[0]);
      if (outputs > 1)
        fx.aut->new_acc_edge (0, 0, r & g[0] & g[1], false);
    }
    fx.aut->new_acc_edge (1, 0, bddtrue, false);
    fx.aut->new_acc_edge (2, 2, bddtrue);
    return fx;
  }

  template <typename Precomputer>
  auto precompute (const fixture& fx) {
    return (Precomputer::template make<spot::twa_graph_ptr, transset> (
        fx.aut, fx.all_inputs, fx.all_outputs)) ();
  }

  void check_endpoint_order () {
    for (unsigned n : {1, 2, 3, 4, 5, 8, 9, 257}) {
      fixture fx;
      fx.aut = spot::make_twa_graph (spot::make_bdd_dict ());
      fx.aut->new_states (n);
      // Exercise variable zero as well as unused APs before the state variables.
      if (n > 3)
        fx.all_inputs = bdd_ithvar (fx.aut->register_ap ("unused"));
      transset expected;
      for (unsigned from = 0; from < n; ++from)
        for (unsigned to = 0; to < n; ++to)
          if (n < 257 or (from % 128 == 0 and to % 128 == 0))
            expected.emplace_back (from, to);
      for (auto [from, to] : std::views::reverse (expected))
        fx.aut->new_edge (from, to, bddtrue);

      const auto full = precompute<ios_precomputers::mona> (fx);
      const auto quot = precompute<ios_precomputers::semantic_mona> (fx);
      expect ("ordered endpoints at " + std::to_string (n) + " states",
              full == quot and full.size () == 1 and full.front ().first == bddtrue and
                  full.front ().second == std::list<transset> {expected});
    }
    fixture empty;
    empty.aut = spot::make_twa_graph (spot::make_bdd_dict ());
    empty.aut->new_state ();
    // No enabled transitions still leaves a legal response reaching bottom (#335).
    const auto full = precompute<ios_precomputers::mona> (empty);
    const auto quot = precompute<ios_precomputers::semantic_mona> (empty);
    expect ("false endpoint relation",
            full == quot and full.size () == 1 and full.front ().first == bddtrue and
                full.front ().second == std::list<transset> {transset {}});
  }

  void check_empty_responses () {
    for (unsigned shape = 0; shape < 3; ++shape) {
      fixture fx;
      fx.aut = spot::make_twa_graph (spot::make_bdd_dict ());
      fx.aut->new_state ();
      fx.all_inputs = bdd_ithvar (fx.aut->register_ap ("i"));
      fx.all_outputs = bdd_ithvar (fx.aut->register_ap ("c"));
      if (shape == 1)
        fx.aut->new_edge (0, 0, fx.all_inputs & fx.all_outputs);
      if (shape == 2)
        fx.aut->new_edge (0, 0, fx.all_inputs ^ fx.all_outputs);
      const auto responses = [] (const auto& table, bdd input) {
        std::set<transset> result;
        for (const auto& [cube, actions] : table)
          if ((cube & input) != bddfalse)
            for (const auto& action : actions)
              result.insert (action);
        return result;
      };
      const auto standard = precompute<ios_precomputers::standard> (fx);
      const auto full = precompute<ios_precomputers::mona> (fx);
      const auto computer = ios_precomputers::semantic_mona::make<spot::twa_graph_ptr, transset> (
          fx.aut, fx.all_inputs, fx.all_outputs);
      const auto symbolic = computer.symbolic ();
      const auto explicit_table = computer.explicit_letters ();
      expect ("empty responses: explicit admission", explicit_table.has_value ());
      for (bdd input : {fx.all_inputs, !fx.all_inputs}) {
        const auto expected = responses (standard, input);
        expect ("empty responses: reference has an empty action", expected.contains (transset {}));
        expect ("empty responses: MONA matches standard", responses (full, input) == expected);
        expect ("empty responses: semantic symbolic matches standard",
                responses (symbolic, input) == expected);
        if (explicit_table)
          expect ("empty responses: semantic explicit matches standard",
                  responses (*explicit_table, input) == expected);
      }
    }
  }

  void check_explicit_letters () {
    auto check = [&] (const std::string& name, const fixture& fx, bool admitted = true) {
      auto computer = ios_precomputers::detail::mona<spot::twa_graph_ptr, transset, true> (
          fx.aut, fx.all_inputs, fx.all_outputs);
      const auto letters = computer.explicit_letters ();
      expect (name + ": admission", letters.has_value () == admitted);
      const auto baseline = computer.symbolic ();
      expect (name + ": ordered identity", computer () == baseline);
      if (letters)
        expect (name + ": explicit ordered identity", *letters == baseline);
    };
    fixture fx;
    fx.aut = spot::make_twa_graph (spot::make_bdd_dict ());
    fx.aut->new_states (3);
    fx.all_inputs &= bdd_ithvar (fx.aut->register_ap ("unused-input"));
    const bdd x = bdd_ithvar (fx.aut->register_ap ("x"));
    const bdd y = bdd_ithvar (fx.aut->register_ap ("y"));
    fx.all_inputs &= x & y;
    fx.all_outputs &= bdd_ithvar (fx.aut->register_ap ("unused-output"));
    fx.aut->new_edge (0, 1, bddtrue);
    fx.aut->new_edge (0, 2, x | y);
    fx.aut->new_edge (0, 2, x);
    fx.aut->new_edge (1, 0, bddfalse);
    check ("unused APs, overlapping guards and conditional input split", fx);
    expect ("conditional split keeps three cubes",
            precompute<ios_precomputers::semantic_mona> (fx).size () == 3);

    fixture different_outputs = make_aut (2, false);
    check ("output and endpoint order", different_outputs);
    fixture duplicating = make_aut (3, true);
    check ("first occurrence of disjunctive outputs", duplicating);

    fixture empty;
    empty.aut = spot::make_twa_graph (spot::make_bdd_dict ());
    empty.aut->new_state ();
    check ("empty alphabet and relation", empty);
    empty.aut->new_edge (0, 0, bddtrue);
    check ("true guard and empty letter", empty);
    const int unused0 = empty.aut->register_ap ("unused0");
    const int unused1 = empty.aut->register_ap ("unused1");
    bdd_swapvar (unused0, unused1);
    check ("reordered BDD fallback", empty, false);
    bdd_swapvar (unused0, unused1);

    fixture large;
    large.aut = spot::make_twa_graph (spot::make_bdd_dict ());
    large.aut->new_state ();
    for (unsigned j = 0; j < 12; ++j)
      large.all_inputs &= bdd_ithvar (large.aut->register_ap ("i" + std::to_string (j)));
    large.aut->new_edge (0, 0, large.all_inputs);
    check ("twelve active APs", large);
    for (unsigned j = 0; j < 2200; ++j)
      large.aut->new_edge (0, 0, bddtrue);
    check ("endpoint byte bound fallback", large, false);
    large.all_inputs &= bdd_ithvar (large.aut->register_ap ("thirteenth"));
    large.aut->new_edge (0, 0, large.all_inputs);
    check ("active AP bound fallback", large, false);
  }

  void check_large_global_pool () {
    const auto dict = spot::make_bdd_dict ();
    int owner = 0;
    dict->register_anonymous_variables (1100, &owner);
    dict->unregister_all_my_variables (&owner);
    const auto aut = spot::make_twa_graph (dict);
    aut->new_state ();
    const bdd x = bdd_ithvar (aut->register_ap ("x"));
    const bdd y = bdd_ithvar (aut->register_ap ("y"));
    aut->new_edge (0, 0, x | y);
    auto computer = ios_precomputers::detail::mona<spot::twa_graph_ptr, transset, true> (
        aut, x & y, bddtrue);
    expect ("large global pool: symbolic fallback", not computer.explicit_letters ());
    const auto baseline = computer.symbolic ();
    expect ("large global pool: includes the empty input cofactor", baseline.size () == 3);
    expect ("large global pool: ordered identity", computer () == baseline);
  }

  /// Every input class of `full`, with its transition sets, keeping the first
  /// occurrence of each distinct set and preserving order.
  std::vector<std::vector<transset>> dedup_first (
      const std::list<std::pair<bdd, std::list<transset>>>& full) {
    std::vector<std::vector<transset>> out;
    for (const auto& [input, sets] : full) {
      (void) input;
      std::vector<transset> kept;
      for (const auto& ts : sets)
        if (std::find (kept.begin (), kept.end (), ts) == kept.end ())
          kept.push_back (ts);
      out.push_back (std::move (kept));
    }
    return out;
  }

  std::vector<std::vector<transset>> as_vectors (
      const std::list<std::pair<bdd, std::list<transset>>>& full) {
    std::vector<std::vector<transset>> out;
    for (const auto& [input, sets] : full) {
      (void) input;
      out.emplace_back (sets.begin (), sets.end ());
    }
    return out;
  }

  std::vector<bdd> input_cubes (const std::list<std::pair<bdd, std::list<transset>>>& full) {
    std::vector<bdd> out;
    for (const auto& [input, sets] : full) {
      (void) sets;
      out.push_back (input);
    }
    return out;
  }

  size_t total_actions (const std::vector<std::vector<transset>>& per_input) {
    size_t n = 0;
    for (const auto& sets : per_input)
      n += sets.size ();
    return n;
  }

  void check_quotient_is_the_first_occurrence_subsequence (const std::string& what,
                                                           const fixture& fx) {
    const auto full = precompute<ios_precomputers::mona> (fx);
    const auto quotiented = precompute<ios_precomputers::semantic_mona> (fx);

    expect (what + ": same input classes",
            input_cubes (full).size () == input_cubes (quotiented).size ());
    expect (what + ": input cubes unchanged and in the same order",
            input_cubes (full) == input_cubes (quotiented));

    const auto expected = dedup_first (full);
    const auto actual = as_vectors (quotiented);
    expect (what + ": quotient equals the first-occurrence dedup of the baseline",
            expected == actual);
    expect (what + ": quotient never grows the action list",
            total_actions (actual) <= total_actions (as_vectors (full)));
  }

  /// Exhaustive rank vectors over {-1, 0, .. K} for the automaton's states.
  std::vector<posets::utils::vector_mm<VECTOR_ELT_T>> all_vectors (unsigned states) {
    std::vector<posets::utils::vector_mm<VECTOR_ELT_T>> out;
    const int levels = K + 2;  // -1 .. K
    long total = 1;
    for (unsigned i = 0; i < states; ++i)
      total *= levels;
    for (long code = 0; code < total; ++code) {
      posets::utils::vector_mm<VECTOR_ELT_T> v (states, 0);
      long rest = code;
      for (unsigned i = 0; i < states; ++i) {
        v[i] = (VECTOR_ELT_T) ((rest % levels) - 1);
        rest /= levels;
      }
      out.push_back (std::move (v));
    }
    return out;
  }

  /// The controller predecessor of one state under one input: the union over
  /// that input's actions of the backward image.  This is the expression the
  /// quotient must preserve, and it is a union, so duplicate actions are
  /// exactly the terms that contribute nothing.
  SetOfStates union_of_backward_images (const fixture& fx,
                                        const std::list<transset>& actions,
                                        const posets::utils::vector_mm<VECTOR_ELT_T>& v) {
    auto make_actioner = [&] (const auto& itoios) {
      return actioners::standard<state>::make (fx.aut, itoios, K);
    };
    std::list<std::pair<bdd, std::list<transset>>> one {{bddtrue, actions}};
    auto actioner = make_actioner (one);
    std::vector<state> images;
    for (const auto& avec : actioner.actions ().front ().second)
      images.push_back (actioner.apply (state (posets::utils::vector_mm<VECTOR_ELT_T> (v)), avec,
                                        actioners::direction::backward));
    return SetOfStates (std::move (images));
  }

  void check_cpre_agrees_exhaustively (const std::string& what, const fixture& fx) {
    posets::vectors::bool_threshold = fx.aut->num_states ();
    const auto full = precompute<ios_precomputers::mona> (fx);
    const auto quotiented = precompute<ios_precomputers::semantic_mona> (fx);

    auto lhs = full.begin ();
    auto rhs = quotiented.begin ();
    bool agreed = true;
    for (; lhs != full.end () and rhs != quotiented.end (); ++lhs, ++rhs)
      for (const auto& v : all_vectors (fx.aut->num_states ())) {
        auto a = union_of_backward_images (fx, lhs->second, v);
        auto b = union_of_backward_images (fx, rhs->second, v);
        // Downsets are equal iff each contains the other's generators.
        for (const auto& m : a)
          agreed = agreed and b.contains (m);
        for (const auto& m : b)
          agreed = agreed and a.contains (m);
      }
    expect (what + ": union over outputs is unchanged on every rank vector", agreed);
  }

  template <typename Precomputer>
  std::optional<VECTOR_ELT_T> solve (const fixture& fx) {
    posets::vectors::bool_threshold = fx.aut->num_states ();
    k_bounded_safety_aut_detail<SetOfStates, Precomputer, actioners::standard<state>,
                                input_pickers::critical>
        solver (fx.aut, 1, K, 1, fx.all_inputs, fx.all_outputs, Precomputer (),
                actioners::standard<state> (), input_pickers::critical ());
    auto result = solver.solve ();
    if (not result.has_value ())
      return std::nullopt;
    return result->first;
  }

  void check_verdicts_agree (const std::string& what, const fixture& fx) {
    const auto base = solve<ios_precomputers::mona> (fx);
    const auto quot = solve<ios_precomputers::semantic_mona> (fx);
    expect (what + ": same realizability verdict", base.has_value () == quot.has_value ());
    if (base.has_value () and quot.has_value ())
      expect (what + ": same winning bound", *base == *quot);
  }

  template <typename Precomputer>
  void check_empty_response_verdicts (const fixture& fx, const std::string& what) {
    for (VECTOR_ELT_T bound : {1, 2, 5}) {
      const Precomputer precomputer;
      const actioners::standard<state> actioner;
      const input_pickers::critical picker;
      k_bounded_safety_aut_detail<SetOfStates, Precomputer, actioners::standard<state>,
                                  input_pickers::critical>
          backward (fx.aut, bound, bound, 1, fx.all_inputs, fx.all_outputs, precomputer, actioner,
                    picker);
      expect (what + ": backward REALIZABLE at K=" + std::to_string (bound),
              backward.solve ().has_value ());
      acacia::solver_detail::forward_k_bounded_safety_aut_detail<
          SetOfStates, Precomputer, actioners::standard<state>, input_pickers::critical>
          forward (fx.aut, bound, bound, 1, fx.all_inputs, fx.all_outputs, precomputer, actioner,
                   picker);
      expect (what + ": forward REALIZABLE at K=" + std::to_string (bound),
              forward.solve ().has_value () and not forward.should_fallback_to_backward ());
    }
  }

  void check_ltl_empty_responses () {
    for (const char* text : {"F (c <-> i) | G F c", "F (c <-> i)"}) {
      auto parsed = spot::parse_infix_psl (text);
      if (not expect ("parse empty-response regression", parsed.f and parsed.errors.empty ()))
        continue;
      const auto dict = spot::make_bdd_dict ();
      fixture fx;
      fx.aut = spot::make_twa_graph (dict);
      fx.all_inputs = bdd_ithvar (fx.aut->register_ap ("i"));
      fx.all_outputs = bdd_ithvar (fx.aut->register_ap ("c"));
      auto options = acacia::translation::make_options ();
      spot::translator translator (dict, &options);
      acacia::translation::validate_options (options);
      auto negated = spot::formula::Not (parsed.f);
      fx.aut = create_automaton (negated, translator);
      aut_preprocessors::standard::make (fx.aut, fx.all_inputs, fx.all_outputs, 5) ();
      posets::vectors::bool_threshold = boolean_states::forward_saturation::make (fx.aut, 5) ();
      // Call the games directly so deterministic CLI shortcuts cannot mask the missing action.
      check_empty_response_verdicts<ios_precomputers::standard> (
          fx, std::string (text) + ": standard");
      check_empty_response_verdicts<ios_precomputers::mona> (fx, std::string (text) + ": mona");
      check_empty_response_verdicts<ios_precomputers::semantic_mona> (
          fx, std::string (text) + ": semantic_mona");
    }
  }

}  // namespace

int main () {
  check_endpoint_order ();
  check_empty_responses ();
  check_ltl_empty_responses ();
  check_explicit_letters ();
  // A disjunctive grant: three output branches reach one endpoint relation.
  const fixture duplicating = make_aut (3, true);
  // Partitioned destinations: every branch reaches a different relation.
  const fixture distinct = make_aut (1, false);
  // Both shapes at once, on a wider output alphabet.
  const fixture with_tail = make_aut (3, false);

  check_quotient_is_the_first_occurrence_subsequence ("duplicating", duplicating);
  check_quotient_is_the_first_occurrence_subsequence ("distinct", distinct);
  check_quotient_is_the_first_occurrence_subsequence ("with-tail", with_tail);

  // The duplicating fixture must actually duplicate, or the test above is
  // vacuous and would keep passing if the quotient stopped working.
  {
    const auto full = as_vectors (precompute<ios_precomputers::mona> (duplicating));
    const auto quot = as_vectors (precompute<ios_precomputers::semantic_mona> (duplicating));
    std::cout << "duplicating: " << total_actions (full) << " -> " << total_actions (quot)
              << " actions\n";
    expect ("duplicating: the baseline really does decode duplicates",
            total_actions (full) > total_actions (quot));
    const auto same = as_vectors (precompute<ios_precomputers::mona> (distinct));
    const auto same_quot = as_vectors (precompute<ios_precomputers::semantic_mona> (distinct));
    std::cout << "distinct: " << total_actions (same) << " -> " << total_actions (same_quot)
              << " actions\n";
    expect ("distinct: nothing is removed when every relation differs",
            total_actions (same) == total_actions (same_quot));
  }

  check_cpre_agrees_exhaustively ("duplicating", duplicating);
  check_cpre_agrees_exhaustively ("distinct", distinct);
  check_cpre_agrees_exhaustively ("with-tail", with_tail);

  check_verdicts_agree ("duplicating", duplicating);
  check_verdicts_agree ("distinct", distinct);
  check_verdicts_agree ("with-tail", with_tail);

  check_large_global_pool ();

  if (failures != 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "semantic-mona: all checks passed\n";
  return 0;
}
