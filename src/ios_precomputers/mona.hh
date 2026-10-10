#pragma once

#include "ios_precomputers/alphabet_census.hh"
#include "solver/diagnostics.hh"
#include "solver/equivariance_budget.hh"
#include "utils/transition_enumerator.hh"
#include <type_traits>
#include <unordered_set>

#include <bddx.h>
#include <bit>
#include <chrono>
#include <list>
#include <numeric>
#include <spot/twa/bdddict.hh>
#include <vector>

namespace ios_precomputers {
  namespace detail {
    /// `quotient` keeps only the first output path that reaches each residual
    /// BDD node at the state-variable frontier.  BuDDy nodes are canonical, so
    /// two paths reaching the same node denote the same endpoint relation and
    /// decode to identical transition sets; CPre unions over outputs and union
    /// is idempotent, so dropping the repeats cannot change the fixed point.
    ///
    /// Order is load-bearing and must be first-occurrence.  `input_pickers`
    /// scans an input's actions in order, breaks at the first action that keeps
    /// the state in the region, and splices that action to the front, so a
    /// sorted or hashed order is a different algorithm.  First-occurrence order
    /// is a subsequence of the order the traversal already produces.
    template <typename Aut, typename TransSet, bool quotient = false>
    class mona {
      public:
        mona (Aut aut, bdd input_support, bdd output_support)
          : aut {aut},
            input_support {input_support},
            output_support {output_support} {}

        using input_to_ios_t = typename std::list<std::pair<bdd, std::list<TransSet>>>;

        auto operator() () const {
          input_to_ios_t result;
          for_each_input ([&] (bdd input, auto& outputs) {
            result.emplace_back (input, std::move (outputs));
          });
          return result;
        }

        void restrict_inputs (bdd selected) { selected_inputs &= selected; }

