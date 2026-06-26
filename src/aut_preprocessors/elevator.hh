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

          spot::scc_info si (aut, spot::scc_info_options::TRACK_STATES);
          std::optional<unsigned> safe_trap;
          unsigned safe_collapsed = 0;
          unsigned losing_collapsed = 0;
          unsigned strategy_dependent_winning = 0;

          for (unsigned scc = 0; scc < si.scc_count (); ++scc) {
            const auto cls = classify_scc (si, scc);
            if (not cls.has_inner_edge or not cls.closed or not cls.deterministic)
              continue;

            const auto& states = si.states_of (scc);
            if (cls.rejecting) {
              rewrite_as_safe_trap (states, safe_trap);
              ++safe_collapsed;
              continue;
            }

            auto winner = controller_winner_for_scc (si, scc);
            if (not winner.has_value ())
              continue;

            if (*winner) {
              ++strategy_dependent_winning;
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

        struct scc_classification {
            bool has_inner_edge = false;
            bool closed = true;
            bool deterministic = true;
            bool rejecting = true;
        };

        scc_classification classify_scc (const spot::scc_info& si, unsigned scc) const {
          scc_classification ret;

          for (unsigned q : si.states_of (scc)) {
            if (aut->state_is_accepting (q))
              ret.rejecting = false;

            bdd covered = bddfalse;
            for (const auto& e : aut->out (q)) {
              if (e.cond == bddfalse)
                continue;

              if (aut->is_univ_dest (e)) {
                ret.closed = false;
                ret.deterministic = false;
                continue;
              }

              if (si.scc_of (e.dst) != scc) {
                ret.closed = false;
                continue;
              }

              ret.has_inner_edge = true;
              if ((covered & e.cond) != bddfalse)
                ret.deterministic = false;

              covered |= e.cond;
            }
          }

          return ret;
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
          auto res = acacia::spot_fastpath::deterministic_forbidden_fast_path (
              component, output_support, false, true);
          if (not res.conclusive or not res.current_output_player_winning_region.has_value ())
            return std::nullopt;

          const auto& winners = *res.current_output_player_winning_region;
          if (winners.size () < states.size ())
            return std::nullopt;

          const bool winner = winners[0];
          for (unsigned init = 1; init < states.size (); ++init)
            if (winners[init] != winner)
              return std::nullopt;

          return winner;
        }

        unsigned get_safe_trap (std::optional<unsigned>& safe_trap) {
          if (not safe_trap.has_value ()) {
            safe_trap = aut->new_state ();
            aut->new_acc_edge (*safe_trap, *safe_trap, bddtrue, false);
          }
          return *safe_trap;
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
