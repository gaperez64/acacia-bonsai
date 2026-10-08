#include "solver/translator_options.hh"

#include "solver/create_automaton.hh"
#include "solver/spot_lazy_game.hh"
#include <string_view>

#include <iostream>
#include <random>
#include <spot/tl/parse.hh>
#include <spot/twa/bdddict.hh>
#include <spot/twaalgos/complement.hh>
#include <spot/twaalgos/hoa.hh>
#include <spot/twaalgos/product.hh>
#include <spot/twaalgos/translate.hh>
#include <sstream>
#include <stdexcept>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}
namespace posets::vectors {
  size_t bool_threshold = 0;
}

namespace {
  bool configured_options_are_consumed () {
    auto options = acacia::translation::make_options ();
    spot::translator trans (spot::make_bdd_dict (), &options);
    try {
      acacia::translation::validate_options (options);
      return true;
    } catch (const std::runtime_error& err) {
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
    } catch (const std::runtime_error& err) {
      return std::string_view {err.what ()}.find ("simlu") != std::string_view::npos;
    }
    std::cerr << "misspelled translator option was silently accepted\n";
    return false;
  }

  void require (bool condition, const char* message) {
    if (!condition)
      throw std::runtime_error (message);
  }

  spot::twa_graph_ptr translate (spot::formula formula, const spot::bdd_dict_ptr& dict,
                                 acacia::translation::level level) {
    auto options = acacia::translation::make_options ();
    spot::translator trans (dict, &options);
    acacia::translation::validate_options (options);
    return create_automaton (formula, trans, spot::postprocessor::Small, level);
  }

  spot::formula generated_formula (std::mt19937& random, unsigned depth) {
    using F = spot::formula;
    if (!depth)
      return F::ap (random () % 2 ? "i" : "o");
    auto left = generated_formula (random, depth - 1);
    switch (random () % 9) {
      case 0: return F::Not (left);
      case 1: return F::X (left);
      case 2: return F::F (left);
      case 3: return F::G (left);
      case 4: return F::And ({left, generated_formula (random, depth - 1)});
      case 5: return F::Or ({left, generated_formula (random, depth - 1)});
      case 6: return F::U (left, generated_formula (random, depth - 1));
      case 7: return F::R (left, generated_formula (random, depth - 1));
      default: return F::Equiv (left, generated_formula (random, depth - 1));
    }
  }

  void equivalent_languages () {
    auto dict = spot::make_bdd_dict ();
    std::mt19937 random {207};
    for (unsigned n = 0; n < 64; ++n) {
      auto formula = generated_formula (random, 3);
      if (n % 2)
        formula = spot::formula::G (spot::formula::F (formula));
      const auto high = translate (formula, dict, spot::postprocessor::High);
      auto options = acacia::translation::make_options ();
      spot::translator original (dict, &options);
      acacia::translation::validate_options (options);
      const auto unchanged = create_automaton (formula, original, spot::postprocessor::Small);
      std::ostringstream before, after;
      spot::print_hoa (before, unchanged);
      spot::print_hoa (after, high);
      require (before.str () == after.str (), "explicit High changed the default graph");
      const auto not_high = spot::complement (high);
      for (auto level : {spot::postprocessor::Low, spot::postprocessor::Medium}) {
        const auto alternate = translate (formula, dict, level);
        const auto not_alternate = spot::complement (alternate);
        // Actual automaton complements, rather than another translation of !f.
        require (spot::product (high, not_alternate)->is_empty (),
                 "High language exceeds alternate");
        require (spot::product (alternate, not_high)->is_empty (),
                 "alternate language exceeds High");
        require (ACACIA_TRANSITION_ACCEPTANCE || alternate->prop_state_acc ().is_true (),
                 "alternate lost state acceptance");
        require (alternate->acc ().is_buchi (), "alternate lost ordinary Buchi acceptance");
      }
    }
  }

  void translated_certificate_replay () {
    using namespace acacia::spot_lazy_game;
    for (auto level :
         {spot::postprocessor::High, spot::postprocessor::Medium, spot::postprocessor::Low}) {
      const auto dict = spot::make_bdd_dict ();
      const auto formula = spot::parse_infix_psl ("!G(i <-> o)").f;
      auto aut = translate (formula, dict, level);
      if (!aut->prop_state_acc ().is_true ())
        aut = spot::sbacc (aut);
      const int input = aut->register_ap ("i"), output = aut->register_ap ("o");
      const letters::WorkerAlphabet alphabet {bdd_ithvar (input) & bdd_ithvar (output),
                                              bdd_ithvar (input),
                                              bdd_ithvar (output),
                                              {input, output}};
      const rows::FrozenAcacia graph {aut, aut->num_states ()};
      RowStore search {graph, {}};
      const auto certificate = Search {search, alphabet, 2}.solve ();
      require (certificate.status == forward_result_status::win_k, "translated copying game lost");
      // Replay reconstructs rows and predicates with a fresh store.
      RowStore replay {graph, {}};
      require (verify_winning_certificate (replay, alphabet, 2, certificate).value.has_value (),
               "translated certificate failed independent replay");
      auto bad = certificate;
      bad.nodes.at (bad.initial).choices.clear ();
      RowStore reject_missing {graph, {}};
      require (!verify_winning_certificate (reject_missing, alphabet, 2, bad).value,
               "missing strategy choices accepted");
      bad = certificate;
      bad.nodes.at (bad.initial).covered_inputs = bddfalse;
      RowStore reject_coverage {graph, {}};
      require (!verify_winning_certificate (reject_coverage, alphabet, 2, bad).value,
               "corrupted coverage accepted");
    }
  }
}  // namespace

int main () {
  try {
    require (configured_options_are_consumed () and typo_is_reported (),
             "option validation failed");
    equivalent_languages ();
    translated_certificate_replay ();
  } catch (const std::exception& err) {
    std::cerr << err.what () << '\n';
    return 1;
  }
  std::cout
      << "64 generated languages: High/Low/Medium inclusions, default graphs and replay checked\n";
  return 0;
}
