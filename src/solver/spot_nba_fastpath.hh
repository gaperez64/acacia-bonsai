#pragma once

#include "configuration.hh"
#include "solver/spot_fast_mode.hh"
#include "utils/verbose.hh"

#include <algorithm>
#include <cassert>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <bddx.h>
#include <spot/twa/acc.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/game.hh>
#include <spot/twaalgos/mealy_machine.hh>
#include <spot/twaalgos/synthesis.hh>

namespace acacia::spot_fastpath {

  enum class nba_fast_class {
    deterministic_buchi,
    gfg_buchi,
    non_gfg_buchi,
    unsupported,
  };

  struct fast_path_result {
      bool conclusive = false;
      bool current_output_player_wins = false;
      std::optional<spot::region_t> current_output_player_winning_region = std::nullopt;
      std::optional<spot::twa_graph_ptr> strategy = std::nullopt;
  };

  namespace detail {

    inline std::string_view class_name (nba_fast_class c) {
      switch (c) {
        case nba_fast_class::deterministic_buchi: return "deterministic-buchi";
        case nba_fast_class::gfg_buchi: return "gfg-buchi";
        case nba_fast_class::non_gfg_buchi: return "non-gfg-buchi";
        case nba_fast_class::unsupported: return "unsupported";
      }
      return "unknown";
    }

    inline bool has_mode (SPOT_FAST_T mode, SPOT_FAST_T flag) {
      return (static_cast<int> (mode) & static_cast<int> (flag)) != 0;
    }

    inline bool within_two_token_runtime_budget (unsigned states) {
      static constexpr size_t max_token_triples = 30000;
      size_t n = states;
      return n == 0 or n <= max_token_triples / n / n;
    }

    inline std::vector<bdd> local_partition (const std::vector<bdd>& labels) {
      std::vector<bdd> regions = {bddtrue};

      for (bdd label : labels) {
        if (label == bddfalse)
          continue;

        std::vector<bdd> next;
        next.reserve (regions.size () * 2);
        for (bdd region : regions) {
          bdd in = region & label;
          bdd out = region & !label;
          if (in != bddfalse)
            next.push_back (in);
          if (out != bddfalse)
            next.push_back (out);
        }
        regions = std::move (next);
      }

      return regions;
    }

    inline spot::acc_cond::mark_t edge_acc_with_state_marks (
        const spot::const_twa_graph_ptr& src, unsigned state,
        spot::acc_cond::mark_t edge_acc) {
      if (src->prop_state_acc ().is_true ())
        edge_acc |= src->state_acc_sets (state);
      return edge_acc;
    }

    inline void copy_existential_edges (const spot::const_twa_graph_ptr& src,
                                        spot::twa_graph_ptr& dst,
                                        const std::vector<unsigned>& state_map) {
      for (unsigned s = 0; s < src->num_states (); ++s)
        for (const auto& e : src->out (s)) {
          assert (not src->is_univ_dest (e));
          dst->new_edge (state_map[s], state_map[e.dst], e.cond,
                         edge_acc_with_state_marks (src, s, e.acc));
        }
    }

  }  // namespace detail

  inline bool is_plain_existential_buchi (const spot::const_twa_graph_ptr& aut) {
    return aut and aut->num_states () > 0 and aut->is_existential () and aut->acc ().is_buchi () and
           aut->acc ().num_sets () == 1;
  }

  inline bool is_syntactically_deterministic (const spot::const_twa_graph_ptr& aut) {
    if (not aut or aut->num_states () == 0)
      return true;

    if (not aut->is_existential ())
      return false;

    for (unsigned s = 0; s < aut->num_states (); ++s) {
      bdd covered = bddfalse;
      for (const auto& e : aut->out (s)) {
        if (e.cond == bddfalse)
          continue;

        if ((covered & e.cond) != bddfalse)
          return false;
        covered |= e.cond;
      }
    }

    return true;
  }

  inline spot::twa_graph_ptr
  complete_copy_with_rejecting_sink (const spot::const_twa_graph_ptr& aut) {
    auto ret = spot::make_twa_graph (aut->get_dict ());
    ret->copy_ap_of (aut);
    ret->copy_acceptance_of (aut);
    ret->prop_copy (aut, spot::twa::prop_set::all ());
    ret->prop_state_acc (false);
    ret->new_states (aut->num_states ());
    ret->set_init_state (aut->get_init_state_number ());

    std::vector<unsigned> state_map (aut->num_states ());
    for (unsigned s = 0; s < aut->num_states (); ++s)
      state_map[s] = s;
    detail::copy_existential_edges (aut, ret, state_map);

    std::optional<unsigned> sink;
    auto get_sink = [&] () -> unsigned {
      if (not sink.has_value ()) {
        sink = ret->new_state ();
        ret->new_edge (*sink, *sink, bddtrue, spot::acc_cond::mark_t {});
      }
      return *sink;
    };

    const unsigned old_n = aut->num_states ();
    for (unsigned s = 0; s < old_n; ++s) {
      bdd covered = bddfalse;
      for (const auto& e : ret->out (s))
        covered |= e.cond;

      bdd missing = !covered;
      if (missing != bddfalse)
        ret->new_edge (s, get_sink (), missing,
                       detail::edge_acc_with_state_marks (aut, s,
                                                          spot::acc_cond::mark_t {}));
    }

    if (sink.has_value ())
      ret->prop_complete (true);

    return ret;
  }

