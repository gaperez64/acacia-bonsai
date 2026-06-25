#pragma once

#include "aut_preprocessors/surely_losing.hh"
#include "solver/spot_nba_fastpath.hh"
#include "utils/verbose.hh"

#include <limits>
#include <optional>
#include <vector>

#include <bddx.h>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/hoa.hh>
#include <spot/twaalgos/sccinfo.hh>

namespace aut_preprocessors {
  namespace detail {
    template <typename Aut>
    class elevator {
      public:
        elevator (Aut& aut, bdd input_support, bdd output_support, unsigned K)
          : aut {aut},
            input_support {input_support},
            output_support {output_support},
            K {K} {}

        auto operator() () {
          aut_preprocessors::surely_losing::make (aut, input_support, output_support, K) ();

          if (aut->num_states () == 0 or not aut->is_existential () or
              not aut->acc ().is_buchi () or aut->acc ().num_sets () != 1 or
              not aut->prop_state_acc ().is_true ())
            return;

          spot::scc_info si (aut, spot::scc_info_options::ALL);
          std::optional<unsigned> safe_trap;
          unsigned safe_collapsed = 0;
          unsigned losing_collapsed = 0;
          unsigned strategy_dependent_winning = 0;

          for (unsigned scc = 0; scc < si.scc_count (); ++scc) {
            if (not has_inner_edge (si, scc) or not is_closed_deterministic_scc (si, scc))
              continue;

            auto winner = controller_winner_for_scc (si, scc);
            if (not winner.has_value ())
              continue;

            const auto& states = si.states_of (scc);
            if (*winner) {
              if (is_rejecting_scc (states)) {
                rewrite_as_safe_trap (states, safe_trap);
                ++safe_collapsed;
              } else {
                ++strategy_dependent_winning;
              }
            } else {
              rewrite_as_losing_trap (states);
              ++losing_collapsed;
            }
          }

          if (safe_collapsed or losing_collapsed) {
            aut->prop_universal (spot::trival::maybe ());
            aut->purge_unreachable_states ();
          }

          verb_do (1, vout << "ELEVATOR collapsed "
                           << safe_collapsed
                           << " winning SCCs and "
                           << losing_collapsed
                           << " losing SCCs; skipped "
                           << strategy_dependent_winning
                           << " strategy-dependent winning SCCs; aut has "
                           << aut->num_states ()
                           << " states." << std::endl);
          verb_do (2, {
            vout << "Automaton after ELEVATOR preprocessing:\n";
            spot::print_hoa (vout, aut, nullptr) << std::endl;
          });
        }

      private:
        static constexpr unsigned no_state = std::numeric_limits<unsigned>::max ();

        bool has_inner_edge (const spot::scc_info& si, unsigned scc) const {
          for (unsigned q : si.states_of (scc))
            for (const auto& e : aut->out (q))
              if (e.cond != bddfalse and not aut->is_univ_dest (e) and si.scc_of (e.dst) == scc)
                return true;
          return false;
        }

        bool is_closed_deterministic_scc (const spot::scc_info& si, unsigned scc) const {
          for (unsigned q : si.states_of (scc)) {
            std::vector<bdd> labels;

            for (const auto& e : aut->out (q)) {
              if (e.cond == bddfalse)
                continue;
              if (aut->is_univ_dest (e) or si.scc_of (e.dst) != scc)
                return false;

              for (bdd old : labels)
                if ((old & e.cond) != bddfalse)
                  return false;

              labels.push_back (e.cond);
            }
          }

          return true;
        }

        spot::twa_graph_ptr restricted_scc_copy (const spot::scc_info& si, unsigned scc) const {
          const auto& states = si.states_of (scc);

          auto ret = spot::make_twa_graph (aut->get_dict ());
          ret->copy_ap_of (aut);
          ret->copy_acceptance_of (aut);
          ret->prop_copy (aut, spot::twa::prop_set::all ());
          ret->prop_state_acc (false);
          ret->new_states (states.size ());

          std::vector<unsigned> state_map (aut->num_states (), no_state);
          for (unsigned local = 0; local < states.size (); ++local)
            state_map[states[local]] = local;

          ret->set_init_state (0);
          for (unsigned old_src : states) {
            const unsigned src = state_map[old_src];
            for (const auto& e : aut->out (old_src)) {
              if (e.cond == bddfalse or aut->is_univ_dest (e) or state_map[e.dst] == no_state)
                continue;
              ret->new_edge (
                  src, state_map[e.dst], e.cond,
                  acacia::spot_fastpath::detail::edge_acc_with_state_marks (aut, old_src, e.acc));
            }
          }

          return ret;
        }

        std::optional<bool> controller_winner_for_scc (const spot::scc_info& si,
                                                       unsigned scc) const {
          const auto& states = si.states_of (scc);
          auto component = restricted_scc_copy (si, scc);
          std::optional<bool> winner;

          for (unsigned init = 0; init < states.size (); ++init) {
            component->set_init_state (init);
            auto res = acacia::spot_fastpath::deterministic_forbidden_fast_path (
                component, output_support, false);
            if (not res.conclusive)
              return std::nullopt;
            if (winner.has_value () and *winner != res.current_output_player_wins)
              return std::nullopt;
            winner = res.current_output_player_wins;
          }

          return winner;
        }

        unsigned get_safe_trap (std::optional<unsigned>& safe_trap) {
          if (not safe_trap.has_value ()) {
            safe_trap = aut->new_state ();
            aut->new_acc_edge (*safe_trap, *safe_trap, bddtrue, false);
          }
          return *safe_trap;
        }

        bool is_rejecting_scc (const std::vector<unsigned>& states) const {
          for (unsigned q : states)
            if (aut->state_is_accepting (q))
              return false;
          return true;
        }

        void clear_outgoing (unsigned q) {
          for (auto t = aut->out_iteraser (q); t; t.erase ())
            /* no-body */;
        }

        void rewrite_as_safe_trap (const std::vector<unsigned>& states,
                                   std::optional<unsigned>& safe_trap) {
          const unsigned trap = get_safe_trap (safe_trap);
          for (unsigned q : states) {
            clear_outgoing (q);
            aut->new_acc_edge (q, trap, bddtrue, false);
          }
        }

        void rewrite_as_losing_trap (const std::vector<unsigned>& states) {
          for (unsigned q : states) {
            clear_outgoing (q);
            aut->new_acc_edge (q, q, bddtrue);
          }
        }

        Aut& aut;
        const bdd input_support, output_support;
        const unsigned K;
    };
  }

  struct elevator {
      template <typename Aut>
      static auto make (Aut& aut, bdd input_support, bdd output_support, unsigned K) {
        return detail::elevator (aut, input_support, output_support, K);
      }
  };
}
