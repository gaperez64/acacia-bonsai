// Frozen b9b95ef6 MONA and actioner implementations for differential tests.
// Keep independent of the streaming implementation.
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

namespace mona_reference::ios_precomputers {
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

          auto recurse_inputs = [&] (this const auto& self, auto& i_to_tss, bdd bdd_iopq,
                                     bdd bdd_input) {
            acacia::equivariance_budget::checkpoint ();
            if (bdd_iopq == bddfalse)
              return;
            if (bdd_iopq == bddtrue or bdd_var (bdd_iopq) >= first_output) {
              using vt = typename std::remove_cvref_t<decltype (i_to_tss)>::value_type;
              i_to_tss.emplace_back (vt (bdd_input, {}));
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
              recurse_outputs (i_to_tss.back ().second, bdd_iopq);
            }
            else {
              self (i_to_tss, bdd_low (bdd_iopq), bdd_input & !bdd_ithvar (bdd_var (bdd_iopq)));
              self (i_to_tss, bdd_high (bdd_iopq), bdd_input & bdd_ithvar (bdd_var (bdd_iopq)));
            }
          };

#if ACACIA_ENABLE_DIAGNOSTICS
          if (acacia::diagnostics::enabled ()) {
            // Count DAG frontier nodes without changing the path-enumerating
            // implementation being measured.  This runs before the expensive
            // enumeration so even a child killed during action construction
            // leaves the decisive census checkpoint behind.
            ::ios_precomputers::record_alphabet_census (bdd_iopq, first_output, first_src_var);
            if (acacia::diagnostics::alphabet_census_only ())
              return input_to_ios_t {};
          }
#endif

          acacia::legacy_phase decoding ("io-decoding");
          input_to_ios_t i_to_tss;
#if ACACIA_ENABLE_DIAGNOSTICS
          const auto decode_started = acacia::diagnostics::clock::now ();
#endif
          recurse_inputs (i_to_tss, bdd_iopq, bddtrue);
#if ACACIA_ENABLE_DIAGNOSTICS
          acacia::diagnostics::set_decode_census (
              decoded, unique_decoded,
              (unsigned long long) std::chrono::duration_cast<std::chrono::milliseconds> (
                  acacia::diagnostics::clock::now () - decode_started)
                  .count ());
#endif
          if (acacia::phase_records_enabled ()) {
            size_t bytes = sizeof (i_to_tss), sets = 0, endpoints = 0;
            for (const auto& [input, outputs] : i_to_tss) {
              (void) input;
              bytes += sizeof (typename input_to_ios_t::value_type);
              sets += outputs.size ();
              for (const auto& transitions : outputs) {
                bytes += sizeof (transitions) +
                         transitions.capacity () * sizeof (typename TransSet::value_type);
                endpoints += transitions.size ();
              }
            }
            // Append-only decoded containers: accounted payload peaks at retention.
            acacia::legacy_count ("decoded_set_peak_payload_bytes_estimate", bytes);
            acacia::legacy_count ("decoded_set_payload_bytes_estimate", bytes);
            acacia::legacy_count ("decoded_sets", sets);
            acacia::legacy_count ("decoded_endpoints", endpoints);
          }
          return i_to_tss;
        }

      private:
        Aut aut;
        const bdd input_support, output_support;
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

#pragma once

#include "actioners/direction.hh"
#include "actioners/profile_dominance.hh"
#include "configuration.hh"
#include "posets/utils/vector_mm.hh"
#include "posets/vectors/traits.hh"
#include "solver/diagnostics.hh"
#include "solver/equivariance_budget.hh"
#include "solver/phase_observation.hh"
#include "solver/transition_payload.hh"

#include <algorithm>
#include <bddx.h>
#include <list>
#include <set>
#include <utility>
#include <vector>

