#include "solver/create_automaton.hh"
#include "guarded_event_univ_master.hh"

#include <iostream>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/contains.hh>
#include <spot/twaalgos/game.hh>
#include <spot/twaalgos/hoa.hh>
#include <spot/twaalgos/postproc.hh>
#include <spot/twaalgos/synthesis.hh>
#include <sstream>

namespace utils {
  voutstream vout;
  unsigned verbose = 0;
}

namespace {
  void require (bool condition, const char* message) {
    if (not condition)
      throw std::runtime_error (message);
  }

  spot::formula parse (const char* text) {
    auto parsed = spot::parse_infix_psl (text);
    require (parsed.errors.empty () && parsed.f, "test formula did not parse");
    return parsed.f;
  }

  std::string hoa (const spot::twa_graph_ptr& aut) {
    std::ostringstream out;
    spot::print_hoa (out, aut);
    return out.str ();
  }

  spot::twa_graph_ptr baseline (spot::formula f, const spot::bdd_dict_ptr& dict,
                                spot::postprocessor::output_pref preference, bool master = false) {
    auto options = acacia::translation::make_options ();
    spot::translator translator (dict, &options);
    acacia::translation::validate_options (options);
    if (master)
      return master_create_automaton (f, translator, preference);
#if ACACIA_TRANSITION_ACCEPTANCE
    translator.set_type (spot::postprocessor::Buchi);
    translator.set_pref (preference);
#else
    translator.set_type (spot::postprocessor::BA);
    translator.set_pref (preference | spot::postprocessor::SBAcc);
#endif
    auto aut = translator.run (f);
#if !ACACIA_TRANSITION_ACCEPTANCE
    if (aut->num_states () && !aut->prop_state_acc ().is_true ())
      aut = spot::sbacc (aut);
#endif
    return aut;
  }

  bool realizable (const spot::twa_graph_ptr& aut, const std::string& output) {
    spot::postprocessor post;
    post.set_type (spot::postprocessor::Parity);
    post.set_pref (spot::postprocessor::Deterministic | spot::postprocessor::Complete);
    auto deterministic = post.run (aut);
    const int var = deterministic->register_ap (output);
    spot::set_synthesis_outputs (deterministic, bdd_ithvar (var));
    spot::synthesis_info info;
    auto game = spot::split_2step (deterministic, info);
    return spot::solve_game (game, info);
  }

  spot::formula generated (unsigned count, unsigned kind, const std::string& input,
                           const std::string& output) {
    std::vector<spot::formula> eventualities;
    auto shifted = spot::formula::ap (input);
    for (unsigned i = 0; i < count; ++i) {
      eventualities.push_back (spot::formula::F (shifted));
      shifted = spot::formula::X (shifted);
    }
    const auto padding = spot::formula::Or (eventualities);
    const auto goal = spot::formula::G (spot::formula::F (spot::formula::ap (output)));
    switch (kind) {
      case 0: return spot::formula::Implies (padding, goal);
      case 1: return spot::formula::And ({padding, goal});
      case 2: return spot::formula::Implies (goal, padding);
      default:
        return spot::formula::And (
            {padding, spot::formula::G (spot::formula::Not (spot::formula::ap (output)))});
    }
  }
}

