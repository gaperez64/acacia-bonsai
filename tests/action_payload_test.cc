#include "action_payload_reference.hh"
#include "actioners/standard.hh"
#include "input_pickers/critical.hh"
#include "input_pickers/critical_fullrnd.hh"
#include "input_pickers/critical_pq.hh"
#include "input_pickers/critical_rnd.hh"
#include "ios_precomputers/mona.hh"
#include "ios_precomputers/semantic_mona.hh"
#include "solver/solve_game_impl.hh"
#include <type_traits>
#include <unordered_map>

#include <functional>
#include <iostream>
#include <random>
#include <spot/twaalgos/hoa.hh>
#include <sstream>
#include <stdexcept>
#include <string>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}
namespace posets::vectors {
  size_t bool_threshold = 0;
}

namespace {
  using state = posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>;
  using downset = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<state>;
  using transitions = std::vector<std::pair<unsigned, unsigned>>;
  using input_table = std::list<std::pair<bdd, std::list<transitions>>>;
  using old_actioner =
      actioners::payload_reference::detail::standard<state, spot::twa_graph_ptr, input_table>;
  using new_actioner = actioners::detail::standard<state, spot::twa_graph_ptr, input_table>;

  void check (bool ok, const char* message) {
    if (not ok)
      throw std::runtime_error (message);
  }

  struct game {
      spot::twa_graph_ptr aut = spot::make_twa_graph (spot::make_bdd_dict ());
      bdd inputs = bddtrue, outputs = bddtrue;
  };

  game generated (unsigned seed) {
    game g;
    std::mt19937 rng (seed);
    const unsigned n = 1 + seed % 9, ni = seed % 4, no = (seed / 4) % 4;
    g.aut->new_states (n);
    g.aut->set_init_state (seed % n);
    g.aut->set_acceptance (1, spot::acc_cond::acc_code::buchi ());
    g.aut->prop_state_acc (true);
    std::vector<bdd> aps;
    for (unsigned i = 0; i < ni + no; ++i) {
      const bdd ap = bdd_ithvar (g.aut->register_ap ("p" + std::to_string (i)));
      aps.push_back (ap);
      (i < ni ? g.inputs : g.outputs) &= ap;
    }
    for (unsigned p = 0; p < n; ++p)
      for (unsigned q = 0; q < n; ++q) {
        bdd guard = bddfalse;
        for (unsigned valuation = 0; valuation < (1u << aps.size ()); ++valuation)
          if (rng () % 3 == 0) {
            bdd cube = bddtrue;
            for (unsigned i = 0; i < aps.size (); ++i)
              cube &= (valuation & (1u << i)) ? aps[i] : !aps[i];
            guard |= cube;
          }
        if (guard != bddfalse)
          g.aut->new_acc_edge (p, q, guard, p % 3 == 0);
      }
    return g;
  }