namespace mona_reference::actioners {
  using ::actioners::direction;
  namespace detail {
    template <typename State, typename Aut, typename IToIOs>
    class standard {
      public:  // types
        using action =
            std::vector<std::pair<unsigned, bool>>;  // All these pairs are unique by construction.
        using action_vec = std::vector<action>;      // Vector indexed by state number
        using action_vecs = std::list<action_vec>;
        using input_and_actions = std::pair<bdd, action_vecs>;
        /**
         * Later, we'll be using an std::set of input_and_actions. We DO NOT
         * want to end up comparing bdds using the default std::less because
         * that's an actual bdd operation (not a comparison). This is why we
         * have a comparison functor which ignores the bdd below.
         */
        struct compare_actions {
            bool operator() (const input_and_actions& x, const input_and_actions& y) const {
              return (x.second < y.second);
            }
        };
        using input_and_actions_set = std::list<input_and_actions>;

      public:
        standard (const Aut& aut, const IToIOs& inputs_to_ios, VECTOR_ELT_T K)
          : aut {aut},
            K {(VECTOR_ELT_T) K},
            apply_out (aut->num_states ()),
            backward_reset (aut->num_states ()) {
          const bool observed = acacia::phase_records_enabled ();
          acacia::legacy_phase constructing ("action-construction");
          // Non boolean
          std::fill_n (backward_reset.begin (), posets::vectors::bool_threshold,
                       (VECTOR_ELT_T) (K - 1));
          // Boolean
          std::fill_n (backward_reset.begin () + posets::vectors::bool_threshold,
                       aut->num_states () - posets::vectors::bool_threshold, (VECTOR_ELT_T) 0);

          std::set<input_and_actions, compare_actions> ioset;
          size_t observed_retained = 0, observed_peak = 0;

          // inputs_to_ios maps each input i to transition sets.  Each set
          // corresponds to an i-compatible IO x and contains every transition
          // p -> q compatible with x.  The IO's BDD is stored alongside it.
          for (const auto& [input, ios] : inputs_to_ios) {
            acacia::equivariance_budget::checkpoint ();
            // input: bdd
            // ios: transition sets and their IOs
            std::list<action_vec> fwd_actions;
            // action_vec : vector<vector<pair<unsigned int, bool>>>
            for (const auto& transset : ios) {
              acacia::equivariance_budget::checkpoint ();
              // transset: transitions compatible with one IO
              // Turn this into a vector that maps q to a list of tuples
              // (p, increment).  The increment comes from the edge when
              // transition acceptance is enabled, and from q otherwise.
              fwd_actions.push_back (compute_action_vec (transset));
              // type that is being inserted: action_vec (ios_precomputers/standard.hh)
              // with current configuration.hh at the time of writing
            }
            // per input: list (one element per compatible IO) of actions
            // what is being inserted = pair<bdd, action_vec> with current configuration.hh at the
            // time of writing
            size_t pending_bytes = 0;
            if (observed) {
              pending_bytes = sizeof (input_and_actions);
              for (const auto& action : fwd_actions) {
                pending_bytes += sizeof (action) +
                                 action.capacity () * sizeof (typename action_vec::value_type);
                for (const auto& row : action)
                  pending_bytes += row.capacity () * sizeof (typename action::value_type);
              }
              observed_peak = std::max (observed_peak, observed_retained + pending_bytes);
            }
            const auto inserted = ioset.insert (std::pair (input, std::move (fwd_actions)));
            if (observed && inserted.second)
              observed_retained += pending_bytes;
          }

          for (auto it = ioset.begin (); it != ioset.end ();) {
            // what is being inserted:
            // pair<bdd, list<vector<vector<pair<unsigned int, bool>>>>>
            // -> for every input, a list (one per compatible IO) of actions
            // where an action maps each state q to a list of (p, increment) tuples
            input_output_fwd_actions.push_back (std::move (ioset.extract (it++).value ()));
          }

#if ACACIA_PROFILE_DOMINANCE
          // Keep pruning after extraction: the set's compare_actions ordering and
          // merging are part of the measured input order.  Pruning earlier would
          // change its sort key and confound dominance gains with order changes.
          for (auto& input_and_actions : input_output_fwd_actions) {
            [[maybe_unused]] const auto stats =
                ::actioners::profile_dominance::prune (input_and_actions.second);
# if ACACIA_ENABLE_DIAGNOSTICS
            if (auto* diag = acacia::diagnostics::current ()) {
              diag->profile_actions_before += stats.actions_before;
              diag->profile_actions_after += stats.actions_after;
              diag->profile_dominance_tests += stats.pair_tests;
              diag->profile_dominance_endpoint_visits += stats.endpoint_visits;
              diag->profile_dominance_declined += stats.declined ? 1 : 0;
              diag->profile_dominance_ms += stats.elapsed_ms;
            }
# endif
          }
#endif
          if (observed) {
            acacia::legacy_count ("action_construction_peak_payload_bytes_estimate",
                                  observed_peak);
            acacia::legacy_count ("action_table_payload_bytes_estimate",
                                  acacia::legacy_action_bytes (input_output_fwd_actions));
            size_t payloads = 0, endpoints = 0;
            for (const auto& [input, actions] : input_output_fwd_actions) {
              (void) input;
              payloads += actions.size ();
              for (const auto& action : actions)
                for (const auto& row : action)
                  endpoints += row.size ();
            }
            acacia::legacy_count ("action_payloads", payloads);
            acacia::legacy_count ("action_endpoints", endpoints);
          }
        }

