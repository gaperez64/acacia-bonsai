#include "solver/translator_options.hh"

#include <iostream>
#include <stdexcept>
#include <string_view>

#include <spot/twa/bdddict.hh>
#include <spot/twaalgos/translate.hh>

namespace {
  bool configured_options_are_consumed () {
    auto options = acacia::translation::make_options ();
    spot::translator trans (spot::make_bdd_dict (), &options);
    try {
      acacia::translation::validate_options (options);
      return true;
    }
    catch (const std::runtime_error& err) {
      std::cerr << "configured translator option was not consumed: " << err.what () << '\n';
      return false;
    }
  }

  bool typo_is_reported () {
    auto options = acacia::translation::make_options ();
    options.set ("simlu", 0);
    spot::translator trans (spot::make_bdd_dict (), &options);
    try {
      acacia::translation::validate_options (options);
    }
    catch (const std::runtime_error& err) {
      return std::string_view {err.what ()}.find ("simlu") != std::string_view::npos;
    }
    std::cerr << "misspelled translator option was silently accepted\n";
    return false;
  }
}  // namespace

int main () {
  return configured_options_are_consumed () and typo_is_reported () ? 0 : 1;
}
