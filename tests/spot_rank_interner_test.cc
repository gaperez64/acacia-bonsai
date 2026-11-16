#define ACACIA_PROVIDER_REPLAY_TESTING
#include "solver/spot_lazy_game.hh"
#include "tiny_spot_game.hh"
#include "utils/verbose.hh"

#include <cstdlib>
#include <iostream>
#include <new>
#include <type_traits>

namespace utils { unsigned verbose = 0; voutstream vout; }
namespace posets::vectors { size_t bool_threshold = 0; }

// Refuse each real allocation, including index rehash, vector and queue growth.
static bool track = false;
static size_t allocations = 0, refuse = SIZE_MAX;
void* operator new (std::size_t n) {
  if (track && allocations++ == refuse) throw std::bad_alloc {};
  if (auto p = std::malloc (n ? n : 1)) return p;
  throw std::bad_alloc {};
}
[[gnu::noinline]] void operator delete (void* p) noexcept { std::free (p); }
[[gnu::noinline]] void operator delete (void* p, std::size_t) noexcept { std::free (p); }

static void check (bool condition, const char* message) {
  if (not condition) throw std::runtime_error (message);
}

namespace acacia::spot_lazy_game {
  struct SearchTestAccess {
      static RankNodeId intern (Search& s, Rank r) { return s.intern (std::move (r)); }
      static void collide (Search& s) { s.interner_hash_mask_ = 0; }
      static auto& result (Search& s) { return s.result_; }
      static auto buckets (Search& s) { return s.interned_.bucket_count (); }
      static void consistent (Search& s) {
        check (s.interned_.size () == s.result_.nodes.size (), "index/node publication is atomic");
        std::vector<bool> seen (s.result_.nodes.size ());
        for (const auto& [hash, id] : s.interned_) {
          check (id < seen.size () && not seen[id], "every ID has exactly one index entry");
          seen[id] = true;
          check (hash == (s.result_.nodes[id].rank.hash () & s.interner_hash_mask_),
                 "hash cached before moving entries");
        }
        for (size_t id = 0; id < s.open_.size (); ++id)
          check (s.open_[id] == id && s.result_.nodes[id].queued,
                 "first-seen numbering and enqueue order are unchanged");
      }
  };
}

using namespace acacia::spot_lazy_game;
namespace games = acacia::testing::spot_games;
static_assert (std::is_nothrow_move_constructible_v<GuardedRankNode>);

static Rank rank (size_t n) { return Rank {{{StateId (n), 0}}, 3}; }

static void collisions_and_growth (RowStore& store, const letters::WorkerAlphabet& alphabet) {
  Search search {store, alphabet, 3};
  const Rank a {{{1591, 3}}, 3}, b {{{1931, 2}}, 3};
  check (a.hash () == b.hash () && not (a == b), "fixture is an actual cached-hash collision");
  check (SearchTestAccess::intern (search, a) == 0 &&
         SearchTestAccess::intern (search, b) == 1 &&
         SearchTestAccess::intern (search, Rank (a)) == 0 &&
         SearchTestAccess::intern (search, Rank (b)) == 1,
         "actual collisions resolve by full entries, copied equals reuse IDs");

  Search forced {store, alphabet, 3};
  SearchTestAccess::collide (forced);
  size_t grows = 0, rehashes = 0;
  for (size_t id = 0; id < 1024; ++id) {
    auto caller = rank (id);
    const auto saved = caller;
    const auto capacity = SearchTestAccess::result (forced).nodes.capacity ();
    const auto buckets = SearchTestAccess::buckets (forced);
    check (SearchTestAccess::intern (forced, std::move (caller)) == id,
           "moved caller retains first-seen ID under forced collisions");
    grows += capacity != SearchTestAccess::result (forced).nodes.capacity ();
    rehashes += buckets != SearchTestAccess::buckets (forced);
    check (SearchTestAccess::intern (forced, saved) == id,
           "equal copied ranks still resolve after vector growth and rehash");
  }
  check (grows > 5 && rehashes > 5, "exercise repeated vector growth and rehash");
  SearchTestAccess::consistent (search);
  SearchTestAccess::consistent (forced);
}