        void setK (VECTOR_ELT_T newK) {
          K = (VECTOR_ELT_T) newK;
          std::fill_n (backward_reset.begin (), posets::vectors::bool_threshold,
                       (VECTOR_ELT_T) (K - 1));
        }

        auto& actions () { return input_output_fwd_actions; }

        State apply (const State& m, const action_vec& avec,
                     direction dir) /* __attribute__((pure)) */ {
          acacia::equivariance_budget::checkpoint ();
          if (dir == direction::forward)
            apply_out.assign (m.size (), (VECTOR_ELT_T) -1);
          else
            apply_out = backward_reset;

          for (size_t p = 0; p < m.size (); ++p) {
            for (const auto& [q, p_final] : avec[p]) {
              if (dir == direction::forward) {
                if (m[q] != -1)
                  apply_out[p] = std::max (
                      apply_out[p],
                      std::min (K, (VECTOR_ELT_T) (m[q] + (VECTOR_ELT_T) (p_final ? 1 : 0))));
              }
              else if (apply_out[q] != -1)
                apply_out[q] =
                    std::min (apply_out[q],
                              std::max ((VECTOR_ELT_T) -1,
                                        (VECTOR_ELT_T) (m[p] - (VECTOR_ELT_T) (p_final ? 1 : 0))));

              // If we reached the extreme value, stop going through states.
              if (dir == direction::forward && apply_out[p] == K)
                break;
            }
          }

          return State (apply_out);
        }

      private:
        const Aut& aut;
        VECTOR_ELT_T K;
        posets::utils::vector_mm<VECTOR_ELT_T> apply_out, backward_reset;
        input_and_actions_set input_output_fwd_actions;

        template <typename Set>
        auto compute_action_vec (const Set& transset) {
          // create action_vec and include transset.second = the IO if needed
          action_vec ret_fwd (aut->num_states ());

          TODO (
              "We have two representations of the same thing here; "
              "see if we can narrow it down to one.");

          // ret_fwd: vector<vector<pair<unsigned int, bool>>>
          // first index = state q, map each state q to a list of tuples (p, increment)

          for (const auto& t : transset)
            ret_fwd[acacia::transitions::dest (t)].push_back (std::make_pair (
                acacia::transitions::source (t), acacia::transitions::increment (t, aut)));

          return ret_fwd;
        }
    };
  }

  template <typename State>
  struct standard {
      template <typename Aut, typename IToIOs>
      static auto make (const Aut& aut, const IToIOs& itoios, VECTOR_ELT_T K) {
        return detail::standard<State, Aut, IToIOs> (aut, itoios, K);
      }
  };
}