        // The consumer sees a complete input class in traversal order. Its decoded
        // sets live only until the callback returns, independently of alphabet size.
        template <typename Consumer>
        void for_each_input (Consumer&& consume) const {
          // States are binary encoded using extra variables.
          // We allocate them via spot's bdd_dict (rather than calling
          // bdd_extvarnum directly) so the dict's internal var_refs
          // bookkeeping stays in sync. Going around the dict has been
          // observed to corrupt its state and either trigger
          // "maps are empty but var_refs is not" diagnostics, std::bad_alloc,
          // or glibc heap-corruption aborts on subsequent destruction.
          auto log_states = std::bit_width (aut->num_states ());
          auto dict = aut->get_dict ();
          // Owner token: any unique pointer scoped to this call works; we use
          // a stack-allocated dummy and pair it with an RAII guard so the
          // anonymous variables are released as soon as we are done with
          // them (all local BDDs that reference them are destroyed before
          // the guard fires, since they go out of scope at function exit).
          int owner_tag = 0;
          auto base_var = dict->register_anonymous_variables (2 * log_states, &owner_tag);
          struct anon_guard {
              spot::bdd_dict_ptr dict;
              const void* owner;
              ~anon_guard () { dict->unregister_all_my_variables (owner); }
          } guard {dict, &owner_tag};
          auto first_src_var = base_var, first_dst_var = base_var + log_states;
          // Projection removes declared APs that do not occur in the
          // automaton. If every output is unused, output_support is true and
          // has no root variable; the output segment is then simply empty.
          auto first_output = output_support == bddtrue ? first_src_var : bdd_var (output_support);

          std::vector<int> state_vars (2 * log_states);
          std::iota (state_vars.begin (), state_vars.end (), base_var);

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

          for (const auto& [formula, trans] :
               transition_enumerator (aut, transition_formater::src_and_dst (aut))) {
            acacia::equivariance_budget::checkpoint ();
            bdd_iopq = bdd_iopq | (formula & encode_src (trans.first) & encode_dst (trans.second));
          }

          // When the pq part of the BDD is reached, iterate through all its
          // satisfying valuations, decode the src and dst.
          auto add_src_dst = [&] (this const auto& self, TransSet& ts, bdd src_dsts) {
            for (auto mt : minterms_of (src_dsts, bdd_state_vars)) {
              acacia::equivariance_budget::checkpoint ();
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

#if ACACIA_ENABLE_DIAGNOSTICS
          // `decoded` is how many transition sets this expansion built;
          // `unique_decoded` is how many distinct residual relations those
          // decodes covered, per input class.  A quotient must decode exactly
          // the second number, so the pair is what validates stage A1.
          unsigned long long decoded = 0, unique_decoded = 0;
          const bool count_residuals = quotient or acacia::diagnostics::semantic_decode_census ();
#endif

          // Residual roots already decoded for the input class being expanded.
          // Only the quotient consults it; the diagnostics build also fills it
          // to count what a quotient would save.
          std::unordered_set<int> residual_roots;

          auto recurse_outputs = [&] (this const auto& self, auto& tss, bdd bdd_opq) {
            acacia::equivariance_budget::checkpoint ();
            if (bdd_opq == bddfalse)
              return;
            if (bdd_opq == bddtrue or bdd_var (bdd_opq) >= first_src_var) {
              if constexpr (quotient) {
                if (not residual_roots.emplace (bdd_opq.id ()).second)
                  return;
              }
#if ACACIA_ENABLE_DIAGNOSTICS
              ++decoded;
              // Under the quotient the set is already updated above, and every
              // decode is by construction a first occurrence.
              if constexpr (quotient)
                ++unique_decoded;
              else if (count_residuals and residual_roots.emplace (bdd_opq.id ()).second)
                ++unique_decoded;
#endif
              add_src_dst (tss.emplace_back (), bdd_opq);
            }
            else {
              self (tss, bdd_high (bdd_opq));
              self (tss, bdd_low (bdd_opq));
            }
          };

          const bool observed = acacia::phase_records_enabled ();
          size_t peak_bytes = 0, sets = 0, endpoints = 0, inputs = 0;
          auto recurse_inputs = [&] (this const auto& self, bdd bdd_iopq, bdd bdd_input) {
            acacia::equivariance_budget::checkpoint ();
            if (bdd_iopq == bddfalse)
              return;
            if (bdd_iopq == bddtrue or bdd_var (bdd_iopq) >= first_output) {
              if ((bdd_input & selected_inputs) == bddfalse)
                return;
              std::list<TransSet> outputs;
              // Per input class: two inputs reaching the same residual root
              // each decode it, because each input owns its own action list.
              // Guarded so the shipping build, which neither quotients nor
              // counts, does not touch the set at all.
              if constexpr (quotient)
                residual_roots.clear ();
#if ACACIA_ENABLE_DIAGNOSTICS
              else if (count_residuals)
                residual_roots.clear ();
#endif
              recurse_outputs (outputs, bdd_iopq);
              if (observed) {
                size_t bytes = sizeof (outputs) + sizeof (bdd_input);
                sets += outputs.size ();
                ++inputs;
                for (const auto& transitions : outputs) {
                  bytes += sizeof (transitions) +
                           transitions.capacity () * sizeof (typename TransSet::value_type);
                  endpoints += transitions.size ();
                }
                peak_bytes = std::max (peak_bytes, bytes);
                acacia::legacy_count ("decoded_set_peak_payload_bytes_estimate", peak_bytes);
                acacia::legacy_count ("decoded_set_payload_bytes_estimate", bytes);
                acacia::legacy_count ("decoded_sets", sets);
                acacia::legacy_count ("decoded_endpoints", endpoints);
                acacia::legacy_count ("decoded_inputs", inputs);
              }
              consume (bdd_input, outputs);
            }
            else {
              self (bdd_low (bdd_iopq), bdd_input & !bdd_ithvar (bdd_var (bdd_iopq)));
              self (bdd_high (bdd_iopq), bdd_input & bdd_ithvar (bdd_var (bdd_iopq)));
            }
          };

#if ACACIA_ENABLE_DIAGNOSTICS
          if (acacia::diagnostics::enabled ()) {
            // Count DAG frontier nodes without changing the path-enumerating
            // implementation being measured.  This runs before the expensive
            // enumeration so even a child killed during action construction
            // leaves the decisive census checkpoint behind.
            record_alphabet_census (bdd_iopq, first_output, first_src_var);
            if (acacia::diagnostics::alphabet_census_only ())
              return;
          }
#endif

          acacia::legacy_phase decoding ("io-decoding");
#if ACACIA_ENABLE_DIAGNOSTICS
          const auto decode_started = acacia::diagnostics::clock::now ();
#endif
          recurse_inputs (bdd_iopq, bddtrue);
#if ACACIA_ENABLE_DIAGNOSTICS
          acacia::diagnostics::set_decode_census (
              decoded, unique_decoded,
              (unsigned long long) std::chrono::duration_cast<std::chrono::milliseconds> (
                  acacia::diagnostics::clock::now () - decode_started)
                  .count ());
#endif
          if (observed)
            acacia::legacy_count ("decoded_set_payload_bytes_estimate", 0);
        }

      private:
        Aut aut;
        const bdd input_support, output_support;
        bdd selected_inputs = bddtrue;
    };

  }

  struct mona {
      // MONA encodes endpoints as bit pairs; carrying acceptance here needs a
      // separate design from the transition payload used by other precomputers.
      template <typename Aut, typename TransSet = std::vector<std::pair<unsigned, unsigned>>>
      static auto make (Aut aut, bdd input_support, bdd output_support) {
        return detail::mona<Aut, TransSet, false> (aut, input_support, output_support);
      }
  };
}
