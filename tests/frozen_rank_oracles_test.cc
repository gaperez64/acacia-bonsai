#include "solver/spot_lazy_game.hh"
#include "tiny_spot_game.hh"
#include "utils/verbose.hh"

#include <iostream>
#include <random>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}
namespace posets::vectors {
  size_t bool_threshold = 0;
}

namespace {
  using namespace acacia::spot_lazy_game;
  namespace games = acacia::testing::spot_games;
  size_t comparisons = 0, wins = 0, losses = 0, checks = 0;
  void expect (bool condition, const char* message) {
    ++checks;
    if (!condition)
      throw std::runtime_error (message);
  }
  template <typename T>
  T take (letters::Result<T> r) {
    if (r.error)
      std::rethrow_exception (r.error);
    expect (r.value.has_value (), "query must complete");
    return std::move (*r.value);
  }
  SolveResult solve (const games::TinySpotGame& g, const char* mode, acacia::OracleLayout layout) {
    setenv ("ACACIA_FWD_ORACLE", mode, 1);
    RowStore store {rows::FrozenAcacia {g.graph, g.bool_threshold}, {}};
    Search search {store, g.alphabet, g.K, {}, layout};
    auto result = search.solve ();
    expect (result.status == forward_result_status::win_k ||
                result.status == forward_result_status::lose_k,
            "small game must be decided");
    // Search already independently verifies both outcomes. Replay once more
    // with a fresh store and corrupt a certificate obligation to check that
    // oracle-enabled replay still rejects invalid certificates.
    RowStore fresh {rows::FrozenAcacia {g.graph, g.bool_threshold}, {}};
    if (result.status == forward_result_status::win_k) {
      expect (verify_winning_certificate (fresh, g.alphabet, g.K, result, {}, layout)
                  .value.has_value (),
              "fresh winning replay");
      result.nodes[result.initial].covered_inputs = bddfalse;
      expect (!verify_winning_certificate (fresh, g.alphabet, g.K, result, {}, layout).value,
              "corrupt winning coverage rejected");
    }
    else {
      expect (take (verify_losing_proof (fresh, g.alphabet, g.K, result, {}, layout)),
              "fresh losing replay");
      result.proofs[*result.initial_proof].record.id += result.proofs.size ();
      expect (!verify_losing_proof (fresh, g.alphabet, g.K, result, {}, layout).value,
              "corrupt losing chronology rejected");
    }
    return result;
  }
  void compare (const games::TinySpotGame& g) {
    const auto baseline = solve (g, "", acacia::OracleLayout::grouped).status;
    wins += baseline == forward_result_status::win_k;
    losses += baseline == forward_result_status::lose_k;
    // Include every combination, including simulation tie-breaking + symmetry.
    for (const char* mode : {"cap", "sim", "sym", "cap,sim", "cap,sym", "sim,sym", "all-on"})
      for (auto layout : {acacia::OracleLayout::scan, acacia::OracleLayout::grouped}) {
        expect (solve (g, mode, layout).status == baseline, "oracle verdict must match baseline");
        ++comparisons;
      }
  }
  void targeted () {
    auto g = games::graph (3, true);
    auto a = games::alphabet (g, false, false);
    g->new_edge (0, 1, bddtrue);
    g->new_acc_edge (1, 2, bddtrue);
    g->new_edge (2, 2, bddtrue);
    compare ({g, a, 2, 3});
    compare ({g, a, 1, 3});  // finite accepting prefix can still overflow K!
    setenv ("ACACIA_FWD_ORACLE", "cap", 1);
    RowStore store {rows::FrozenAcacia {g, 3}, {}};
    Reader reader {store, false, {}, 2};
    expect (reader.initial_rank ().entries ().empty (), "cap erases universally safe initial run");
    RowStore tight_store {rows::FrozenAcacia {g, 3}, {}};
    Reader tight {tight_store, false, {}, 1};
    expect (!tight.initial_rank ().entries ().empty (), "cap retains finite-prefix overflow");

    // Equal acceptance at p/q does not suffice: ranks increment at destinations.
    // Also protect the stricter Boolean envelope, even without accepting edges.
    auto sim = games::graph (3, true);
    auto sa = games::alphabet (sim, false, false);
    sim->new_edge (0, 1, bddtrue);
    sim->new_edge (0, 2, bddtrue);
    sim->new_edge (1, 1, bddtrue);
    sim->new_edge (2, 2, bddtrue);
    compare ({sim, sa, 2, 3});
    compare ({sim, sa, 2, 2});
    setenv ("ACACIA_FWD_ORACLE", "sim", 1);
    RowStore ss {rows::FrozenAcacia {sim, 3}, {}};
    Reader sr {ss, false, {}, 2};
    Oracle so {sr, ss, sa, 2};
    const auto next = take (so.evaluate (sr.initial_rank (), bddtrue));
    expect (next.entries ().size () == 1, "simulation actually removes a redundant coordinate");

    // A safe loop must never cover the branch leading to an accepting trap.
    // Transposing the relation only in the forward consumer erases that branch.
    auto fork = games::graph (4, true);
    auto fa = games::alphabet (fork, false, false);
    fork->new_edge (0, 1, bddtrue);
    fork->new_edge (0, 2, bddtrue);
    fork->new_edge (1, 3, bddtrue);
    fork->new_edge (2, 2, bddtrue);
    fork->new_acc_edge (3, 3, bddtrue);
    RowStore fs {rows::FrozenAcacia {fork, 4}, {}};
    Reader fr {fs, false, {}, 1};
    Oracle fo {fr, fs, fa, 1};
    expect (take (fo.evaluate (fr.initial_rank (), bddtrue)).at (1) == 0,
            "simulation retains the dangerous branch in the forward consumer");
    compare ({fork, fa, 1, 4});

    // Review reproducer: the shared name prefix does not authorize swapping an
    // input with an output. The oracle-off game supplies the expected verdict.
    auto mixed = games::graph (4, true);
    const int r0 = mixed->register_ap ("r_0"), r1 = mixed->register_ap ("r_1");
    letters::WorkerAlphabet ma {mixed->ap_vars (), bdd_ithvar (r0), bdd_ithvar (r1), {r0, r1}};
    mixed->set_init_state (3);
    mixed->new_edge (3, 1, bdd_ithvar (r1));
    mixed->new_edge (3, 2, bdd_ithvar (r0));
    mixed->new_edge (1, 0, bdd_ithvar (r1));
    mixed->new_edge (2, 0, bdd_ithvar (r0));
    mixed->new_acc_edge (0, 0, bddtrue);
    compare ({mixed, ma, 1, 1});
    setenv ("ACACIA_FWD_ORACLE", "sym", 1);
    RowStore ms {rows::FrozenAcacia {mixed, 1}, {}};
    Reader mr {ms, false, {}, 1, ma.inputs, ma.outputs};
    expect (mr.oracles ()->symmetry_generators () == 0, "cross-I/O symmetry rejected");
    // The old quotient's certificate chooses r_1=false and treats state 2 as
    // state 1. Replaying that claimed strategy must reject the false win.
    SolveResult forged;
    for (const Rank& rank : {Rank {{{3, 0}}, 1}, Rank {{{1, 0}}, 1}, Rank {{}, 1}}) {
      forged.nodes.push_back ({rank});
      forged.nodes.back ().active_rows_complete = true;
      forged.nodes.back ().covered_inputs = bddtrue;
    }
    forged.nodes[0].choices.push_back (
        SparseChoice::constant (bdd_ithvar (r0), bdd_nithvar (r1), 1));
    forged.nodes[0].choices.push_back (
        SparseChoice::constant (bdd_nithvar (r0), bdd_nithvar (r1), 2));
    for (unsigned id : {1, 2})
      forged.nodes[id].choices.push_back (SparseChoice::constant (bddtrue, bdd_nithvar (r1), 2));
    expect (!verify_winning_certificate (ms, ma, 1, forged).value,
            "false quotient certificate rejected");

    // Verified client swap with an accepting trap. Both input and output AP
    // permutations are exercised; the game has asymmetric reachable ranks.
    auto sym = games::graph (4, true);
    int u0 = sym->register_ap ("u_0"), u1 = sym->register_ap ("u_1");
    int c0 = sym->register_ap ("c_0"), c1 = sym->register_ap ("c_1");
    letters::WorkerAlphabet syma {sym->ap_vars (),
                                  bdd_ithvar (u0) & bdd_ithvar (u1),
                                  bdd_ithvar (c0) & bdd_ithvar (c1),
                                  {u0, u1, c0, c1}};
    sym->new_edge (0, 1, bdd_ithvar (u0));
    sym->new_edge (0, 2, bdd_ithvar (u1));
    sym->new_edge (1, 3, !bdd_ithvar (c0));
    sym->new_edge (2, 3, !bdd_ithvar (c1));
    sym->new_acc_edge (3, 3, bddtrue);
    compare ({sym, syma, 2, 4});
    setenv ("ACACIA_FWD_ORACLE", "sym", 1);
    RowStore ys {rows::FrozenAcacia {sym, 4}, {}};
    Reader yr {ys, false, {}, 2, syma.inputs, syma.outputs};
    expect (yr.oracles ()->symmetry_generators () > 0, "test must discover a symmetry");
    Oracle yo {yr, ys, syma, 2};
    const Rank one {{{1, 0}}, 2}, two {{{2, 0}}, 2};
    expect (
        take (yo.query<Rank> ([&] (auto& b) { return yr.oracles ()->canonical (one, 2, b); })) ==
            take (yo.query<Rank> ([&] (auto& b) { return yr.oracles ()->canonical (two, 2, b); })),
        "symmetry actually merges distinct rank keys");
    setenv ("ACACIA_FWD_ORACLE", "all-on", 1);
    RowStore limited_store {rows::FrozenAcacia {sym, 4}, {}};
    Limits limited;
    limited.queries.max_steps = 0;
    Search limited_search {limited_store, syma, 2, limited};
    const auto stopped = limited_search.solve ();
    expect (stopped.status == forward_result_status::resource_limit,
            "oracle work exceeding the query budget cannot produce a verdict");

    // No whole-graph fact may leak into a demand provider, even a graph-backed
    // one. Selection is ignored there and initial/transition ranks stay exact.
    setenv ("ACACIA_FWD_ORACLE", "all-on", 1);
    RowStore demand {FixedBuchi {{sim}, {}, {}}, {}};
    Reader dr {demand, false, {}, 2, sa.inputs, sa.outputs};
    expect (!dr.oracles () && dr.active_oracles () == "none", "demand provider is unchanged");
  }
}
int main () {
  try {
    targeted ();
    std::mt19937 rng {0x50304657};
    for (unsigned i = 0; i < 100; ++i)
      compare (games::random_game (rng, i, true));
    unsetenv ("ACACIA_FWD_ORACLE");
    std::cout << comparisons << " verdict comparisons, " << wins << " winning / " << losses
              << " losing games, " << checks << " checks; 0 failures\n";
  } catch (const std::exception& e) {
    std::cerr << e.what () << '\n';
    return 1;
  }
}