  void number (std::string& bytes, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i)
      bytes.push_back (static_cast<char> (value >> (8 * i)));
  }

  template <typename Table>
  void serialize (std::string& bytes, const Table& table) {
    number (bytes, table.size ());
    for (const auto& [input, actions] : table) {
      number (bytes, input.id ());
      number (bytes, actions.size ());
      for (const auto& action : actions) {
        number (bytes, action.size ());
        for (const auto& row : action) {
          number (bytes, row.size ());
          for (const auto& [source, increment] : row) {
            number (bytes, source);
            number (bytes, increment);
          }
        }
      }
    }
  }

  template <typename Actual>
  struct recording_actioner {
      using action_vec = typename Actual::action_vec;
      Actual actual;
      std::string& bytes;
      std::unordered_map<const void*, size_t> ids;
      recording_actioner (Actual actual, std::string& bytes)
        : actual {std::move (actual)},
          bytes {bytes} {
        size_t id = 0;
        for (const auto& [input, actions] : this->actual.actions ())
          for (const auto& action : actions)
            ids.emplace (&action, id++);
      }
      auto& actions () { return actual.actions (); }
      void setK (VECTOR_ELT_T k) { actual.setK (k); }
      auto apply (const state& rank, const action_vec& action, actioners::direction direction) {
        number (bytes, ids.at (&action));
        number (bytes, direction == actioners::direction::forward);
        for (auto x : rank)
          number (bytes, x);
        auto result = actual.apply (rank, action, direction);
        for (auto x : result)
          number (bytes, x);
        return result;
      }
  };

  // Access the test instantiations' private engines without a release mutation hook.
  template <typename Picker>
  struct rng_tag {
      using member = std::mt19937 Picker::*;
      friend member rng_member (rng_tag);
  };
  template <typename Tag, typename Tag::member Member>
  struct rng_access {
      friend typename Tag::member rng_member (Tag) { return Member; }
  };
  using old_table = old_actioner::input_and_actions_set;
  using new_table = new_actioner::input_and_actions_set;
  using old_random =
      input_pickers::detail::critical_rnd<old_table, recording_actioner<old_actioner>>;
  using new_random =
      input_pickers::detail::critical_rnd<new_table, recording_actioner<new_actioner>>;
  using old_full_random =
      input_pickers::detail::critical_fullrnd<old_table, recording_actioner<old_actioner>>;
  using new_full_random =
      input_pickers::detail::critical_fullrnd<new_table, recording_actioner<new_actioner>>;
  using old_priority =
      input_pickers::detail::critical_pq<old_table, recording_actioner<old_actioner>>;
  using new_priority =
      input_pickers::detail::critical_pq<new_table, recording_actioner<new_actioner>>;
  template struct rng_access<rng_tag<old_random>, &old_random::gen>;
  template struct rng_access<rng_tag<new_random>, &new_random::gen>;
  template struct rng_access<rng_tag<old_full_random>, &old_full_random::gen>;
  template struct rng_access<rng_tag<new_full_random>, &new_full_random::gen>;
  template struct rng_access<rng_tag<old_priority>, &old_priority::gen>;
  template struct rng_access<rng_tag<new_priority>, &new_priority::gen>;

  template <typename Maker, typename Actual>
  auto trace (const game& g, Actual actual) {
    std::string bytes;
    recording_actioner recorder (std::move (actual), bytes);
    auto picker = Maker::make (recorder.actions (), recorder);
    std::mt19937 rng (871);
    for (unsigned i = 0; i < 128; ++i) {
      const VECTOR_ELT_T k = 1 + i % 4;
      recorder.setK (k);
      posets::utils::vector_mm<VECTOR_ELT_T> v (g.aut->num_states ());
      for (unsigned p = 0; p < v.size (); ++p)
        v[p] = p < posets::vectors::bool_threshold ? rng () % (k + 1) - 1 : rng () % 2 - 1;
      downset region {state (v)};
      auto selected = picker (region);
      number (bytes, selected.has_value ());
      if (selected) {
        number (bytes, selected->get ().first.id ());
        for (const auto& action : selected->get ().second)
          recorder.apply (state (v), action, actioners::direction::backward);
      }
      serialize (bytes, recorder.actions ());
      if constexpr (not std::is_same_v<Maker, input_pickers::critical>) {
        std::ostringstream engine;
        engine << (picker.*rng_member (rng_tag<decltype (picker)> {}));
        bytes += engine.str ();
      }
    }
    return bytes;
  }

  std::string* active_trace = nullptr;
  template <typename Maker>
  struct traced_factory {
      template <typename Aut, typename Inputs>
      static auto make (const Aut& aut, const Inputs& inputs, VECTOR_ELT_T k) {
        return recording_actioner (Maker::make (aut, inputs, k), *active_trace);
      }
  };

  template <typename Maker, bool Forward>
  auto solve_trace (const game& g) {
    using factory = traced_factory<Maker>;
    using solver_type = std::conditional_t<
        Forward,
        acacia::solver_detail::forward_k_bounded_safety_aut_detail<
            downset, ios_precomputers::mona, factory, input_pickers::critical_fullrnd>,
        k_bounded_safety_aut_detail<downset, ios_precomputers::mona, factory,
                                    input_pickers::critical_fullrnd>>;
    std::string bytes;
    active_trace = &bytes;
    ios_precomputers::mona precomputer;
    factory actioner;
    input_pickers::critical_fullrnd picker;
    solver_type solver (g.aut, 1, 4, 1, g.inputs, g.outputs, precomputer, actioner, picker);
    std::ostringstream execution;
    struct capture {
        std::streambuf* previous;
        unsigned verbose;
        explicit capture (std::ostream& out)
          : previous {utils::vout.rdbuf (out.rdbuf ())},
            verbose {utils::verbose} {
          utils::verbose = 3;
        }
        ~capture () {
          utils::vout.rdbuf (previous);
          utils::verbose = verbose;
        }
    } captured (execution);
    auto result = solver.solve ();
    number (bytes, result.has_value ());
    if (result) {
      number (bytes, result->first);
      for (const auto& maximum : result->second)
        for (auto x : maximum)
          number (bytes, x);
      const auto strategy = acacia::solver_detail::post_real<downset> (std::move (result), true,
                                                                       g.aut, g.inputs, g.outputs);
      check (strategy.has_value (), "strategy extraction declined");
      spot::print_hoa (execution, *strategy);
    }
    check (not execution.str ().empty (), "solver trace was compiled out");
    bytes += execution.str ();
    active_trace = nullptr;
    return bytes;
  }

  void compare (const game& g, const input_table& inputs) {
    auto old = actioners::payload_reference::standard<state>::make (g.aut, inputs, 3);
    auto compact = actioners::standard<state>::make (g.aut, inputs, 3);
    std::string a, b;
    serialize (a, old.actions ());
    serialize (b, compact.actions ());
    check (a == b, "action order, labels, rows or endpoints differ");
    check (trace<input_pickers::critical> (g, old) == trace<input_pickers::critical> (g, compact),
           "critical picker trace differs");
    check (trace<input_pickers::critical_rnd> (g, old) ==
               trace<input_pickers::critical_rnd> (g, compact),
           "random picker trace differs");
    check (trace<input_pickers::critical_fullrnd> (g, old) ==
               trace<input_pickers::critical_fullrnd> (g, compact),
           "full random trace differs");
    check (trace<input_pickers::critical_pq> (g, old) ==
               trace<input_pickers::critical_pq> (g, compact),
           "priority picker trace differs");
    auto old_input = old.actions ().begin ();
    for (auto& [input, actions] : compact.actions ()) {
      const auto before = actioners::profile_dominance::prune (old_input->second);
      const auto after = actioners::profile_dominance::prune (actions);
      check (before.pair_tests == after.pair_tests and
                 before.endpoint_visits == after.endpoint_visits and
                 before.declined == after.declined,
             "dominance traversal differs");
      ++old_input;
    }
    a.clear ();
    b.clear ();
    serialize (a, old.actions ());
    serialize (b, compact.actions ());
    check (a == b, "dominance output order differs");
  }
}

int main () {
  try {
    static_assert (sizeof (actioners::compact_payload) == sizeof (std::vector<int>));
    for (unsigned seed = 0; seed < 64; ++seed) {
      const auto g = generated (seed);
      posets::vectors::bool_threshold = seed % (g.aut->num_states () + 1);
      compare (g, ios_precomputers::detail::mona<spot::twa_graph_ptr, transitions, false> (
                      g.aut, g.inputs, g.outputs) ());
      compare (g, ios_precomputers::detail::mona<spot::twa_graph_ptr, transitions, true> (
                      g.aut, g.inputs, g.outputs) ());
      using old = actioners::payload_reference::standard<state>;
      using compact = actioners::standard<state>;
      check (solve_trace<old, false> (g) == solve_trace<compact, false> (g),
             "backward predecessor/region/strategy trace differs");
      check (solve_trace<old, true> (g) == solve_trace<compact, true> (g),
             "forward result/certificate/strategy trace differs");
    }
    std::cout << "action-payload: byte-identical tables, four picker/RNG traces, "
                 "predecessors, forward/backward results and strategies on 64 games\n";
  } catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
}
