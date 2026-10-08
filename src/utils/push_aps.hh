#pragma once

#include "phase_records.hh"

#include <bddx.h>
#include <cstddef>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <spot/twa/twagraph.hh>
#include <utility>

namespace utils {
  inline constexpr std::size_t push_aps_max_states = 100000;
  inline constexpr std::size_t push_aps_max_edges = 250000;

  struct push_aps_observation {
      size_t interned_keys = 0, expanded_keys = 0, partition_computations = 0, emitted_tuples = 0;
      std::set<unsigned> sources;
      const char* limit_reason = "none";
      void publish () const {
        acacia::legacy_count ("interned_keys", interned_keys);
        acacia::legacy_count ("expanded_keys", expanded_keys);
        acacia::legacy_count ("distinct_original_sources", sources.size ());
        acacia::legacy_count ("repeated_source_expansions", expanded_keys - sources.size ());
        acacia::legacy_count ("partition_computations", partition_computations);
        acacia::legacy_count ("emitted_tuples", emitted_tuples);
        acacia::legacy_metric ("limit_reason", limit_reason);
      }
  };

  /**
   * Changes  q -> <i', o'> -> q'  (with saved o)  to
   * q -> <i', o> -> {q' saved o'}.
   *
   * to_be_pushed:  the output APs (whose values are "saved" one step later).
   * all_others:    the input APs.
   *
   * This implements the UNREAL_X_AUTOMATON transformation: push the outputs
   * into the next transition so that the automaton uses Moore semantics.
   */
  inline spot::twa_graph_ptr push_aps (const spot::twa_graph_ptr aut, bdd to_be_pushed,
                                       bdd all_others,
                                       std::size_t max_states = push_aps_max_states,
                                       std::size_t max_edges = push_aps_max_edges,
                                       push_aps_observation* observed = nullptr) {
    auto ret = spot::make_twa_graph (aut->get_dict ());
    ret->copy_acceptance_of (aut);
    ret->copy_ap_of (aut);
    ret->prop_copy (aut, spot::twa::prop_set::all ());
    ret->prop_universal (spot::trival::maybe ());

    struct pending_state {
        unsigned source;
        bdd saved_o;
        unsigned target;
    };

    std::map<std::pair<unsigned, int>, unsigned> states;
    std::deque<pending_state> pending;
    const auto intern = [&] (unsigned source, bdd saved_o) -> std::optional<unsigned> {
      const auto key = std::pair {source, saved_o.id ()};
      if (const auto found = states.find (key); found != states.end ())
        return found->second;
      if (states.size () >= max_states) {
        if (observed)
          observed->limit_reason = "states";
        return std::nullopt;
      }

      const unsigned target = ret->new_state ();
      states.emplace (key, target);
      if (observed)
        ++observed->interned_keys;
      pending.emplace_back (source, saved_o, target);
      return target;
    };

    const auto init = intern (aut->get_init_state_number (), bddtrue);
    if (not init.has_value ())
      return nullptr;
    ret->set_init_state (*init);
    std::size_t edges = 0;
    while (not pending.empty ()) {
      auto [state, saved_o, ret_state] = pending.front ();
      pending.pop_front ();
      if (observed) {
        ++observed->expanded_keys;
        observed->sources.insert (state);
      }
      for (const auto& e : aut->out (state)) {
        auto cond = e.cond;
        while (cond != bddfalse) {
          if (observed)
            ++observed->partition_computations;
          bdd one_sat = bdd_satoneset (cond, to_be_pushed, bddtrue);
          bdd one_input_bdd = bdd_exist (cond & bdd_exist (one_sat, all_others), to_be_pushed);
          bdd next_saved_o = bdd_exist (cond & one_input_bdd, all_others);
          const auto target = intern (e.dst, next_saved_o);
          if (not target.has_value () or edges >= max_edges) {
            if (observed && target.has_value ())
              observed->limit_reason = "edges";
            return nullptr;
          }
          ret->new_edge (ret_state, *target, saved_o & one_input_bdd, e.acc);
          ++edges;
          if (observed)
            ++observed->emitted_tuples;
          cond -= one_input_bdd;
        }
      }
    }
    return ret;
  }
}  // namespace utils
