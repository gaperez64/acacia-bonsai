#pragma once

#include <spot/twaalgos/hoa.hh>
#include <spot/twaalgos/isdet.hh>
#include <spot/twaalgos/mask.hh>

#include <utils/verbose.hh>

#include "ios_precomputers/powset.hh"  // for power()


namespace aut_preprocessors {
  namespace detail {
    template <typename Aut>
    class surely_losing {
      public:
        surely_losing (Aut& aut, bdd input_support, bdd output_support, unsigned K) :
          aut {aut}, input_support {input_support}, output_support {output_support}, K {K}
        {}

        auto operator() () {
          // isuccs[q] is a set of pairs [input, transs] such that all t in
          // transs can be crossed from q by reading input.  Additionally, for
          // any output o, q.<input, o> is nonempty.
          auto isuccs = compute_isuccs ();

          // Initialize with c[q] = (q \in F)
          std::vector<unsigned> c (aut->num_states ());
          for (size_t q = 0; q < aut->num_states(); ++q)
            c[q] = (aut->state_is_accepting (q)) ? 1 : 0;

          bool changed = true;
          while (changed) {
            changed = false;

            for (size_t q = 0; q < aut->num_states(); ++q) {
              if (c[q] > K) // Already losing.
                continue;

              // Compute max_i min_o c[q.<io>]
              unsigned max = 0;
              for (auto& transitions : isuccs[q]) {
                unsigned min = K + 1;
                for (auto& [cond, to] : transitions)
                  min = std::min (min, c[to]);
                max = std::max (max, min);
              }
              max += (aut->state_is_accepting (q)) ? 1 : 0;
              if (c[q] != max) {
                changed = true;
                c[q] = max;
              }
            }
          }

          unsigned flushed = 0;
          for (size_t q = 0; q < aut->num_states(); ++q)
            if (c[q] > K) {
              flushed++;
              for (auto t = aut->out_iteraser (q); t; t.erase ())
                /* no-body */;
              verb_do (2, vout << "Flushing state " << q << std::endl);
              aut->new_acc_edge (q, q, bddtrue);
            }
          if (flushed) {
            aut->prop_universal(spot::trival::maybe ());
            aut->purge_dead_states ();
            aut->merge_states ();
          }
          verb_do (1, vout << "After flushing " << flushed
                   /*   */ << " states and cleaning, aut has " << aut->num_states ()
                   /*   */ << " states." << std::endl);
          verb_do (2, {
              vout << "Automaton after flushing surely losing states:\n";
              spot::print_hoa(vout, aut, nullptr) << std::endl;
            });
        }

      private:
        auto compute_isuccs () const {
          using trans_set_t = std::vector<std::pair<bdd, unsigned>>;
          using crossings_t = std::list<std::pair<bdd, trans_set_t>>;

          std::vector<std::list<trans_set_t>> isuccs (aut->num_states ());

          for (size_t q = 0; q < aut->num_states (); ++q) {
            auto input_power =
              ios_precomputers::detail::power<crossings_t> (
                transition_enumerator (aut, q, transition_formater::cond_and_dst (aut)),
                [this] (bdd b) {
                  return bdd_exist (b, output_support);
                });
            // We're not interested in inputs for which there's an output that
            // makes us leave the game.
            for (auto& [input, trans] : input_power) {
              bdd all_outs = bddfalse;
              for (auto& [cond, dst] : trans) {
                auto input_and_cond_over_outs =
                  bdd_exist (input & cond, input_support);
                all_outs |= input_and_cond_over_outs;
                if (all_outs == bddtrue)
                  break;
              }
              if (all_outs == bddtrue) // Keep this set of transitions
                isuccs[q].push_front (std::move (trans));
            }
          }
          return isuccs;
        }

        Aut& aut;
        const bdd input_support, output_support;
        const unsigned K;
    };
  }

  struct surely_losing {
      template <typename Aut>
      static auto make (Aut& aut, bdd input_support, bdd output_support, unsigned K) {
        return detail::surely_losing (aut, input_support, output_support, K);
      }
  };
}
