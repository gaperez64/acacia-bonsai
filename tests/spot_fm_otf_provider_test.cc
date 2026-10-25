#include "solver/spot_fm_otf_provider.hh"

#include <iostream>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/contains.hh>

namespace {
  namespace lazy = acacia::spot_lazy;
  using acacia::spot_fm_otf::Provider;
  unsigned checks = 0, failures = 0;
  void expect (const char* what, bool ok) {
    ++checks;
    if (!ok) {
      ++failures;
      std::cerr << "FAIL: " << what << '\n';
    }
  }
  spot::formula formula (const char* text) {
    auto parsed = spot::parse_infix_psl (text);
    if (parsed.format_errors (std::cerr))
      throw std::runtime_error ("invalid test formula");
    return parsed.f;
  }
}

int main () try {
  unsigned languages = 0, late = 0;
  for (const char* text :
       {"true",
        "false",
        "a",
        "G a",
        "F a",
        "GF a",
        "FG a",
        "a U b",
        "a M b",
        "a W b",
        "a R b",
        "a U (b U c)",
        "(a M b) M c",
        "GF a & GF b",
        "FG a & FG b",
        "GF a & FG b",
        "X (GF a & X GF b)",
        "X(a U b)",
        "a & X(b U c)",
        "a U (b & X(c U d))",
        "GF (a & X GF b) & FG (c | X FG d)",
        "GF (FG a & X GF b) & GF (FG c & X GF d)",
        "G(F a & ((a M b) U ((c U !d) M d)))",
        "!(GF a <-> FG b)",
        "(a & b) U (c | d)",
        "GF (p0 & a) & FG (p1 | b)",
        "G((a | b) -> F(c & d)) & GF(e | f) & FG(g & h)",
        "GF(a & b) & GF(a & c) & GF(d & e) & GF(d & f) & GF(g & h) & GF(g & i)"}) {
    auto f = formula (text);
    auto dict = spot::make_bdd_dict ();
    auto provider = std::make_shared<Provider> (f, dict);
    const auto initial_sets = provider->allocated_sets ();
    auto built = lazy::make_view ([&] { return provider; });
    expect ("factory is demand driven", built.value && provider->rows_explored () == 0);
    if (!built.value)
      continue;
    auto view = *built.value;
    const auto acceptance = view->acc ();
    auto graph =
        view->checked ([&] { return spot::make_twa_graph (view, spot::twa::prop_set::all ()); });
    expect ("complete expansion succeeds", graph.value.has_value ());
    if (!graph.value)
      continue;
    // Independent eager FM construction of the ORIGINAL formula, including its
    // original alphabet. This also checks preprocessing and label restoration.
    auto oracle = spot::ltl_to_tgba_fm (f, dict, true);
    expect (text, spot::are_equivalent (*graph.value, oracle));
    expect ("fixed Buchi acceptance", view->acc () == acceptance && view->num_sets () == 1);
    expect ("one FM expansion per cached formula row",
            provider->rows_explored () == view->underlying_rows ());
    late += provider->allocated_sets () > initial_sets;
    ++languages;
  }
  expect ("sample allocates promises after factory", late > 0);
  {
    auto dict = spot::make_bdd_dict ();
    auto f = formula ("GF a");
    auto provider = std::make_shared<Provider> (f, dict, lazy::Limits {}, 3);
    auto view = std::make_shared<lazy::LazyBuchiView> (provider);
    expect ("unused upper-bound sets are fulfilled on every edge",
            spot::are_equivalent (spot::make_twa_graph (view, spot::twa::prop_set::all ()),
                                  spot::ltl_to_tgba_fm (f, dict, true)));
  }
  {
    auto f = formula ("(a & b) U (c | d)");
    auto dict = spot::make_bdd_dict ();
    expect ("sample exercises Boolean relabeling",
            !acacia::spot_fm_otf::prepare (f, dict).labels.empty ());
  }
  {
    auto dict = spot::make_bdd_dict ();
    auto built = lazy::make_view ([&] {
      return std::make_shared<Provider> (formula ("a & X(b U c)"), dict, lazy::Limits {}, 0);
    });
    expect ("overflow hook admits initial unallocated promises", built.value.has_value ());
    if (built.value) {
      auto view = *built.value;
      auto result =
          view->checked ([&] { return spot::make_twa_graph (view, spot::twa::prop_set::all ()); });
      expect ("overflow declines without partial automaton",
              !result.value && result.status == lazy::Status::declined);
      expect ("overflow stays declined on cache hits",
              view->checked ([] { return true; }).status == lazy::Status::declined);
    }
  }
  for (bool states : {false, true}) {
    auto dict = spot::make_bdd_dict ();
    lazy::Limits limits;
    if (states)
      limits.max_states = 0;
    else
      limits.rows.max_rows = 0;
    auto result = lazy::attempt ([&] {
      auto provider = std::make_shared<Provider> (formula ("GF a"), dict, limits);
      auto view = std::make_shared<lazy::LazyBuchiView> (provider, limits);
      return spot::make_twa_graph (view, spot::twa::prop_set::all ());
    });
    expect ("state/row exhaustion is UNKNOWN",
            !result.value && result.status == lazy::Status::unknown);
  }
  std::cerr << "spot-fm-otf: " << languages << " language checks, " << late
            << " late allocations; " << checks - failures << " passed, " << failures
            << " failed\n";
  return failures ? 1 : 0;
} catch (const std::exception& e) {
  std::cerr << e.what () << '\n';
  return 1;
}