  namespace detail {

    struct two_token_state {
        unsigned eve;
        unsigned adam1;
        unsigned adam2;

        bool operator== (const two_token_state&) const = default;
    };

    struct two_token_hash {
        static void combine (size_t& seed, unsigned value) {
          seed ^= std::hash<unsigned> {} (value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }

        size_t operator() (const two_token_state& s) const {
          size_t seed = 0;
          combine (seed, s.eve);
          combine (seed, s.adam1);
          combine (seed, s.adam2);
          return seed;
        }
    };

    inline bool two_token_game_eve_wins (const spot::twa_graph_ptr& aut) {
      auto completed = complete_copy_with_rejecting_sink (aut);
      auto game = spot::make_twa_graph (completed->get_dict ());
      game->copy_ap_of (completed);
      game->set_acceptance (5, spot::acc_cond::acc_code::parity_max_even (5));

      std::vector<bool> owners;
      auto new_state = [&] (bool owner) {
        const unsigned s = game->new_state ();
        owners.push_back (owner);
        return s;
      };

      std::unordered_map<two_token_state, unsigned, two_token_hash> state_to_vertex;
      std::vector<two_token_state> todo;

      auto get_state = [&] (unsigned eve, unsigned adam1, unsigned adam2) {
        two_token_state p {eve, adam1, adam2};
        auto [it, inserted] = state_to_vertex.emplace (p, 0);
        if (inserted) {
          it->second = new_state (false);  // Adam chooses the current letter region.
          todo.push_back (p);
        }
        return it->second;
      };

      const unsigned init = completed->get_init_state_number ();
      game->set_init_state (get_state (init, init, init));

      for (size_t idx = 0; idx < todo.size (); ++idx) {
        const auto [eve, adam1, adam2] = todo[idx];
        const unsigned state_v = get_state (eve, adam1, adam2);

        std::vector<bdd> labels;
        for (const auto& e : completed->out (eve))
          labels.push_back (e.cond);
        for (const auto& e : completed->out (adam1))
          labels.push_back (e.cond);
        for (const auto& e : completed->out (adam2))
          labels.push_back (e.cond);

        for (bdd region : local_partition (labels)) {
          unsigned eve_choice = new_state (true);
          game->new_edge (state_v, eve_choice, region, spot::acc_cond::mark_t {0});

          for (const auto& ee : completed->out (eve)) {
            if ((ee.cond & region) == bddfalse)
              continue;

            unsigned adam_choice = new_state (false);
            game->new_edge (eve_choice, adam_choice, bddtrue,
                            ee.acc ? spot::acc_cond::mark_t {4}
                                   : spot::acc_cond::mark_t {});

            for (const auto& a1 : completed->out (adam1))
              if ((a1.cond & region) != bddfalse)
                for (const auto& a2 : completed->out (adam2)) {
                  if ((a2.cond & region) == bddfalse)
                    continue;

                  spot::acc_cond::mark_t acc {};
                  if (a1.acc)
                    acc.set (3);
                  if (a2.acc)
                    acc.set (1);

                  game->new_edge (adam_choice, get_state (ee.dst, a1.dst, a2.dst),
                                  bddtrue, acc);
                }

          }
        }
      }

      spot::set_state_players (game, std::move (owners));
      return spot::solve_parity_game (game);
    }

    inline spot::twa_graph_ptr build_gfg_decision_arena (const spot::twa_graph_ptr& aut_forbid,
                                                         const bdd& all_inputs,
                                                         const bdd& all_outputs) {
      auto completed = complete_copy_with_rejecting_sink (aut_forbid);

      auto arena = spot::make_twa_graph (completed->get_dict ());
      arena->copy_ap_of (completed);
      arena->copy_acceptance_of (completed);
      arena->prop_copy (completed, spot::twa::prop_set::all ());
      arena->release_named_properties ();
      arena->prop_state_acc (false);

      std::vector<bool> owners;
      auto new_state = [&] (bool owner) {
        const unsigned s = arena->new_state ();
        owners.push_back (owner);
        return s;
      };

      std::vector<unsigned> monitor_states (completed->num_states ());
      for (unsigned q = 0; q < completed->num_states (); ++q)
        monitor_states[q] = new_state (false);  // Environment chooses current inputs.

      arena->set_init_state (monitor_states[completed->get_init_state_number ()]);

      for (unsigned q = 0; q < completed->num_states (); ++q) {
        bdd possible_inputs = bddfalse;
        for (const auto& e : completed->out (q))
          possible_inputs |= bdd_exist (e.cond, all_outputs);

        for (bdd input_region : minterms_of (possible_inputs, all_inputs)) {
          const unsigned output_choice = new_state (true);  // Controller chooses outputs.
          arena->new_edge (monitor_states[q], output_choice, input_region,
                           spot::acc_cond::mark_t {});

          bdd possible_outputs = bddfalse;
          for (const auto& e : completed->out (q))
            possible_outputs |= bdd_exist (e.cond & input_region, all_inputs);

          for (bdd output_region : minterms_of (possible_outputs, all_outputs)) {
            const unsigned transition_choice = new_state (false);
            arena->new_edge (output_choice, transition_choice, output_region,
                             spot::acc_cond::mark_t {});

            for (const auto& e : completed->out (q))
              if ((e.cond & input_region & output_region) != bddfalse)
                arena->new_edge (transition_choice, monitor_states[e.dst], bddtrue, e.acc);
          }
        }
      }

      spot::set_state_players (arena, std::move (owners));
      return arena;
    }

  }  // namespace detail

