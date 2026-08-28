#include "solver/mp_nba.hh"

#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include <spot/misc/optionmap.hh>
#include <spot/tl/parse.hh>
#include <spot/tl/randomltl.hh>
#include <spot/twaalgos/contains.hh>

namespace {

  std::optional<spot::formula> parse (std::string_view text) {
    auto parsed = spot::parse_infix_psl (std::string {text});
    if (not parsed.f or not parsed.errors.empty ()) {
      std::cerr << "could not parse " << text << '\n';
      parsed.format_errors (std::cerr);
      return std::nullopt;
    }
    return parsed.f;
  }

  bool check_equivalence (std::string_view label, spot::formula formula,
                          bool extraction_required) {
    auto dict = spot::make_bdd_dict ();
    auto cubes = acacia::mp_nba::extract_cubes (formula, dict, 1024);
    if (not cubes) {
      if (extraction_required)
        std::cerr << label << ": extract_cubes rejected the formula\n";
      return not extraction_required;
    }

    auto nba = acacia::mp_nba::build_violation_nba (*cubes, dict);
    try {
      if (spot::are_equivalent (nba, spot::formula::Not (formula)))
        return true;
    }
    catch (const std::exception& error) {
      std::cerr << label << ": equivalence check threw: "
                << error.what () << '\n';
      return false;
    }

    std::cerr << label << ": violation NBA is not equivalent to the negation\n";
    return false;
  }

  bool check_targeted_formulas () {
    constexpr std::string_view formulas[] = {
      "true",
      "false",
      "GF a",
      "FG a",
      "GF a | FG b",
      "GF a & GF b",
      "(GF a | FG b) & (GF c | FG d)",
      "GF a | GF b",
      "FG a & FG b",
      "GF(a & b)",
    };

    bool ok = true;
    for (std::string_view text : formulas) {
      auto formula = parse (text);
      if (not formula) {
        ok = false;
        continue;
      }
      ok &= check_equivalence (text, *formula, true);
    }
    return ok;
  }

  bool check_rejected_formula () {
    auto formula = parse ("a U b");
    if (not formula)
      return false;
    auto dict = spot::make_bdd_dict ();
    if (not acacia::mp_nba::extract_cubes (*formula, dict, 1024))
      return true;
    std::cerr << "a U b: extract_cubes accepted unsupported input\n";
    return false;
  }

  bool check_cube_cap () {
    auto formula = parse ("GF a & GF b & GF c");
    if (not formula)
      return false;
    auto dict = spot::make_bdd_dict ();
    if (not acacia::mp_nba::extract_cubes (*formula, dict, 2))
      return true;
    std::cerr << "cube cap: formula with three clauses passed a cap of two\n";
    return false;
  }

  bool check_extraction_stats () {
    auto formula = parse ("GF a | FG b");
    if (not formula)
      return false;

    auto dict = spot::make_bdd_dict ();
    acacia::mp_nba::extraction_options options;
    acacia::mp_nba::extraction_stats stats;
    auto cubes = acacia::mp_nba::extract_cubes (*formula, dict, options,
                                                 &stats);
    if (not cubes
        or stats.status != acacia::mp_nba::extraction_status::accepted
        or stats.nodes_before == 0 or stats.nodes_after_delta2 == 0
        or stats.cubes != 1 or stats.predicates != 2
        or stats.max_inf_width != 1) {
      std::cerr << "stats: accepted extraction reported incorrect metrics\n";
      return false;
    }

    options.node_cap = 1;
    if (acacia::mp_nba::extract_cubes (*formula, dict, options, &stats)
        or stats.status != acacia::mp_nba::extraction_status::node_cap
        or stats.nodes_before == 0 or stats.nodes_after_delta2 <= 1) {
      std::cerr << "stats: node cap was not reported precisely\n";
      return false;
    }

    options.node_cap = 0;
    options.cube_cap = 0;
    if (acacia::mp_nba::extract_cubes (*formula, dict, options, &stats)
        or stats.status != acacia::mp_nba::extraction_status::cube_cap) {
      std::cerr << "stats: cube cap was not reported precisely\n";
      return false;
    }

    options.cube_cap = 1024;
    options.predicate_cap = 1;
    if (acacia::mp_nba::extract_cubes (*formula, dict, options, &stats)
        or stats.status != acacia::mp_nba::extraction_status::predicate_cap
        or stats.predicates != 2) {
      std::cerr << "stats: predicate cap was not reported precisely\n";
      return false;
    }

    auto unsupported = parse ("a U b");
    if (not unsupported)
      return false;
    options.predicate_cap = 0;
    if (acacia::mp_nba::extract_cubes (*unsupported, dict, options, &stats)
        or stats.status != acacia::mp_nba::extraction_status::unsupported
        or stats.nodes_before == 0 or stats.nodes_after_delta2 == 0) {
      std::cerr << "stats: unsupported shape was not reported precisely\n";
      return false;
    }
    return true;
  }

  bool check_cube_set_lifetimes () {
    auto formula = parse ("GF a | FG b");
    if (not formula)
      return false;

    {
      auto dict = spot::make_bdd_dict ();
      auto cubes = acacia::mp_nba::extract_cubes (*formula, dict, 1024);
      if (not cubes) {
        std::cerr << "cube lifetime: extract_cubes rejected the formula\n";
        return false;
      }
    }

    auto dict = spot::make_bdd_dict ();
    spot::twa_graph_ptr nba;
    {
      auto cubes = acacia::mp_nba::extract_cubes (*formula, dict, 1024);
      if (not cubes) {
        std::cerr << "automaton lifetime: extract_cubes rejected the formula\n";
        return false;
      }
      nba = acacia::mp_nba::build_violation_nba (*cubes, dict);
    }

    try {
      if (spot::are_equivalent (nba, spot::formula::Not (*formula)))
        return true;
    }
    catch (const std::exception& error) {
      std::cerr << "automaton lifetime: equivalence check threw: "
                << error.what () << '\n';
      return false;
    }

    std::cerr << "automaton lifetime: violation NBA became invalid after "
                 "destroying its cube set\n";
    return false;
  }

  bool check_random_formulas () {
    bool ok = true;
    size_t formula_number = 0;
    for (int atomic_propositions : {2, 3}) {
      spot::option_map options;
      options.set ("output", spot::randltlgenerator::LTL);
      options.set ("seed", 0x5eed + atomic_propositions);
      options.set ("tree_size_min", 1);
      options.set ("tree_size_max", 12);
      options.set ("unique", 1);
      spot::randltlgenerator generator (atomic_propositions, options);

      for (size_t i = 0; i < 12; ++i, ++formula_number) {
        const spot::formula formula = generator.next ();
        const std::string label =
            "random formula " + std::to_string (formula_number);
        if (not formula) {
          std::cerr << label << ": generator returned no formula\n";
          ok = false;
          continue;
        }
        ok &= check_equivalence (label, formula, false);
      }
    }
    return ok;
  }

}  // namespace

int main () {
  bool ok = true;
  ok &= check_targeted_formulas ();
  ok &= check_rejected_formula ();
  ok &= check_cube_cap ();
  ok &= check_extraction_stats ();
  ok &= check_cube_set_lifetimes ();
  ok &= check_random_formulas ();
  return ok ? 0 : 1;
}
