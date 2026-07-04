#pragma once

#include "config/component_concepts.hh"
#include "configuration.hh"
#include "solver/configured_components.hh"

#include <bddx.h>
#include <spot/twa/fwd.hh>
#include <type_traits>
#include <utility>

namespace acacia::config::checks {

  template <class SpecializedDownset>
  constexpr void check_solver_components () {
    using State = typename SpecializedDownset::value_type;
    using Aut = spot::twa_graph_ptr;
    using SelectedIOSPrecomputer = IOS_PRECOMPUTER;
    using SelectedActionerMaker = ACTIONER<State>;
    using SelectedInputPickerMaker = INPUT_PICKER;

    static_assert (concepts::AcaciaDownset<SpecializedDownset>);
    static_assert (std::is_default_constructible_v<SelectedIOSPrecomputer>);
    static_assert (std::is_default_constructible_v<SelectedActionerMaker>);
    static_assert (std::is_default_constructible_v<SelectedInputPickerMaker>);
    static_assert (concepts::IOSPrecomputerMaker<SelectedIOSPrecomputer, Aut>);

    using IOSFactory =
        decltype (SelectedIOSPrecomputer::make (std::declval<Aut> (), std::declval<bdd> (),
                                                std::declval<bdd> ()));
    static_assert (concepts::IOSPrecomputationFactory<IOSFactory>);

    using InputsToIOS = decltype (std::declval<IOSFactory> () ());
    static_assert (concepts::ActionerMaker<SelectedActionerMaker, Aut, InputsToIOS>);

    using Actioner = decltype (SelectedActionerMaker::make (
        std::declval<Aut> (), std::declval<InputsToIOS> (), std::declval<VECTOR_ELT_T> ()));
    static_assert (concepts::ActionProvider<Actioner>);

    using FwdActions = decltype (std::declval<Actioner&> ().actions ());
    static_assert (concepts::InputPickerMaker<SelectedInputPickerMaker, FwdActions&, Actioner&>);

    using Picker = decltype (SelectedInputPickerMaker::make (std::declval<FwdActions&> (),
                                                             std::declval<Actioner&> ()));
    static_assert (concepts::InputPicker<Picker, SpecializedDownset>);
  }

}  // namespace acacia::config::checks
