#pragma once


#include <type_traits>
#include <numeric>
#include <vector>
#include <list>
#include <bddx.h>

#include "utils/transition_enumerator.hh"

namespace ios_precomputers {
  namespace detail {
    template <typename Aut, typename TransSet>
    class mona {
      public:
        mona (Aut aut, bdd input_support, bdd output_support)
          : aut {aut},
            input_support {input_support},
            output_support {output_support}
        {}

        using input_to_ios_t = typename std::list<std::pair<bdd, std::list<TransSet>>>;

        auto operator() () const {

          // States are binary encoded using extra variables.
          auto log_states = std::bit_width (aut->num_states ());
          auto base_var = bdd_extvarnum (2 * log_states);
          auto  first_output = bdd_var (output_support),
            first_src_var = base_var,
            first_dst_var = base_var + log_states;

          std::vector<int> state_vars (2 * log_states);
          std::ranges::iota (state_vars, base_var);

          // TODO Not sure if it's worth caching.
          auto encode_src = [&] (size_t s) {
            return bdd_ibuildcube (s, log_states, state_vars.data ());
          };
          auto encode_dst = [&] (size_t s) {
            return bdd_ibuildcube (s, log_states, state_vars.data () + log_states);
          };

          auto bdd_state_vars = bdd_ibuildcube (-1, 2 * log_states, state_vars.data ());

          // Create the mona BDD.
          bdd bdd_iopq = bddfalse;

          for (const auto& [formula, trans] : transition_enumerator (aut, transition_formater::src_and_dst (aut)))
            bdd_iopq = bdd_iopq |
              (formula & encode_src (trans.first) & encode_dst (trans.second));

          // When the pq part of the BDD is reached, iterate through all its
          // satisfying valuations, decode the src and dst.
          auto add_src_dst = [&] (this const auto& self, TransSet& ts, bdd src_dsts) {
            for (auto mt : minterms_of (src_dsts, bdd_state_vars)) {
              unsigned from = 0, to = 0;
              while (mt != bddtrue) {
                bool true_lit = (bdd_low (mt) == bddfalse);
                if (bdd_var (mt) < first_dst_var)
                  from = (from << 1) | true_lit;
                else
                  to = (to << 1) | true_lit;
                mt = true_lit ? bdd_high (mt) : bdd_low (mt);
              }
              ts.emplace_back (from, to);
            }
          };

          auto recurse_outputs = [&] (this const auto& self, auto& tss, bdd bdd_opq) {
            if (bdd_opq == bddfalse)
              return;
            // TODO There may be more than one way to reach this bdd_pq; cache.
            if (bdd_opq == bddtrue or bdd_var (bdd_opq) >= first_src_var)
              add_src_dst (tss.emplace_back (), bdd_opq);
            else {
              self (tss, bdd_high (bdd_opq));
              self (tss, bdd_low (bdd_opq));
            }
          };

          auto recurse_inputs = [&] (this const auto& self, auto& i_to_tss, bdd bdd_iopq, bdd bdd_input) {
            if (bdd_iopq == bddfalse)
              return;
            if (bdd_iopq == bddtrue or bdd_var (bdd_iopq) >= first_output) {
              using vt = typename std::remove_cvref_t<decltype (i_to_tss)>::value_type;
              i_to_tss.emplace_back (vt (bdd_input, {}));
              recurse_outputs (i_to_tss.back ().second, bdd_iopq);
            }
            else {
              self (i_to_tss, bdd_low (bdd_iopq), bdd_input & !bdd_ithvar (bdd_var (bdd_iopq)));
              self (i_to_tss, bdd_high (bdd_iopq), bdd_input & bdd_ithvar (bdd_var (bdd_iopq)));
            }
          };

          input_to_ios_t i_to_tss;
          recurse_inputs (i_to_tss, bdd_iopq, bddtrue);
          return i_to_tss;
        }

      private:
        Aut aut;
        const bdd input_support, output_support;
    };

  }

  struct mona {
      template <typename Aut, typename TransSet = std::vector<std::pair<unsigned, unsigned>>>
      static auto make (Aut aut, bdd input_support, bdd output_support) {
        return detail::mona<Aut, TransSet> (aut, input_support, output_support);
      }
  };
}
