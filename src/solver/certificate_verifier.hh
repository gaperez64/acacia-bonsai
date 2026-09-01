#pragma once

#include "actioners/direction.hh"

namespace acacia::solver_detail {

  /// Independently verify that `candidate` is a winning downset inside
  /// `envelope`.  Iterating the candidate itself is significant: construction
  /// may reduce its generators to a maximal antichain, and every generator
  /// that survives that reduction must discharge every input obligation.
  template <typename SetOfStates, typename InputOutputFwdActions, typename Actioner>
  bool verify_winning_certificate (
      const SetOfStates& envelope, const SetOfStates& candidate,
      const typename SetOfStates::value_type& initial,
      const InputOutputFwdActions& input_output_fwd_actions, Actioner& actioner,
      unsigned long long* forward_applications = nullptr,
      unsigned long long forward_application_budget = 0,
      bool* budget_exhausted = nullptr) {
    unsigned long long ignored_forward_applications = 0;
    unsigned long long& applications =
        forward_applications == nullptr ? ignored_forward_applications
                                        : *forward_applications;
    if (budget_exhausted != nullptr)
      *budget_exhausted = false;

    if (not candidate.contains (initial))
      return false;

    for (const auto& generator : candidate) {
      if (not envelope.contains (generator))
        return false;
      for (const auto& input_and_actions : input_output_fwd_actions) {
        bool discharged = false;
        for (const auto& action : input_and_actions.second) {
          if (forward_application_budget != 0
              and applications >= forward_application_budget) {
            if (budget_exhausted != nullptr)
              *budget_exhausted = true;
            return false;
          }
          ++applications;
          const auto image =
              actioner.apply (generator, action, actioners::direction::forward);
          if (candidate.contains (image)) {
            discharged = true;
            break;
          }
        }
        if (not discharged)
          return false;
      }
    }
    return true;
  }

}  // namespace acacia::solver_detail
