#include "solver/spot_nba_fastpath.hh"
#include "utils/verbose.hh"

#include <bddx.h>
#include <spot/twa/twagraph.hh>

#include <iostream>
#include <string>
#include <vector>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}

namespace {

  using acacia::spot_fastpath::nba_fast_class;

  bool expect_class (std::string name, nba_fast_class got, nba_fast_class expected) {
    if (got == expected)
      return true;

    std::cerr << name << ": expected "
              << acacia::spot_fastpath::detail::class_name (expected)
              << ", got "
              << acacia::spot_fastpath::detail::class_name (got)
              << '\n';
    return false;
  }

  spot::twa_graph_ptr make_deterministic_buchi () {
    auto dict = spot::make_bdd_dict ();
    auto aut = spot::make_twa_graph (dict);
    aut->set_acceptance (1, spot::acc_cond::acc_code::buchi ());
    aut->new_state ();
    aut->set_init_state (0);
    aut->new_edge (0, 0, bddtrue, spot::acc_cond::mark_t {0});
    return aut;
  }

  spot::twa_graph_ptr make_nondet_gfg_buchi () {
    auto dict = spot::make_bdd_dict ();
    auto aut = spot::make_twa_graph (dict);
    aut->set_acceptance (1, spot::acc_cond::acc_code::buchi ());
    aut->new_states (3);
    aut->set_init_state (0);

    aut->new_edge (0, 1, bddtrue, spot::acc_cond::mark_t {0});
    aut->new_edge (0, 2, bddtrue, spot::acc_cond::mark_t {0});
    aut->new_edge (1, 1, bddtrue, spot::acc_cond::mark_t {0});
    aut->new_edge (2, 2, bddtrue, spot::acc_cond::mark_t {0});
    return aut;
  }

  spot::twa_graph_ptr make_nondet_non_gfg_buchi () {
    auto dict = spot::make_bdd_dict ();
    int owner;
    const unsigned a_var = dict->register_proposition (spot::formula::ap ("a"), &owner);
    bdd a = bdd_ithvar (a_var);

    auto aut = spot::make_twa_graph (dict);
    aut->set_acceptance (1, spot::acc_cond::acc_code::buchi ());
    aut->new_states (2);
    aut->set_init_state (0);

    aut->new_edge (0, 0, bddtrue);
    aut->new_edge (0, 1, a);
    aut->new_edge (1, 1, a, spot::acc_cond::mark_t {0});
    dict->unregister_all_my_variables (&owner);
    return aut;
  }

  bool deterministic_winning_region_covers_all_original_states () {
    auto dict = spot::make_bdd_dict ();
    auto aut = spot::make_twa_graph (dict);
    aut->set_acceptance (1, spot::acc_cond::acc_code::buchi ());
    aut->new_states (2);
    aut->set_init_state (0);
    aut->new_acc_edge (0, 0, bddtrue, false);
    aut->new_acc_edge (1, 1, bddtrue);

    auto res = acacia::spot_fastpath::deterministic_forbidden_fast_path (
        aut, bddtrue, false, true);
    if (not res.conclusive or not res.current_output_player_winning_region.has_value ()) {
      std::cerr << "det-region: no winning region returned\n";
      return false;
    }

    const auto& region = *res.current_output_player_winning_region;
    if (region.size () != 2 or not region[0] or region[1]) {
      std::cerr << "det-region: expected only state 0 to be controller-winning\n";
      return false;
    }

    return true;
  }

  bool spot_fast_modes_require_det_bit () {
    auto aut = make_deterministic_buchi ();

    auto gfg_only = acacia::spot_fastpath::try_spot_nba_fast_path (
        aut, bddtrue, bddtrue, false, true, SPOT_FAST_GFG_DECISION);
    if (gfg_only.conclusive) {
      std::cerr << "mode-gate: GFG-only bit unexpectedly ran a fast path\n";
      return false;
    }

    auto det_and_gfg = acacia::spot_fastpath::try_spot_nba_fast_path (
        aut, bddtrue, bddtrue, false, true, SPOT_FAST_DET_AND_GFG);
    if (not det_and_gfg.conclusive) {
      std::cerr << "mode-gate: DET+GFG did not run deterministic fast path\n";
      return false;
    }

    return true;
  }

