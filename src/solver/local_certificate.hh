#pragma once

#include "actioners/direction.hh"
#include "configuration.hh"
#include "solver/certificate_verifier.hh"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace acacia::solver_detail {

  /// Sized by measurement, not taste.  Every verified certificate in the
  /// campaign fits: robot_grid2_2 needs 3,834 forward applications,
  /// finding_nemo_1 4,314, finding_nemo_2 139,143 -- but lift3 and
  /// finding_nemo_2 both fall to UNKNOWN at 20,000, so the budget cannot go
  /// much lower.  Above this it stops paying: at 2,000,000 the frozen sentinel
  /// Morning_f2774e0b goes from 13.70 s to 17.78 s and crosses the 17-second
  /// cap, while at 100,000 it costs 14.30 s and every win is still found.
  /// The scans this excludes are the expensive ones -- prioritized_arbiter
  /// needs 1.8 to 4.3 million, and scanning the safe set reached 145 million.
  inline constexpr unsigned long long default_local_certificate_forward_application_budget =
      100000;
  inline constexpr std::size_t default_local_certificate_cache_entry_budget = 200000;

  enum class local_certificate_status {
    unknown,
    win_certificate,
    root_refuted,
    budget_exhausted
  };

  template <typename SetOfStates>
  struct local_certificate_result {
      local_certificate_status status = local_certificate_status::unknown;
      std::optional<SetOfStates> win;
      unsigned long long forward_applications = 0, nodes = 0;
      int refuting_input = -1;
  };

  struct root_refutation_result {
      int refuting_input = -1;
      bool budget_exhausted = false;
  };

  namespace local_certificate_detail {

    template <typename State, typename InitialState>
    State materialize_state (const InitialState& initial) {
      if constexpr (std::is_same_v<std::remove_cvref_t<InitialState>, State>)
        return initial.copy ();
      else
        return State (initial);
    }

    template <typename SetOfStates, typename InputOutputFwdActions, typename Actioner>
    root_refutation_result root_refutation_impl (
        const SetOfStates& envelope, const typename SetOfStates::value_type& initial,
        const InputOutputFwdActions& input_output_fwd_actions, Actioner& actioner,
        unsigned long long& forward_applications,
        unsigned long long forward_application_budget) {
      int input_index = 0;
      for (const auto& input_and_actions : input_output_fwd_actions) {
        bool refutes_root = true;
        for (const auto& action : input_and_actions.second) {
          // Do not turn an incompletely scanned input class into a refutation:
          // an untested action may still keep the initial state in the region.
          if (forward_applications >= forward_application_budget)
            return {-1, true};
          ++forward_applications;
          const auto image =
              actioner.apply (initial, action, actioners::direction::forward);
          if (envelope.contains (image)) {
            refutes_root = false;
            break;
          }
        }
        if (refutes_root)
          return {input_index, false};
        ++input_index;
      }
      return {-1, false};
    }

    template <typename SetOfStates, typename InputOutputFwdActions, typename Actioner>
    class kernel_search {
        using state = typename SetOfStates::value_type;

      public:
        kernel_search (const SetOfStates& envelope,
                       const InputOutputFwdActions& input_output_fwd_actions,
                       Actioner& actioner, std::size_t generator_budget,
                       unsigned long long node_budget,
                       unsigned long long forward_application_budget,
                       local_certificate_result<SetOfStates>& result,
                       std::size_t cache_entry_budget)
          : envelope {envelope},
            input_output_fwd_actions {input_output_fwd_actions},
            actioner {actioner},
            generator_budget {generator_budget},
            node_budget {node_budget},
            forward_application_budget {forward_application_budget},
            result {result},
            cache_entry_budget {cache_entry_budget} {
          for (const auto& input_and_actions : input_output_fwd_actions) {
            input_action_base.push_back (total_actions);
            total_actions += input_and_actions.second.size ();
          }
          generator_ids.push_back (next_generator_id++);
        }

        [[nodiscard]] bool budget_was_exhausted () const { return exhausted; }

        bool search (std::vector<state>& generators) {
          if (result.nodes >= node_budget or
              result.forward_applications >= forward_application_budget) {
            exhausted = true;
            return false;
          }
          ++result.nodes;

          // A successor outside G is an unresolved obligation, not a losing
          // state.  Only successors outside `envelope` are inadmissible.
          bool found_obligation = false;
          std::vector<state> best_choices;
          for (std::size_t generator_ordinal = 0;
               generator_ordinal < generators.size (); ++generator_ordinal) {
            const auto& generator = generators[generator_ordinal];
            auto input_it = input_output_fwd_actions.begin ();
            for (std::size_t input_ordinal = 0;
                 input_it != input_output_fwd_actions.end ();
                 ++input_it, ++input_ordinal) {
              const auto& input_and_actions = *input_it;
              bool discharged = false;
              std::vector<state> choices;
              auto action_it = input_and_actions.second.begin ();
              for (std::size_t action_ordinal = 0;
                   action_it != input_and_actions.second.end ();
                   ++action_it, ++action_ordinal) {
                const auto& action = *action_it;
                if (result.forward_applications >= forward_application_budget) {
                  exhausted = true;
                  return false;
                }
                ++result.forward_applications;
                const std::size_t flat_action_index =
                    input_action_base[input_ordinal] + action_ordinal;
                cache_entry* cached = cache_for (
                    generator_ids[generator_ordinal], flat_action_index);
                std::optional<state> uncached_image;
                const state* image;
                bool in_envelope;
                if (cached == nullptr) {
                  uncached_image.emplace (actioner.apply (
                      generator, action, actioners::direction::forward));
                  image = &*uncached_image;
                  in_envelope = envelope.contains (*image);
                } else {
                  if (not cached->computed) {
                    cached->image.emplace (actioner.apply (
                        generator, action, actioners::direction::forward));
                    cached->in_envelope = envelope.contains (*cached->image);
                    cached->computed = true;
                  }
                  image = &*cached->image;
                  in_envelope = cached->in_envelope;
                }
                if (not in_envelope)
                  continue;
                if (covered (generators, *image)) {
                  discharged = true;
                  break;
                }
                if (std::ranges::none_of (
                        choices,
                        [image] (const state& choice) { return choice == *image; }))
                  choices.push_back (image->copy ());
              }

              if (discharged)
                continue;
              if (choices.empty ())
                return false;
              if (not found_obligation or choices.size () < best_choices.size ()) {
                found_obligation = true;
                best_choices = std::move (choices);
              }
            }
          }

          if (not found_obligation)
            return true;
          if (generators.size () >= generator_budget) {
            exhausted = true;
            return false;
          }

          // Prefer lower-rank successors, as in the offline kernel search.
          std::ranges::sort (best_choices, [] (const state& lhs, const state& rhs) {
            return rank (lhs) < rank (rhs);
          });
          for (const auto& choice : best_choices) {
            generators.push_back (choice.copy ());
            generator_ids.push_back (next_generator_id++);
            if (search (generators))
              return true;
            generators.pop_back ();
            generator_ids.pop_back ();
          }
          return false;
        }

      private:
        const SetOfStates& envelope;
        const InputOutputFwdActions& input_output_fwd_actions;
        Actioner& actioner;
        std::size_t generator_budget;
        unsigned long long node_budget;
        unsigned long long forward_application_budget;
        local_certificate_result<SetOfStates>& result;
        bool exhausted = false;
        std::vector<std::size_t> input_action_base;
        std::size_t total_actions = 0;
        std::vector<std::size_t> generator_ids;
        std::size_t next_generator_id = 0;

        struct cache_entry {
            bool computed = false;
            bool in_envelope = false;
            std::optional<state> image;
        };

        std::vector<std::vector<cache_entry>> image_cache;
        std::size_t cached_image_entries = 0;
        std::size_t cache_entry_budget;

        cache_entry* cache_for (std::size_t generator_id,
                                std::size_t flat_action_index) {
          if (generator_id >= image_cache.size ())
            image_cache.resize (generator_id + 1);
          auto& generator_cache = image_cache[generator_id];
          if (generator_cache.empty ()) {
            if (cached_image_entries >= cache_entry_budget or
                total_actions > cache_entry_budget - cached_image_entries)
              return nullptr;
            generator_cache.resize (total_actions);
            cached_image_entries += total_actions;
          }
          return &generator_cache[flat_action_index];
        }

        static long long rank (const state& value) {
          long long sum = 0;
          for (std::size_t i = 0; i < value.size (); ++i)
            sum += value[i];
          return sum;
        }

        static bool covered (const std::vector<state>& generators, const state& value) {
          return std::ranges::any_of (generators, [&value] (const state& generator) {
            return value.partial_order (generator).leq ();
          });
        }
    };

  }  // namespace local_certificate_detail

  /// Return the first input class whose every action leaves `envelope` from
  /// the initial state.  The action lists are traversed read-only.
  template <typename SetOfStates, typename InitialState, typename InputOutputFwdActions,
            typename Actioner>
  root_refutation_result root_refutation (
      const SetOfStates& envelope, const InitialState& initial,
      const InputOutputFwdActions& input_output_fwd_actions, Actioner& actioner,
      unsigned long long* forward_applications = nullptr,
      unsigned long long forward_application_budget =
          default_local_certificate_forward_application_budget) {
    using state = typename SetOfStates::value_type;
    const state initial_state =
        local_certificate_detail::materialize_state<state> (initial);
    unsigned long long ignored = 0;
    unsigned long long& applications =
        forward_applications == nullptr ? ignored : *forward_applications;
    return local_certificate_detail::root_refutation_impl (
        envelope, initial_state, input_output_fwd_actions, actioner, applications,
        forward_application_budget);
  }

  /// Search for a small inductive downset inside the current solver region.
  /// A failed bounded search is deliberately inconclusive.
  template <typename SetOfStates, typename InitialState, typename InputOutputFwdActions,
            typename Actioner>
  local_certificate_result<SetOfStates> find_local_certificate (
      const SetOfStates& envelope, const InitialState& initial,
      const InputOutputFwdActions& input_output_fwd_actions, Actioner& actioner,
      [[maybe_unused]] VECTOR_ELT_T k, std::size_t generator_budget,
      unsigned long long node_budget,
      unsigned long long forward_application_budget =
          default_local_certificate_forward_application_budget,
      std::size_t cache_entry_budget =
          default_local_certificate_cache_entry_budget) {
    using state = typename SetOfStates::value_type;
    local_certificate_result<SetOfStates> result;
    const auto root = root_refutation (
        envelope, initial, input_output_fwd_actions, actioner,
        &result.forward_applications, forward_application_budget);
    if (root.budget_exhausted) {
      result.status = local_certificate_status::budget_exhausted;
      return result;
    }
    result.refuting_input = root.refuting_input;
    if (result.refuting_input >= 0) {
      result.status = local_certificate_status::root_refuted;
      return result;
    }

    const state initial_state =
        local_certificate_detail::materialize_state<state> (initial);
    if (not envelope.contains (initial_state))
      return result;

    if (generator_budget == 0 or node_budget == 0 or
        result.forward_applications >= forward_application_budget) {
      result.status = local_certificate_status::budget_exhausted;
      return result;
    }

    std::vector<state> generators;
    generators.push_back (initial_state.copy ());
    local_certificate_detail::kernel_search search {
        envelope, input_output_fwd_actions, actioner, generator_budget, node_budget,
        forward_application_budget, result, cache_entry_budget};
    const bool found = search.search (generators);
    if (not found) {
      result.status = search.budget_was_exhausted ()
                          ? local_certificate_status::budget_exhausted
                          : local_certificate_status::unknown;
      return result;
    }

    // Constructing the downset reduces G to its maximal antichain.  Recompute
    // every obligation from scratch against that reduced representation before
    // treating the heuristic search as a certificate.
    std::vector<state> reduced;
    reduced.reserve (generators.size ());
    for (const auto& generator : generators)
      reduced.push_back (generator.copy ());
    SetOfStates candidate {std::move (reduced)};
    bool verification_budget_exhausted = false;
    if (not verify_winning_certificate (
            envelope, candidate, initial_state, input_output_fwd_actions, actioner,
            &result.forward_applications, forward_application_budget,
            &verification_budget_exhausted)) {
      if (verification_budget_exhausted)
        result.status = local_certificate_status::budget_exhausted;
      return result;
    }

    result.status = local_certificate_status::win_certificate;
    result.win.emplace (std::move (candidate));
    return result;
  }

}  // namespace acacia::solver_detail
