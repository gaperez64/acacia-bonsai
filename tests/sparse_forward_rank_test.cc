#include "solver/sparse_forward_rank.hh"
#include "utils/verbose.hh"

#include <iostream>
#include <unordered_set>

namespace utils { unsigned verbose = 0; voutstream vout; }
namespace posets::vectors { size_t bool_threshold = 0; }

namespace {
  using namespace acacia::spot_rows;
  using Sparse = SparseForwardRank<>;
  int failures = 0;
  bool expect (const std::string& name, bool condition) {
    if (condition) return true;
    std::cerr << "FAIL: " << name << '\n';
    ++failures;
    return false;
  }
  template <typename F> void rejects (const std::string& name, F f) {
    bool rejected = false;
    try { f (); } catch (const std::invalid_argument&) { rejected = true; }
    expect (name, rejected);
  }

  void values () {
    const Sparse rank {{{8, -1}, {4, 0}, {1, 1}, {1, 3}, {1, 2}, {4, -1}}, 3};
    expect ("sorted, duplicate MAX, omit -1", rank.entries () == std::vector<Sparse::Entry> {{1, 3}, {4, 0}});
    expect ("missing is -1", rank.at (0) == -1 && rank.at (1) == 3 && rank.at (9) == -1);
    const Sparse same {{{4, 0}, {1, 3}}, 4};
    expect ("hash and equality ignore construction order and K", rank == same && rank.hash () == same.hash ());

    // The hash is derived once by the constructor, so every route to the same
    // normalized entries must carry the same value: a copy, a move target, and
    // a rank whose -1 entries were dropped rather than never supplied.
    const Sparse copied {rank};
    Sparse moved_from {{{4, 0}, {1, 3}}, 3};
    const Sparse moved {std::move (moved_from)};
    const Sparse dropped {{{1, 3}, {4, 0}, {2, -1}, {7, -1}}, 3};
    expect ("cached hash survives copy, move and dropped -1 entries",
            copied.hash () == rank.hash () && moved.hash () == rank.hash ()
            && dropped.hash () == rank.hash () && dropped == rank);
    expect ("bound is numeric safe envelope", not rank.is_safe (3) && rank.is_safe (4));
    const Sparse empty {{}, 3}, zeros {{{0, 0}}, 3}, other {{{5, 0}}, 3};
    expect ("empty bottom, zero is active", empty.leq (zeros) && not zeros.leq (empty) && empty != zeros);
    expect ("disjoint supports incomparable", not zeros.leq (other) && not other.leq (zeros));
    expect ("order with extra rhs support", zeros.leq (Sparse {{{0, 1}, {8, 0}}, 3}));
    const Sparse far {{{std::numeric_limits<StateId>::max (), 1}}, 3};
    expect ("stable wide state ID without arena allocation", far.entries ().size () == 1 && empty.leq (far));
    rejects ("invalid negative rank", [] { Sparse r {{{0, -2}}, 3}; });
    rejects ("above K", [] { Sparse r {{{0, 4}}, 3}; });
    rejects ("nonpositive K", [] { Sparse r {{}, 0}; });
    rejects ("changed bound validates values", [&] { (void) rank.is_safe (2); });

    using Byte = SparseForwardRank<std::int8_t>;
    expect ("signed-byte saturation before narrowing", Byte::increment (127, true, 127) == 127
            && Byte::increment (126, true, 127) == 127 && Byte::increment (-1, true, 127) == -1);
    const auto max = std::numeric_limits<std::int32_t>::max ();
    expect ("int32 addition also widens", Sparse::increment (max, true, max) == max);

    const Byte byte_rank {{{1, 3}, {4, 0}}, 127}, byte_same {{{4, 0}, {1, 3}}, 100};
    expect ("narrow value type hashes like the wide one",
            byte_rank.hash () == byte_same.hash () && byte_rank == byte_same);

    // Exhaustive componentwise order against independent dense coordinates.
    for (int a = 0; a < 64; ++a)
      for (int b = 0; b < 64; ++b) {
        std::vector<Sparse::Entry> left, right;
        bool leq = true;
        for (StateId q = 0; q < 3; ++q) {
          const int av = ((a >> (2 * q)) & 3) - 1, bv = ((b >> (2 * q)) & 3) - 1;
          left.emplace_back (q * 3, av);
          right.emplace_back (q * 3, bv);
          leq &= av <= bv;
        }
        const Sparse l {left, 2}, r {right, 2};
        expect ("exhaustive sparse order", l.leq (r) == leq);
        // The lemma the prefilter rests on, checked on every one of the 4096
        // pairs: leq is a refinement of the O(1) necessary condition, and an
        // equal mass under leq leaves only equality.
        if (leq) {
          expect ("leq implies the prefilter admits", l.prefilter_leq (r));
          expect ("leq implies smaller support", l.entries ().size () <= r.entries ().size ());
          expect ("leq implies no greater mass", l.mass () <= r.mass ());
          if (l.mass () == r.mass ())
            expect ("leq with equal mass is equality", l == r);
        }
      }

    // Support size alone does not decide the order, and neither does mass:
    // two coordinates at 0 weigh the same as one coordinate at 1.
    const Sparse wide {{{0, 0}, {1, 0}}, 3}, tall {{{0, 1}}, 3};
    expect ("equal mass, different support, incomparable both ways",
            wide.mass () == tall.mass () && wide.mass () == 2
            && not wide.leq (tall) && not tall.leq (wide));
    const Sparse light {{{0, 0}, {1, 0}}, 3}, heavy {{{0, 3}, {1, 3}}, 3};
    expect ("equal support size, different mass, ordered one way",
            light.entries ().size () == heavy.entries ().size ()
            && light.mass () < heavy.mass () && light.leq (heavy) && not heavy.leq (light));

    // Arena independence: mass is a sum over the support, so a coordinate far
    // out in the state space weighs exactly what its value says, and a dense
    // coordinate sum's implicit -1 tail never enters.
    const Sparse near {{{0, 1}}, 3};
    const Sparse remote {{{std::numeric_limits<StateId>::max (), 1}}, 3};
    expect ("mass does not depend on where the coordinate sits",
            near.mass () == remote.mass () && Sparse ({}, 3).mass () == 0);

    // A term is at most K+1 <= 2^31 and the guard trips at int64max/2, so a
    // rank that abandons its mass would need about 2^33 entries -- tens of
    // gigabytes of pairs. The branch is therefore unreachable by construction,
    // and exists so that correctness does not rest on that being true. What is
    // testable is that a large but valid mass stays usable and exact.
    using Byte8 = SparseForwardRank<std::int8_t>;
    std::vector<Byte8::Entry> heavy_entries;
    for (StateId q = 0; q < 8; ++q) heavy_entries.emplace_back (q, 127);
    const Byte8 saturated {heavy_entries, 127}, one {{{0, 127}}, 127};
    expect ("large valid mass stays usable", saturated.mass () == 8 * 128);
    expect ("large-mass rank still orders exactly",
            saturated.leq (saturated) && not saturated.leq (one) && one.leq (saturated));
  }

