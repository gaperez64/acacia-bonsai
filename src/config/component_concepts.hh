#pragma once

#include "configuration.hh"

#include <bddx.h>
#include <concepts>
#include <cstddef>
#include <optional>
#include <spot/twa/fwd.hh>
#include <spot/twa/twagraph.hh>
#include <type_traits>

#include <posets/concepts.hh>

namespace acacia::config::concepts {

  template <class D>
  concept AcaciaDownset = posets::Downset<D> && requires (D d, typename D::value_type v) {
    typename D::value_type;
    { d.size () } -> std::convertible_to<size_t>;
    { d.contains (v) } -> std::convertible_to<bool>;
    { d.begin () };
    { d.end () };
  };

  template <class Maker, class Aut>
  concept IOSPrecomputerMaker = requires (Aut aut, bdd inputs, bdd outputs) {
    Maker::make (aut, inputs, outputs);
  };

  template <class Factory>
  concept IOSPrecomputationFactory = requires (Factory factory) {
    factory ();
  };

  template <class Maker, class Aut, class InputsToIOS>
  concept ActionerMaker = requires (Aut aut, InputsToIOS ios, VECTOR_ELT_T k) {
    Maker::make (aut, ios, k);
  };

  template <class Actioner>
  concept ActionProvider = requires (Actioner actioner, VECTOR_ELT_T k) {
    actioner.actions ();
    actioner.setK (k);
  };

  template <class Maker, class FwdActions, class Actioner>
  concept InputPickerMaker = requires (FwdActions actions, Actioner actioner) {
    Maker::make (actions, actioner);
  };

  template <class Picker, class SetOfStates>
  concept InputPicker = requires (Picker picker, SetOfStates states) {
    picker (states);
  };

}  // namespace acacia::config::concepts