  bool fast_path_records (bool gfg) {
    namespace records = acacia::spot_records;
    char directory[] = "/tmp/acacia-fast-record-XXXXXX";
    if (not mkdtemp (directory)) return false;
    setenv ("ACACIA_SPOT_CAPTURE_DIR", directory, 1);
    setenv ("ACACIA_SPOT_CAPTURE_HISTORY", "1", 1);
    bool ok = true;
    {
      records::Record capture;
      records::segment ("frozen-graph", "spot-guarded-sparse");
      records::phase ("preprocessing");
      // The elevator's auxiliary SCC query must not relabel worker progress.
      ok &= deterministic_winning_region_covers_all_original_states ();
      for (const auto& entry : std::filesystem::directory_iterator (directory)) {
        std::ifstream input (entry.path ());
        std::string line;
        while (std::getline (input, line))
          ok &= line.find ("\"search_started\":\"true\"") == std::string::npos &&
                line.find ("\"stage\":\"search\"") == std::string::npos &&
                line.find ("\"stage\":\"action-construction\"") == std::string::npos;
      }
      auto aut = gfg ? make_nondet_gfg_buchi () : make_deterministic_buchi ();
      const auto result = acacia::spot_fastpath::try_spot_nba_fast_path (
          aut, bddtrue, bddtrue, false, true, SPOT_FAST_DET_AND_GFG);
      ok &= result.conclusive && !result.current_output_player_wins;
    }
    const auto has = [] (const std::string& line, const char* key, const char* value) {
      return line.find ('"' + std::string (key) + "\":\"" + value + '"') != std::string::npos;
    };
    bool actions = false, search = false, verification = false, ended = false;
    for (const auto& entry : std::filesystem::directory_iterator (directory)) {
      if (entry.path ().extension () != ".jsonl") continue;
      std::ifstream input (entry.path ());
      std::string line;
      while (std::getline (input, line)) {
        if (has (line, "stage", "verification")) {
          verification = true;
          ok &= has (line, "verification_kind", "gfg-precondition");
        }
        if (has (line, "stage", "action-construction")) actions = true;
        if (has (line, "stage", "search")) {
          search = true;
          ok &= actions && (verification == gfg);
        }
        if (has (line, "stage", "attempt-end")) {
          ended = true;
          ok &= search && has (line, "search_started", "true") &&
                has (line, "attempt_end", "true") && has (line, "status", "LOSE") &&
                has (line, "evidence", "spot-game") &&
                has (line, "backend", gfg ? "spot-fast-gfg" : "spot-fast-det") &&
                line.find ("\"k\"") == std::string::npos;
          ok &= (line.find ("\"verification_ms\"") != std::string::npos) == gfg;
        }
      }
    }
    ok &= ended;
    unsetenv ("ACACIA_SPOT_CAPTURE_DIR");
    unsetenv ("ACACIA_SPOT_CAPTURE_HISTORY");
    std::filesystem::remove_all (directory);
    std::cout << (ok ? "PASS " : "FAIL ") << "spot-fast-" << (gfg ? "gfg" : "det")
              << "-loss lifecycle (GFG precondition verification only)\n";
    return ok;
  }

}  // namespace

int main () {
  bool ok = true;

  ok &= expect_class ("deterministic",
                      acacia::spot_fastpath::classify_nba_for_fast_path (
                          make_deterministic_buchi ()),
                      nba_fast_class::deterministic_buchi);

  ok &= expect_class ("nondet-gfg",
                      acacia::spot_fastpath::classify_nba_for_fast_path (
                          make_nondet_gfg_buchi ()),
                      nba_fast_class::gfg_buchi);

  ok &= expect_class ("eventually-stable-non-gfg",
                      acacia::spot_fastpath::classify_nba_for_fast_path (
                          make_nondet_non_gfg_buchi ()),
                      nba_fast_class::non_gfg_buchi);
  ok &= deterministic_winning_region_covers_all_original_states ();
  ok &= spot_fast_modes_require_det_bit ();
  ok &= fast_path_records (false);
  ok &= fast_path_records (true);

  return ok ? 0 : 1;
}
