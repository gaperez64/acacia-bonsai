#pragma once

#include "utils/cache.hh"
#include <bddx.h>
#include <spot/twa/twagraph.hh>

namespace utils {
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
  inline spot::twa_graph_ptr push_aps (const spot::twa_graph_ptr aut,
                                       bdd to_be_pushed, bdd all_others) {
    auto ret = spot::make_twa_graph (aut->get_dict ());
    ret->copy_acceptance_of (aut);
    ret->copy_ap_of (aut);
    ret->prop_copy (aut, spot::twa::prop_set::all ());
    ret->prop_universal (spot::trival::maybe ());

    static auto cache = utils::make_cache<unsigned> (0u, 0u);
    const auto build_aut = [&] (unsigned state, bdd saved_o, const auto& recurse) {
      auto cached = cache.get (state, saved_o.id ());
      if (cached)
        return *cached;
      auto ret_state = ret->new_state ();
      cache (ret_state, state, saved_o.id ());
      for (auto& e : aut->out (state)) {
        auto cond = e.cond;
        while (cond != bddfalse) {
          bdd one_sat = bdd_satoneset (cond, to_be_pushed, bddtrue);
          bdd one_input_bdd = bdd_exist (cond & bdd_exist (one_sat, all_others), to_be_pushed);
          ret->new_edge (ret_state,
                         recurse (e.dst, bdd_exist (cond & one_input_bdd, all_others), recurse),
                         saved_o & one_input_bdd, e.acc);
          cond -= one_input_bdd;
        }
      }
      return ret_state;
    };
    build_aut (aut->get_init_state_number (), bddtrue, build_aut);
    return ret;
  }
} // namespace utils
