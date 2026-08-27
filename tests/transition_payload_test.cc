#include "actioners/standard.hh"
#include "ios_precomputers/standard.hh"
#include "solver/transition_payload.hh"

#include <bddx.h>
#include <spot/twa/twagraph.hh>

#include <algorithm>
#include <iostream>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace posets::vectors {
  size_t bool_threshold = 0;
}

namespace {
  using rank_state = posets::utils::vector_mm<VECTOR_ELT_T>;
  using source_increment = std::pair<unsigned, bool>;

  struct test_automaton {
      spot::twa_graph_ptr graph;
      std::vector<bool> accepting_states;

      unsigned num_states () const { return graph->num_states (); }
      auto out (unsigned state) const { return graph->out (state); }
      bool state_is_accepting (unsigned state) const {
        return accepting_states[state];
      }
  };

  using test_automaton_ptr = std::shared_ptr<test_automaton>;

  bool expect (std::string_view label, bool condition) {
    if (condition)
      return true;
    std::cerr << label << ": failed\n";
    return false;
  }

  template <typename Action>
  bool expect_action (std::string_view label, const Action& action,
                      std::vector<source_increment> expected) {
    std::vector<source_increment> actual (action.begin (), action.end ());
    std::sort (actual.begin (), actual.end ());
    std::sort (expected.begin (), expected.end ());
    if (actual == expected)
      return true;

    std::cerr << label << ": expected";
    for (const auto& [source, increment] : expected)
      std::cerr << " (" << source << ',' << increment << ')';
    std::cerr << "; got";
    for (const auto& [source, increment] : actual)
      std::cerr << " (" << source << ',' << increment << ')';
    std::cerr << '\n';
    return false;
  }

  bool expect_rank (std::string_view label, const rank_state& actual,
                    const std::vector<int>& expected) {
    if (actual.size () == expected.size ()) {
      bool equal = true;
      for (size_t i = 0; i < expected.size (); ++i)
        equal &= actual[i] == expected[i];
      if (equal)
        return true;
    }

    std::cerr << label << ": expected";
    for (int rank : expected)
      std::cerr << ' ' << rank;
    std::cerr << "; got";
    for (size_t i = 0; i < actual.size (); ++i)
      std::cerr << ' ' << static_cast<int> (actual[i]);
    std::cerr << '\n';
    return false;
  }

  test_automaton_ptr make_test_automaton () {
    auto graph = spot::make_twa_graph (spot::make_bdd_dict ());
    const auto accepting = graph->set_buchi ();
    graph->new_states (5);
    graph->set_init_state (0);

    // Parallel edges with different acceptance.
    graph->new_edge (0, 1, bddtrue);
    graph->new_edge (0, 1, bddtrue, accepting);

    // An accepting self-loop.
    graph->new_edge (1, 1, bddtrue, accepting);

    // An accepting edge into a state whose state acceptance is false.
    graph->new_edge (2, 3, bddtrue, accepting);
    graph->new_edge (3, 3, bddtrue);

    // Several differently accepting incoming edges to state 4.
    graph->new_edge (0, 4, bddtrue, accepting);
    graph->new_edge (1, 4, bddtrue);
    graph->new_edge (2, 4, bddtrue, accepting);
    graph->new_edge (4, 4, bddtrue);

    return std::make_shared<test_automaton> (
        test_automaton {graph, {false, true, false, false, false}});
  }

  bool helpers_are_selected_correctly (const test_automaton_ptr& aut) {
    struct edge {
        spot::acc_cond::mark_t acc;
    };

    const edge nonaccepting {};
    const edge accepting {spot::acc_cond::mark_t {0}};
    const auto into_accepting_state =
        acacia::transitions::make (7, 1, nonaccepting);
    const auto accepting_into_nonaccepting_state =
        acacia::transitions::make (2, 3, accepting);

    bool ok = true;
    ok &= expect ("helper endpoints",
                  acacia::transitions::source (into_accepting_state) == 7 and
                      acacia::transitions::dest (into_accepting_state) == 1);

#if ACACIA_TRANSITION_ACCEPTANCE
    static_assert (std::is_same_v<acacia::transitions::element,
                                  acacia::transitions::triple>);
    ok &= expect ("triple ignores accepting destination state",
                  not acacia::transitions::increment (into_accepting_state, aut));
    ok &= expect ("triple uses accepting incoming edge",
                  acacia::transitions::increment (
                      accepting_into_nonaccepting_state, aut));
#else
    static_assert (std::is_same_v<acacia::transitions::element,
                                  std::pair<int, int>>);
    ok &= expect ("pair uses accepting destination state",
                  acacia::transitions::increment (into_accepting_state, aut));
    ok &= expect ("pair ignores accepting incoming edge",
                  not acacia::transitions::increment (
                      accepting_into_nonaccepting_state, aut));
#endif
    return ok;
  }

  bool action_vector_and_apply () {
    const auto aut = make_test_automaton ();
    bool ok = helpers_are_selected_correctly (aut);

    auto inputs_to_ios =
        ios_precomputers::standard::make (aut, bddtrue, bddtrue) ();
    posets::vectors::bool_threshold = aut->num_states ();
    auto rank_action = actioners::standard<rank_state>::make (
        aut, inputs_to_ios, static_cast<VECTOR_ELT_T> (9));

    const auto& input_actions = rank_action.actions ();
    if (input_actions.size () != 1 or input_actions.front ().second.size () != 1) {
      std::cerr << "expected one input and one IO action\n";
      return false;
    }
    const auto& action_vec = input_actions.front ().second.front ();

#if ACACIA_TRANSITION_ACCEPTANCE
    ok &= expect_action ("parallel edges and accepting self-loop", action_vec[1],
                         {{0, false}, {0, true}, {1, true}});
    ok &= expect_action ("accepting edge into nonaccepting state", action_vec[3],
                         {{2, true}, {3, false}});
    ok &= expect_action ("mixed incoming edge acceptance", action_vec[4],
                         {{0, true}, {1, false}, {2, true}, {4, false}});
#else
    ok &= expect_action ("parallel edges and state fallback", action_vec[1],
                         {{0, true}, {0, true}, {1, true}});
    ok &= expect_action ("nonaccepting destination fallback", action_vec[3],
                         {{2, false}, {3, false}});
    ok &= expect_action ("mixed incoming edges use destination fallback", action_vec[4],
                         {{0, false}, {1, false}, {2, false}, {4, false}});
#endif

    const rank_state ranks {
        static_cast<VECTOR_ELT_T> (6), static_cast<VECTOR_ELT_T> (2),
        static_cast<VECTOR_ELT_T> (7), static_cast<VECTOR_ELT_T> (1),
        static_cast<VECTOR_ELT_T> (0)};
    const auto forward = rank_action.apply (
        ranks, action_vec, actioners::direction::forward);
    const auto backward = rank_action.apply (
        ranks, action_vec, actioners::direction::backward);

#if ACACIA_TRANSITION_ACCEPTANCE
    ok &= expect_rank ("transition-acceptance forward ranks", forward,
                       {-1, 7, -1, 8, 8});
    ok &= expect_rank ("transition-acceptance backward ranks", backward,
                       {-1, 0, -1, 1, 0});
#else
    ok &= expect_rank ("state-acceptance forward ranks", forward,
                       {-1, 7, -1, 7, 7});
    ok &= expect_rank ("state-acceptance backward ranks", backward,
                       {0, 0, 0, 1, 0});
#endif
    return ok;
  }
}

int main () {
  return action_vector_and_apply () ? 0 : 1;
}