  inline nba_fast_class classify_nba_for_fast_path (const spot::twa_graph_ptr& aut,
                                                   bool run_gfg_recognizer,
                                                   bool enforce_runtime_budget) {
    if (not is_plain_existential_buchi (aut))
      return nba_fast_class::unsupported;

    if (is_syntactically_deterministic (aut))
      return nba_fast_class::deterministic_buchi;

    if (not run_gfg_recognizer)
      return nba_fast_class::unsupported;

    if (enforce_runtime_budget and
        not detail::within_two_token_runtime_budget (aut->num_states ()))
      return nba_fast_class::unsupported;

    return detail::two_token_game_eve_wins (aut) ? nba_fast_class::gfg_buchi
                                                 : nba_fast_class::non_gfg_buchi;
  }

  inline nba_fast_class classify_nba_for_fast_path (const spot::twa_graph_ptr& aut,
                                                   bool run_gfg_recognizer) {
    return classify_nba_for_fast_path (aut, run_gfg_recognizer, false);
  }

  inline nba_fast_class classify_nba_for_fast_path (const spot::twa_graph_ptr& aut) {
    return classify_nba_for_fast_path (aut, true);
  }

  inline fast_path_result deterministic_forbidden_fast_path (
      const spot::twa_graph_ptr& aut_forbid, const bdd& all_outputs,
      bool want_strategy, bool want_winning_region = false) {
    fast_path_result res;

    auto good = complete_copy_with_rejecting_sink (aut_forbid);
    good->set_acceptance (1, spot::acc_cond::acc_code::cobuchi ());
    spot::set_synthesis_outputs (good, all_outputs);

    spot::synthesis_info gi;
    gi.sp = spot::synthesis_info::splittype::AUTO;
    auto arena = spot::split_2step (good, gi);
    const bool p_out_wins = want_winning_region
        ? spot::solve_parity_game (arena, true)
        : spot::solve_game (arena, gi);

    res.conclusive = true;
    res.current_output_player_wins = p_out_wins;
    if (want_winning_region) {
      const auto& arena_winners = spot::get_state_winners (arena);
      spot::region_t original_winners (aut_forbid->num_states (), false);
      for (unsigned s = 0; s < aut_forbid->num_states (); ++s)
        original_winners[s] = arena_winners[s];
      res.current_output_player_winning_region = std::move (original_winners);
    }
    if (p_out_wins and want_strategy)
      res.strategy = spot::solved_game_to_separated_mealy (arena, gi);

    return res;
  }

  inline fast_path_result gfg_forbidden_decision_only_fast_path (
      const spot::twa_graph_ptr& aut_forbid, const bdd& all_inputs,
      const bdd& all_outputs) {
    fast_path_result res;

    auto arena = detail::build_gfg_decision_arena (aut_forbid, all_inputs, all_outputs);
    arena->set_acceptance (1, spot::acc_cond::acc_code::cobuchi ());
    const bool p_out_wins = spot::solve_game (arena);

    res.conclusive = true;
    res.current_output_player_wins = p_out_wins;
    return res;
  }

  inline fast_path_result try_spot_nba_fast_path (const spot::twa_graph_ptr& aut_forbid,
                                                  const bdd& all_inputs,
                                                  const bdd& all_outputs,
                                                  bool want_controller_strategy,
                                                  bool allow_gfg_decision,
                                                  SPOT_FAST_T mode) {
    if (not detail::has_mode (mode, SPOT_FAST_DET))
      return {};

    const bool can_use_gfg_decision =
        detail::has_mode (mode, SPOT_FAST_GFG_DECISION) and
        allow_gfg_decision and not want_controller_strategy;
    nba_fast_class c = classify_nba_for_fast_path (aut_forbid, can_use_gfg_decision, true);
    verb_do (1, utils::vout << "Spot NBA fast path classification: "
                            << detail::class_name (c) << std::endl);

    if (c == nba_fast_class::deterministic_buchi and detail::has_mode (mode, SPOT_FAST_DET)) {
      auto res = deterministic_forbidden_fast_path (aut_forbid, all_outputs,
                                                    want_controller_strategy);
      if (want_controller_strategy and not res.current_output_player_wins)
        return {};
      return res;
    }

    if (c == nba_fast_class::gfg_buchi and
        can_use_gfg_decision)
      return gfg_forbidden_decision_only_fast_path (aut_forbid, all_inputs, all_outputs);

    return {};
  }

}  // namespace acacia::spot_fastpath