static void allocation_refusal (RowStore& store, const letters::WorkerAlphabet& alphabet) {
  size_t stops = 0;
  for (const size_t warm : {0, 1, 13, 29, 63, 127, 257}) {
    size_t count = 0;
    for (size_t fail = 0;; ++fail) {
      Search search {store, alphabet, 3};
      SearchTestAccess::collide (search);
      for (size_t id = 0; id < warm; ++id) SearchTestAccess::intern (search, rank (id));
      auto caller = rank (warm);
      allocations = 0;
      refuse = fail == 0 ? SIZE_MAX : fail - 1;
      track = true;
      const auto result = detail::checked<RankNodeId> ([&] {
        return SearchTestAccess::intern (search, std::move (caller));
      });
      track = false;
      if (fail == 0) {
        check (result.value && *result.value == warm, "unrefused intern succeeds");
        count = allocations;
      }
      else {
        check (not result.value && result.unknown == Unknown::resource_limit,
               "every refused allocation remains inconclusive/resource-limited");
        ++stops;
      }
      SearchTestAccess::consistent (search);
      // A failed enqueue can leave its coherent pair published, as before.
      check (SearchTestAccess::result (search).nodes.size () <= warm + 1,
             "failure publishes at most the requested node");
      check (SearchTestAccess::intern (search, rank (warm)) == warm,
             "index/node allocation rollback permits exact retry");
      if (fail == count) break;
    }
  }
  std::cout << "interner refused allocations: " << stops << '\n';
  // Like the worker, also catch allocations before solve's inner boundary.
  for (size_t fail = 0; fail < 6; ++fail) {
    Search search {store, alphabet, 3};
    allocations = 0;
    refuse = fail;
    track = true;
    const auto stopped = detail::checked<SolveResult> ([&] { return search.solve (); });
    track = false;
    check (not stopped.value ? stopped.unknown == Unknown::resource_limit :
               stopped.value->status == forward_result_status::resource_limit &&
               stopped.value->failure == Unknown::resource_limit,
           "allocation refusal during solve's first intern never publishes a verdict");
  }
}

static void limits_and_lifetime (const spot::twa_graph_ptr& graph,
                                const letters::WorkerAlphabet& alphabet) {
  std::map<std::string, std::string> fields;
  Reporter report {[&] (const auto& key, const auto& value) { fields[key] = value; }};
  RowStore store {FixedBuchi {{graph}, {}, {}}, {}, report};
  Limits limits;
  limits.max_rank_nodes = 2;
  {
    Search search {store, alphabet, 3, limits};
    SearchTestAccess::intern (search, rank (0));
    SearchTestAccess::intern (search, rank (1));
    check (SearchTestAccess::intern (search, rank (0)) == 0, "equal rank allowed at node limit");
    const auto stopped = detail::checked<RankNodeId> ([&] {
      return SearchTestAccess::intern (search, rank (2));
    });
    check (not stopped.value && stopped.unknown == Unknown::resource_limit,
           "new rank refused at the same node limit");
    SearchTestAccess::consistent (search);
    search.report_counters ();
    const auto bytes = fields.at ("rank_interner_bytes");
    check (std::stoull (bytes) == 2 * sizeof (std::pair<const size_t, RankNodeId>),
           "telemetry charges hash/ID payload, without node-owned entries");
    auto moved = std::move (SearchTestAccess::result (search));
    SearchTestAccess::result (search).nodes.clear ();
    search.report_counters ();
    check (fields.at ("rank_interner_bytes") == bytes && moved.nodes.size () == 2,
           "reporting after moving result never resolves IDs");
    moved.nodes.clear ();  // Destroy owners before ~Search, too.
  }
  check (std::stoull (fields.at ("rank_interner_bytes")) ==
             2 * sizeof (std::pair<const size_t, RankNodeId>),
         "destructor reports the stored index payload after owners are destroyed");
  for (const bool cancel : {false, true}) {
    Limits stop;
    if (cancel) stop.queries.aborted = [] (void*) { return true; };
    else stop.max_rank_nodes = 0;
    const auto result = Search {store, alphabet, 3, stop}.solve ();
    check (result.failure == (cancel ? Unknown::aborted : Unknown::resource_limit) &&
           result.status == (cancel ? forward_result_status::unknown :
                                      forward_result_status::resource_limit),
           "node exhaustion and cancellation never publish a verdict");
  }
  for (const bool accepting : {false, true}) {
    auto g = games::graph (1, false);
    g->new_edge (0, 0, bddtrue, accepting ? spot::acc_cond::mark_t {0} :
                                         spot::acc_cond::mark_t {});
    const auto a = games::alphabet (g, false, false);
    RowStore rows {FixedBuchi {{g}, {}, {}}, {}, report};
    auto result = Search {rows, a, 3}.solve ();
    const auto bytes = fields.at ("rank_interner_bytes");
    check (result.status == (accepting ? forward_result_status::lose_k :
                                        forward_result_status::win_k),
           "moved solve result retains an independently verified verdict");
    check (std::stoull (bytes) == result.nodes.size () *
                                   sizeof (std::pair<const size_t, RankNodeId>),
           "successful solve/destructor retains index accounting");
  }
}

int main () {
  try {
    auto graph = games::graph (1, false);
    const auto alphabet = games::alphabet (graph, false, false);
    graph->new_edge (0, 0, bddtrue);
    RowStore store {FixedBuchi {{graph}, {}, {}}, {}};
    collisions_and_growth (store, alphabet);
    allocation_refusal (store, alphabet);
    limits_and_lifetime (graph, alphabet);
    std::cout << "rank interner: collisions, growth, refusal, limits and lifetime pass\n";
  }
  catch (const std::exception& e) {
    track = false;
    std::cerr << e.what () << '\n';
    return 1;
  }
}
