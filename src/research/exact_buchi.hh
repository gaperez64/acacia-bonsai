#pragma once

#include <chrono>
#include <spot/twaalgos/cleanacc.hh>
#include <spot/twaalgos/degen.hh>
#include <spot/twaalgos/isdet.hh>
#include <spot/twaalgos/parity.hh>
#include <spot/twaalgos/remfin.hh>
#include <spot/twaalgos/sbacc.hh>
#include <spot/twaalgos/sccfilter.hh>
#include <spot/twaalgos/strength.hh>
#include <spot/twaalgos/totgba.hh>
#include <spot/twaalgos/zlktree.hh>
#include <stdexcept>
#include <utility>
#include <vector>

namespace acacia::research {

  struct buchi_route {
      spot::twa_graph_ptr aut;
      unsigned tier = 0;
  };

  inline bool plain_buchi (const spot::twa_graph_ptr& aut) {
    return aut->acc ().is_buchi () or aut->acc ().is_t () or aut->acc ().is_f ();
  }

  // Fin-less CNF clauses become Inf of the union of their edge marks. Spot's
  // top_conjuncts splits grouped Inf marks as well. No graph choices are added.
  inline spot::twa_graph_ptr finless_gba (spot::twa_graph_ptr aut) {
    if (aut->acc ().uses_fin_acceptance ())
      throw std::runtime_error ("generalized acceptance must not contain Fin");
    if (aut->acc ().is_generalized_buchi ())
      return aut;
    const auto clauses = aut->get_acceptance ().to_cnf ().top_conjuncts ();
    std::vector<spot::acc_cond::mark_t> marks;
    for (const auto& clause : clauses)
      marks.push_back (clause.used_sets ());
    aut->set_generalized_buchi (marks.size ());
    for (auto& edge : aut->edges ()) {
      spot::acc_cond::mark_t acc {};
      for (unsigned i = 0; i < marks.size (); ++i)
        if (edge.acc & marks[i])
          acc.set (i);
      edge.acc = acc;
    }
    return aut;
  }

  // The caller owns this graph. Classification uses only its current structure,
  // never a formula, translator, complement, or postprocessor. Only tier 4 calls
  // to_generalized_buchi. The split route returns a ready tier 1-3 graph or a
  // tier-4 GBA; state_only completes the latter in an independent process.
  template <typename Observer>
  buchi_route route_buchi (spot::twa_graph_ptr aut, bool generalized_only, bool state_only,
                           Observer&& observe) {
    if (not aut->is_existential ())
      throw std::runtime_error ("conversion requires an existential automaton");
    // HOA strength flags are hints; applicability follows the actual graph.
    aut->prop_weak (spot::trival::maybe ());
    aut->prop_very_weak (spot::trival::maybe ());
    aut->prop_inherently_weak (spot::trival::maybe ());
    aut->prop_terminal (spot::trival::maybe ());
    const std::vector<spot::formula> aps {aut->ap ().begin (), aut->ap ().end ()};
    unsigned tier = state_only ? 4 : 0;
    auto stage = [&] (const char* name, auto convert) {
      observe (name, aut, -1.0, tier, true);
      const auto start = std::chrono::steady_clock::now ();
      auto next = convert (aut);
      const double seconds =
          std::chrono::duration<double> (std::chrono::steady_clock::now () - start).count ();
      const bool available = bool (next);
      if (next)
        aut = std::move (next);
      for (const auto& ap : aps)
        aut->register_ap (ap);
      observe (name, aut, seconds, tier, available);
      return available;
    };
    auto deterministic = [&] {
      aut->prop_universal (spot::trival::maybe ());
      if (not spot::is_deterministic (aut))
        throw std::runtime_error ("deterministic conversion introduced nondeterminism");
    };
    if (not state_only) {
      deterministic ();
      stage ("acceptance-cleanup",
             [] (const auto& value) { return spot::simplify_acceptance_here (value); });
      if (plain_buchi (aut))
        tier = 1;
      else if (not aut->acc ().uses_fin_acceptance ())
        tier = 2;
      else {
        // Clear cached HOA strength hints: the graph decides applicability.
        stage ("acceptance-strength", [] (const auto& value) {
          value->prop_inherently_weak (spot::trival::maybe ());
          spot::is_inherently_weak_automaton (value);
          return value;
        });
        if (aut->prop_inherently_weak ().is_true ()) {
          tier = 3;
          stage ("weak-relabel", [] (const auto& value) {
            auto weak = spot::scc_filter (value);
            // scc_filter keeps co-Buchi acceptance; remove_fin's verified weak
            // branch relabels it without nondeterministic guesses.
            if (weak->acc ().uses_fin_acceptance ()) {
              weak->prop_weak (spot::trival::maybe ());
              if (not spot::is_weak_automaton (weak))
                throw std::runtime_error ("weak relabelling did not produce a weak graph");
              weak = spot::remove_fin (weak);
            }
            return weak;
          });
        }
        else {
          std::vector<spot::acc_cond::rs_pair> pairs;
          const bool rabin = aut->acc ().is_rabin_like (pairs);
          if (aut->acc ().is_parity ()) {
            stage ("parity-reduce", [] (const auto& value) {
              return spot::simplify_acceptance_here (spot::reduce_parity (value));
            });
          }
          else if (not rabin) {
            stage ("acd-parity", [] (const auto& value) { return spot::acd_transform (value); });
            stage ("parity-reduce", [] (const auto& value) {
              return spot::simplify_acceptance_here (spot::reduce_parity (value));
            });
          }
          if (plain_buchi (aut) or not aut->acc ().uses_fin_acceptance ())
            tier = 3;
          else if (stage ("rabin-buchi", [] (const auto& value) {
                     auto result = spot::rabin_to_buchi_if_realizable (value);
                     if (result)
                       result->prop_state_acc (spot::trival::maybe ());
                     return result;
                   }))
            tier = 3;
          else
            tier = 4;
        }
      }
      deterministic ();
      observe ("tier", aut, 0.0, tier, true);
      if (tier == 1)
        return {std::move (aut), tier};
      if (tier == 2 or tier == 3) {
        // Constants and transition Buchi are already accepted by spot_fast.
        if (not plain_buchi (aut)) {
          stage ("finless-normalize", [] (auto value) { return finless_gba (value); });
          stage ("degeneralize", [] (const auto& value) { return spot::degeneralize (value); });
        }
        deterministic ();
        if (not plain_buchi (aut))
          throw std::runtime_error ("deterministic route did not produce Buchi acceptance");
        return {std::move (aut), tier};
      }
      stage ("generalized", [] (const auto& value) { return spot::to_generalized_buchi (value); });
    }
    if (not aut->acc ().is_generalized_buchi ())
      throw std::runtime_error ("state conversion requires generalized Buchi acceptance");
    if (not generalized_only) {
      stage ("degeneralize", [] (const auto& value) { return spot::degeneralize (value); });
      stage ("state-acceptance", [] (const auto& value) { return spot::sbacc (value); });
      if (not aut->acc ().is_buchi () or aut->num_sets () != 1 or
          not aut->prop_state_acc ().is_true ())
        throw std::runtime_error ("conversion did not produce one-set state-based Buchi");
    }
    return {std::move (aut), tier};
  }

  inline spot::twa_graph_ptr exact_buchi (spot::twa_graph_ptr aut) {
    return route_buchi (std::move (aut), false, false, [] (auto&&...) {}).aut;
  }

}  // namespace acacia::research