int main (int argc, char** argv) {
  try {
    using namespace acacia::translation;
    if (argc == 2 && std::string_view {argv[1]} == "--width") {
      std::cout << acceptance_set_width () << '\n';
      return 0;
    }
    if (argc == 4) {
      acacia::phase_records_start_writer ();
      acacia::worker_record record;
      record.pid = getpid ();
      acacia::active_worker_record () = &record;
      auto f = parse (argv[2]);
      const auto dict = spot::make_bdd_dict ();
      const auto preference = static_cast<spot::postprocessor::output_pref> (std::stoi (argv[3]));
      if (std::string_view {argv[1]} == "--master")
        std::cout << hoa (baseline (f, dict, preference, true));
      else {
        auto options = make_options ();
        spot::translator translator (dict, &options);
        validate_options (options);
        std::cout << hoa (create_automaton (
            f, translator, preference, std::string_view {argv[1]} == "--expanded"
                                          ? event_univ_policy::expanded
                                          : event_univ_policy::guarded));
      }
      acacia::phase_records_close ();
      acacia::active_worker_record () = nullptr;
      return 0;
    }
    const auto width = acceptance_set_width ();
    require (
        !needs_event_univ (width - 1) && !needs_event_univ (width) && needs_event_univ (width + 1),
        "guard boundary changed");
    require (distinct_promises (parse ("(F a) & X (F a) & G (F a)")) == 1,
             "shared promise counted more than once");
    require (distinct_promises (parse ("(a U b) & (c M d) & F e")) == 3,
             "F/U/M promises were not all counted");
    require (distinct_promises (parse ("!((a W b) & (c R d) & G e)")) == 3,
             "negative promises were not normalized");
    require (distinct_promises (parse ("(F a) <-> (F b)")) == 2,
             "abbreviated Boolean operators were not normalized");

    unsigned cases = 0, below = 0, above = 0, real = 0, unreal = 0, expanded_only = 0;
    for (const auto count : {1U, width - 2, width - 1, width, width + 1})
      for (unsigned kind = 0; kind < 4; ++kind)
        for (const bool renamed : {false, true}) {
          auto f = generated (count, kind, renamed ? "sensor" : "request",
                              renamed ? "actuator" : "response");
          const auto dict = spot::make_bdd_dict ();
          const auto promises = distinct_promises (f);
          const bool guarded = needs_event_univ (promises);
          guarded ? ++above : ++below;
          const auto old = baseline (f, dict, spot::postprocessor::Small);
          const auto input = event_univ_input (f, promises, event_univ_policy::expanded);
          if (!guarded && input.changed)
            ++expanded_only;
          // Direct formula equivalence uses Spot's own default translator, whose
          // acceptance width is also finite. Keep those generated checks bounded;
          // all boundary sizes below are checked via the explicit BA translations.
          if (count == 1)
            require (spot::are_equivalent (f, input.formula), "rewrite changed formula language");
          const auto favored = baseline (input.formula, dict, spot::postprocessor::Small);
          require (spot::are_equivalent (old, favored), "option changed the automaton language");
          const auto output = renamed ? "actuator" : "response";
          const bool verdict = realizable (old, output);
          require (verdict == realizable (favored, output), "option changed realizability");
          require (verdict == (kind == 0 || kind == 2), "independent game oracle disagrees");
          verdict ? ++real : ++unreal;
          for (const auto preference : {spot::postprocessor::Small, spot::postprocessor::Any,
                                        spot::postprocessor::Deterministic}) {
            auto options = make_options ();
            spot::translator translator (dict, &options);
            validate_options (options);
            const auto before = f;
            const auto current = create_automaton (f, translator, preference);
            require (f == before, "translation mutated the bound worker formula");
            const auto expected = baseline (f, dict, preference, true);
            require (hoa (current) == hoa (expected),
                     "guarded translation differs from reference");
            const auto expanded = create_automaton (f, translator, preference,
                                                     event_univ_policy::expanded);
            require (f == before, "expanded translation mutated the bound worker formula");
            require (spot::are_equivalent (current, expanded),
                     "expanded automaton changed language");
            require (hoa (expanded) == hoa (baseline (input.formula, dict, preference)),
                     "expanded translation differs from selected input");
            if (preference == spot::postprocessor::Small) {
              require (realizable (current, output) == verdict, "guarded verdict changed");
              require (realizable (expanded, output) == verdict, "expanded verdict changed");
            }
            if (!guarded)
              require (hoa (current) == hoa (baseline (f, dict, preference)),
                       "below-guard automaton is not byte-identical to master");
          }
          ++cases;
        }
    require (expanded_only > 0, "no below-width changed cases were checked");
    for (const auto text : {"1", "0", "G a", "F a", "(F a) & X (F a) & G (F a)",
                            "(a U b) & (c M d) & F e", "!((a W b) & (c R d) & G e)",
                            "(F a) <-> (F b)", "G F (a | G b)", "F (a U b) | G (c M d)"}) {
      auto f = parse (text);
      const auto before = f;
      const auto input = event_univ_input (f, distinct_promises (f), event_univ_policy::expanded);
      require (spot::are_equivalent (f, input.formula), "boundary formula language changed");
      if (favor_event_univ (f) == favor_event_univ (f, false))
        require (input.formula == f, "unchanged probe replaced the original formula");
      const auto dict = spot::make_bdd_dict ();
      int owner = 0;
      dict->register_proposition (spot::formula::ap ("unused_input"), &owner);
      dict->register_proposition (spot::formula::ap ("unused_output"), &owner);
      {
        auto options = make_options ();
        spot::translator translator (dict, &options);
        validate_options (options);
        const auto aut = create_automaton (f, translator, spot::postprocessor::Small,
                                           event_univ_policy::expanded);
        require (spot::are_equivalent (aut, before), "boundary automaton language changed");
        require (f == before, "boundary translation mutated formula");
      }
      dict->unregister_all_my_variables (&owner);
    }
    {
      const auto dict = spot::make_bdd_dict ();
      auto options = make_options ();
      spot::translator translator (dict, &options);
      validate_options (options);
      auto high = generated (width + 1, 1, "sensor", "actuator");
      create_automaton (high, translator);
      create_automaton (high, translator, ACACIA_TRANSLATION_PREF, event_univ_policy::expanded);
      auto low = parse ("G (sensor -> F actuator)");
      const auto current = create_automaton (low, translator);
      require (hoa (current) == hoa (baseline (low, dict, ACACIA_TRANSLATION_PREF)),
               "guard leaked into a later below-width translation");
      const auto input =
          event_univ_input (low, distinct_promises (low), event_univ_policy::expanded);
      const auto expanded = create_automaton (low, translator, ACACIA_TRANSLATION_PREF,
                                               event_univ_policy::expanded);
      require (hoa (expanded) == hoa (baseline (input.formula, dict, ACACIA_TRANSLATION_PREF)),
               "expanded options leaked into a later ordinary translation");
    }
    std::cout << cases << " paired language/verdict cases; " << below << " below/on guard, "
              << above << " above, " << expanded_only << " expanded-only changed; "
              << real << " REAL, " << unreal << " UNREAL\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
}
