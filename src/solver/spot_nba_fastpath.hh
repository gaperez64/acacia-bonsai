#pragma once

#include "configuration.hh"
#include "phase_records.hh"
#include "solver/spot_fast_mode.hh"
#include "utils/verbose.hh"

#include <cassert>
#include <chrono>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <bddx.h>
#include <spot/twa/acc.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/game.hh>
#include <spot/twaalgos/mealy_machine.hh>
#include <spot/twaalgos/synthesis.hh>
#include "solver/synthesis_rendezvous.hh"

namespace acacia::spot_fastpath {

  enum class nba_fast_class {
    deterministic_buchi,
    nondeterministic_buchi,
    unsupported,
  };

  struct fast_path_result {
      bool conclusive = false;
      bool current_output_player_wins = false;
      nba_fast_class classification = nba_fast_class::unsupported;
      bool classification_ran = false;
      long long classification_ms = 0;
      long long solve_ms = 0;
      std::optional<spot::region_t> current_output_player_winning_region = std::nullopt;
      std::optional<spot::twa_graph_ptr> strategy = std::nullopt;
      std::optional<spot::twa_graph_ptr> solved_arena = std::nullopt;
  };

  namespace detail {
    using clock = std::chrono::steady_clock;

    inline long long elapsed_ms (clock::time_point started) {
      return std::chrono::duration_cast<std::chrono::milliseconds> (clock::now () - started)
          .count ();
    }

    inline std::string_view class_name (nba_fast_class c) {
      switch (c) {
        case nba_fast_class::deterministic_buchi: return "deterministic";
        case nba_fast_class::nondeterministic_buchi: return "nondeterministic";
        case nba_fast_class::unsupported: return "unsupported";
      }
      return "unknown";
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

  inline nba_fast_class classify_nba_for_fast_path (const spot::twa_graph_ptr& aut) {
    if (not is_plain_existential_buchi (aut))
      return nba_fast_class::unsupported;

    return is_syntactically_deterministic (aut) ? nba_fast_class::deterministic_buchi
                                                : nba_fast_class::nondeterministic_buchi;
  }

  inline fast_path_result deterministic_negated_spec_fast_path (
      const spot::twa_graph_ptr& negated_spec_aut, const bdd& all_outputs,
      bool want_strategy, bool want_winning_region = false, bool moore = false) {
    fast_path_result res;

    auto good = complete_copy_with_rejecting_sink (negated_spec_aut);
    good->set_acceptance (1, spot::acc_cond::acc_code::cobuchi ());
    spot::set_synthesis_outputs (good, all_outputs);

    spot::synthesis_info gi;
    gi.sp = spot::synthesis_info::splittype::AUTO;
    gi.moore = moore;
    auto arena = spot::split_2step (good, gi);
    const bool p_out_wins = want_winning_region
        ? spot::solve_parity_game (arena, true)
        : spot::solve_game (arena, gi);

    res.conclusive = true;
    res.current_output_player_wins = p_out_wins;
    if (want_winning_region) {
      const auto& arena_winners = spot::get_state_winners (arena);
      spot::region_t original_winners (negated_spec_aut->num_states (), false);
      for (unsigned s = 0; s < negated_spec_aut->num_states (); ++s)
        original_winners[s] = arena_winners[s];
      res.current_output_player_winning_region = std::move (original_winners);
      res.solved_arena = arena;
    }
    if (p_out_wins and want_strategy) {
      if (acacia::synthesis::deferred_controller) {
        *acacia::synthesis::deferred_controller = [arena, gi] () mutable {
          return spot::solved_game_to_separated_mealy (arena, gi);
        };
        res.strategy = negated_spec_aut;
      }
      else
        res.strategy = spot::solved_game_to_separated_mealy (arena, gi);
    }

    return res;
  }

  inline fast_path_result try_spot_nba_fast_path (const spot::twa_graph_ptr& negated_spec_aut,
                                                  const bdd& all_outputs,
                                                  bool want_controller_strategy,
                                                  SPOT_FAST_T mode) {
    if (mode != SPOT_FAST_DET)
      return {};

    fast_path_result result;
    result.classification_ran = true;

    acacia::accounting_phase classification ("spot_fast_classification");
    const auto class_started = detail::clock::now ();
    result.classification = classify_nba_for_fast_path (negated_spec_aut);
    result.classification_ms = detail::elapsed_ms (class_started);
    classification.finish ();

    nba_fast_class c = result.classification;
    verb_do (1, utils::vout << "Spot NBA fast path classification: "
                            << detail::class_name (c) << std::endl);

    if (c == nba_fast_class::deterministic_buchi) {
      acacia::accounting_phase solving ("spot_fast_solve");
      const auto solve_started = detail::clock::now ();
      auto res = deterministic_negated_spec_fast_path (negated_spec_aut, all_outputs,
                                                    want_controller_strategy);
      res.classification = result.classification;
      res.classification_ran = result.classification_ran;
      res.classification_ms = result.classification_ms;
      res.solve_ms = detail::elapsed_ms (solve_started);
      if (want_controller_strategy and not res.current_output_player_wins)
        return {};
      return res;
    }

    return result;
  }

}  // namespace acacia::spot_fastpath
