#include "research/cpre_downset_replay.hh"
#include "utils/verbose.hh"

#include <iostream>
#include <random>
#include <stdexcept>

#include <posets/downsets/bboxtree_backed.hh>
#include <posets/downsets/kdtree_backed.hh>
#include <posets/downsets/rank_bucketed_vector_backed.hh>
#include <posets/downsets/sharingtrie_backed.hh>
#include <posets/vectors.hh>
#include <posets/vectors/X_and_bitset.hh>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}
namespace posets::vectors {
  size_t bool_threshold = 0;
  size_t bitset_threshold = 0;
}

#define ACACIA_STRINGIFY_IMPL(x) #x
#define ACACIA_STRINGIFY(x) ACACIA_STRINGIFY_IMPL (x)

namespace {
  using namespace acacia::research;
  using namespace acacia::research::downset_replay;
  constexpr std::string_view backend = ACACIA_STRINGIFY (ACACIA_REPLAY_TEST_DOWNSET);

  void require (bool condition, const char* message) {
    if (not condition)
      throw std::runtime_error (message);
  }

  std::vector<rank_vector> domain (size_t dimension, int k, size_t numeric,
                                   bool ceilings = false) {
    std::vector<rank_vector> values {rank_vector (dimension, -1)};
    for (size_t i = 0; i < dimension; ++i) {
      std::vector<rank_vector> next;
      const int cap = (i < numeric ? k - 1 : 0) + (ceilings ? 1 : 0);
      for (const auto& v : values)
        for (int value = -1; value <= cap; ++value) {
          auto copy = v;
          copy[i] = value;
          next.push_back (std::move (copy));
        }
      values = std::move (next);
    }
    return values;
  }

  bool represented (const exact_rows& rows, const std::vector<int>& value) {
    return std::ranges::any_of (rows, [&] (const auto& m) { return below (value, m); });
  }

  // Independent pointwise successor relation. This never calls the production
  // actioner, replay beta, candidate contains(), or a Posets reduction routine.
  std::vector<int> successor (const rank_vector& source, const action_vec& action, int k) {
    std::vector<int> result (source.size (), -1);
    for (size_t destination = 0; destination < action.size (); ++destination)
      for (const auto& [origin, accepting] : action[destination])
        if (source[origin] >= 0) {
          const int next = std::min (k, int (source[origin]) + int (accepting));
          result[destination] = std::max (result[destination], next);
        }
    return result;
  }

  exact_rows oracle_update (const exact_rows& before, const event& ev, size_t numeric) {
    exact_rows retained;
    for (const auto& source : domain (ev.states, ev.k, numeric)) {
      const auto point = rows_of (std::vector<rank_vector> {source}).front ();
      if (not represented (before, point))
        continue;
      for (const auto& action : ev.actions)
        if (represented (before, successor (source, action, ev.k))) {
          retained.push_back (point);
          break;
        }
    }
    return maxima (std::move (retained));
  }

  template <class Downset>
  void check_queries (const Downset& region, const exact_rows& expected, const event& ev,
                      size_t numeric) {
    using state = typename Downset::value_type;
    require (rows_of (region) == expected, "exact maximal generators differ");
    for (const auto& query : domain (ev.states, ev.k, numeric, true)) {
      if constexpr (not requires { state::blocks_for (ev.states); }) {
        bool representable = true;
        for (size_t i = numeric; i < ev.states; ++i)
          representable &= query[i] <= 0;
        if (not representable)
          continue;
      }
      const auto point = rows_of (std::vector<rank_vector> {query}).front ();
      require (region.contains (state (rank_vector (query))) == represented (expected, point),
               "membership differs from enumerated domain");
    }
  }