  void rows_and_discovery () {
    auto graph = spot::make_twa_graph (spot::make_bdd_dict ());
    graph->set_buchi ();
    graph->new_states (4);
    graph->set_init_state (0);
    const auto v = graph->register_ap ("a");
    const bdd a = bdd_ithvar (v);
    graph->new_edge (0, 1, a);
    graph->new_edge (0, 2, !a);
    graph->new_edge (1, 3, bddtrue);
    graph->new_edge (2, 3, bddtrue, {0});
    graph->new_edge (3, 3, bddtrue, {0});
    const acacia::spot_letters::WorkerAlphabet alphabet {a, a, bddtrue, {v}};
    auto rows = std::make_shared<SpotRows> (GenericTransitionBuchi {graph});
    const Sparse original {{{rows->initial_id (), 0}}, 3}, saved = original;
    const auto hash = original.hash ();
    const std::unordered_set<Sparse> keys {original};
    expect ("initial discovery requests no row", rows->state_count () == 1 && rows->complete_rows () == 0);
    auto first = evaluate_sparse (rows, alphabet, original, a, 3);
    expect ("complete source row discovers inactive branch too", first.value && rows->state_count () == 3
            && rows->complete_rows () == 1 && first.value->entries () == std::vector<Sparse::Entry> {{1, 0}});
    if (not first.value) return;
    auto second = evaluate_sparse (rows, alphabet, *first.value, a, 3);
    expect ("late discovery only completes active rows", second.value && rows->state_count () == 4
            && rows->complete_rows () == 2 && rows->state (2) == RowState::not_requested
            && rows->state (3) == RowState::not_requested);
    expect ("old key and hash survive late discovery", original == saved && original.hash () == hash
            && keys.contains (original) && keys.contains (Sparse {{{0, 0}, {3, -1}}, 3}));

    const Sparse merged {{{1, 0}, {2, 1}}, 3};
    const auto successor = evaluate_sparse (rows, alphabet, merged, a, 3);
    expect ("duplicate destination MAX includes acceptance", successor.value
            && successor.value->entries () == std::vector<Sparse::Entry> {{3, 2}});
    const auto dense = rows->evaluate (Rank {-1, 0, 1, -1}, a, 3);
    expect ("P2 dense and sparse tau agree", dense.rank && *dense.rank == Rank ({-1, -1, -1, 2}));
    // Arbitrary global Boolean metadata must have no effect in this domain.
    posets::vectors::bool_threshold = 0;
    expect ("all numeric even beyond Boolean threshold", successor.value && successor.value->is_safe (3));
    using Byte = SparseForwardRank<std::int8_t>;
    const Byte byte {{{3, 127}}, 127};
    const auto saturated = evaluate_sparse (rows, alphabet, byte, a, std::int8_t (127));
    expect ("byte rank through real accepting row", saturated.value && saturated.value->at (3) == 127);
    auto invalid = evaluate_sparse (rows, alphabet, original, bddtrue, 3);
    expect ("partial valuation is inconclusive", not invalid.value
            && invalid.unknown == acacia::spot_letters::Unknown::invalid_query);
    auto budget = evaluate_sparse (rows, alphabet, original, a, 3, {.max_live_nodes = 0});
    expect ("BDD budget is UNKNOWN", not budget.value && budget.unknown == acacia::spot_letters::Unknown::resource_limit);
    auto limited = std::make_shared<SpotRows> (GenericTransitionBuchi {graph}, RowLimits {.max_rows = 0});
    auto unknown = evaluate_sparse (limited, alphabet, original, a, 3);
    expect ("incomplete active row never publishes a rank", not unknown.value
            && unknown.unknown == acacia::spot_letters::Unknown::resource_limit);
    const auto bottom = evaluate_sparse (limited, alphabet, Sparse {{}, 3}, a, 3);
    expect ("empty support requests no row", bottom.value && bottom.value->entries ().empty () && limited->complete_rows () == 0);
  }
}

int main () {
  try { values (); rows_and_discovery (); }
  catch (const std::exception& e) { expect (std::string ("unexpected exception: ") + e.what (), false); }
  if (not failures) std::cerr << "sparse-forward-rank: normalization, order, late discovery, arithmetic and budgets pass\n";
  return failures ? 1 : 0;
}