  template <class Vector>
  void generated () {
    using Downset = posets::downsets::ACACIA_REPLAY_TEST_DOWNSET<Vector>;
    std::mt19937 random {20261014};
    for (unsigned trial = 0; trial < 96; ++trial) {
      event ev;
      ev.states = 1 + trial % 4;
      ev.k = 1 + trial % 3;
      const size_t numeric = trial % (ev.states + 1);
      posets::vectors::bool_threshold = numeric;
      posets::vectors::bitset_threshold = numeric;
      const auto safe_domain = domain (ev.states, ev.k, numeric);
      // Includes bottom, duplicates, domination chains, and random incomparables.
      ev.before.push_back (safe_domain.front ());
      for (unsigned i = 0; i < 1 + trial % 9; ++i)
        ev.before.push_back (safe_domain[random () % safe_domain.size ()]);
      ev.before.push_back (ev.before.back ());
      for (unsigned output = 0; output < 1 + trial % 3; ++output) {
        action_vec action (ev.states);
        for (unsigned destination = 0; destination < ev.states; ++destination)
          for (unsigned origin = 0; origin < ev.states; ++origin)
            if (random () % 3 == 0)
              action[destination].emplace_back (origin, random () % 2 != 0);
        ev.actions.push_back (std::move (action));
      }
      validate (ev, numeric);
      const auto initial = maxima (rows_of (ev.before));
      const auto expected = oracle_update (initial, ev, numeric);
      auto unions = construct<Downset> (ev.before, backend);
      auto batched = construct<Downset> (ev.before, backend);
      costs cost;
      update<Downset, 0> (unions, ev, cost, backend);
      update<Downset, 1> (batched, ev, cost, backend);
      check_queries (unions, expected, ev, numeric);
      check_queries (batched, expected, ev, numeric);
      require (cost.kernel_ms == 0, "correctness test unexpectedly measured time");

      auto moved = std::move (unions);
      auto rebuilt = moved.apply ([] (const auto& m) { return m.copy (); });
      moved = std::move (rebuilt);
      check_queries (moved, expected, ev, numeric);
      // Repeat shrinking updates until the exact fixed point, including bottom.
      auto reference = expected;
      for (unsigned step = 0; step < 40; ++step) {
        const auto next = oracle_update (reference, ev, numeric);
        update<Downset, 0> (moved, ev, cost, backend);
        check_queries (moved, next, ev, numeric);
        if (next == reference)
          break;
        reference = next;
        require (step != 39, "tiny descending fixed point did not terminate");
      }
      // Numeric K lift retains the Boolean coordinates and is compared as a set.
      auto lift_expected = rows_of (moved);
      for (auto& row : lift_expected)
        for (size_t i = 0; i < numeric; ++i)
          ++row[i];
      auto lifted = moved.apply ([&] (const auto& m) {
        rank_vector row (ev.states, 0);
        for (size_t i = 0; i < numeric; ++i)
          row[i] = m[i] + 1;
        return Vector (row);
      });
      // Production K lift resets all Boolean entries to 0.
      for (auto& row : lift_expected)
        for (size_t i = numeric; i < ev.states; ++i)
          row[i] = 0;
      require (rows_of (lifted) == maxima (lift_expected), "K lift changed represented set");

      auto united = construct<Downset> (ev.before, backend);
      auto other = construct<Downset> ({safe_domain.back ()}, backend);
      united.union_with (std::move (other));
      check_queries (united, maxima (rows_of (std::vector<rank_vector> {safe_domain.back ()})), ev,
                     numeric);
      auto intersected = construct<Downset> (ev.before, backend);
      intersected.intersect_with (united);
      check_queries (intersected, initial, ev, numeric);
    }
    // Explicit incomparables at the numeric/Boolean seam and signed K ceilings.
    for (int k : {3, 99, 127}) {
      event ev;
      ev.states = 2;
      ev.k = k;
      posets::vectors::bool_threshold = 1;
      posets::vectors::bitset_threshold = 1;
      rank_vector first (2, -1), second (2, 0);
      first[0] = k - 1;
      second[0] = k - 2;
      ev.before = {first, second, first};
      action_vec action (2);
      action[0].emplace_back (0, true);
      action[1].emplace_back (1, false);
      ev.actions = {action};
      const auto expected = oracle_update (maxima (rows_of (ev.before)), ev, 1);
      auto region = construct<Downset> (ev.before, backend);
      costs cost;
      update<Downset, 0> (region, ev, cost, backend);
      check_queries (region, expected, ev, 1);
      auto batch = construct<Downset> (ev.before, backend);
      update<Downset, 1> (batch, ev, cost, backend);
      check_queries (batch, expected, ev, 1);
    }
    // Empty is distinct from bottom. Reject unsupported constructors explicitly.
    if (backend == "kdtree_backed" or backend == "bboxtree_backed") {
      bool rejected = false;
      try {
        auto empty = construct<Downset> ({}, backend);
        (void) empty;
      } catch (const std::invalid_argument&) {
        rejected = true;
      }
      require (rejected, "unsupported empty constructor was not rejected");
    }
    else {
      auto empty = construct<Downset> ({}, backend);
      auto image = empty.apply ([] (const auto& m) { return m.copy (); });
      require (rows_of (image).empty (), "empty apply gained a generator");
      empty.union_with (std::move (image));
      require (empty.size () == 0, "empty union gained a generator");
      auto bottom = construct<Downset> ({rank_vector (2, -1)}, backend);
      bottom.intersect_with (empty);
      require (bottom.size () == 0, "empty intersection is not empty");
    }
    event singleton;
    singleton.states = 2;
    singleton.k = 2;
    singleton.before = {rank_vector (2, -1)};
    posets::vectors::bool_threshold = 1;
    posets::vectors::bitset_threshold = 1;
    costs cost;
    auto bottom = construct<Downset> (singleton.before, backend);
    update<Downset, 0> (bottom, singleton, cost, backend);
    require (rows_of (bottom) == rows_of (singleton.before), "action-free union mode bottom");
  }
}

int main () {
  try {
    generated<posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>> ();
    generated<posets::vectors::vector_backed<VECTOR_ELT_T>> ();
    generated<posets::vectors::x_and_bitset<posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>, 1>> ();
    std::cout << backend << ": generated algebra, oracle, fixed point, K lift, lifetime passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << backend << ": " << error.what () << '\n';
    return 1;
  }
}
