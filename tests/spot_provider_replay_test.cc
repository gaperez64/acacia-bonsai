// Unmeasured correctness reference only. This is not a C4/C5 timing arm.
// Compile the research specialization in isolation without its CLI entry point.
#define ACACIA_PROVIDER_REPLAY_TESTING
#include "research/spot_provider_replay.cc"

#include "research/explicit_forward_game.hh"

#include <cstdlib>
#include <fcntl.h>
#include <random>
#include <type_traits>
using namespace replay;
static_assert (not std::is_aggregate_v<SparseChoice>);

void expect (bool condition, const std::string& name) {
  if (not condition)
    throw std::runtime_error ("FAIL: " + name);
}

void check_verifier_split (const Fields& fields) {
  for (const auto* key : {"queries", "steps", "bdd_operations"}) {
    std::size_t sum = 0;
    for (const auto* phase : {"traversal", "invariant", "proof_bad"})
      sum += std::stoull (fields.at (std::string ("verify_") + phase + "_" + key));
    expect (sum == std::stoull (fields.at (std::string ("verify_") + key)),
            "verifier phases partition the cumulative " + std::string (key));
  }
}

void check_solve_counters (const SolveResult& result, const Fields& fields) {
  expect (result.scan_tombstones + result.scan_prefilter_rejects + result.scan_exact_compares ==
              result.subsumption_nodes_checked, "scan buckets partition every loop iteration");
  expect (result.scan_exact_compares >= result.subsumption_nodes_invalidated,
          "every invalidation requires an exact comparison");
  expect (result.proofs_total == result.proofs.size () &&
          result.proofs_in_initial_cone <= result.proofs_total, "proof cone fits the certificate");
  std::size_t sum = 0, maximum = 0;
  std::set<std::size_t> cone;
  if (result.initial_proof) cone.insert (*result.initial_proof);
  // Independent chronological propagation, rather than the production DFS.
  for (auto it = result.proofs.rbegin (); it != result.proofs.rend (); ++it) {
    const auto& deps = it->record.dependencies;
    sum += deps.size ();
    maximum = std::max (maximum, deps.size ());
    if (cone.contains (it->record.id)) cone.insert (deps.begin (), deps.end ());
  }
  expect (result.proofs_in_initial_cone == cone.size () &&
          result.dependency_list_len_sum == sum && result.dependency_list_len_max == maximum,
          "shape counters describe the recorded dependencies, including an absent root");
  Fields published;
  rank_metrics (result, Reporter {[&] (const auto& k, const auto& v) { published[k] = v; }});
  for (const auto* key : {"scan_tombstones", "scan_prefilter_rejects", "scan_exact_compares",
                         "proofs_total", "proofs_in_initial_cone", "dependency_list_len_sum",
                         "dependency_list_len_max"}) {
    expect (fields.at (key) == published.at (key),
            "destructor and SolveResult reader agree on " + std::string (key));
    expect (std::count (columns.begin (), columns.end (), key) == 1,
            "replay TSV contains exactly one column for " + std::string (key));
  }
  check_verifier_split (fields);
  const bool win = result.status == forward_result_status::win_k;
  for (const auto* key : {"queries", "steps", "bdd_operations"}) {
    const auto suffix = std::string ("_") + key;
    expect (std::stoull (fields.at ("verify_proof_bad" + suffix)) ==
                (win ? 0 : std::stoull (fields.at ("verify" + suffix))),
            "only the losing verifier charges the proof loop");
    if (not win)
      expect (fields.at ("verify_traversal" + suffix) == "0" &&
              fields.at ("verify_invariant" + suffix) == "0", "losing replay resets winning phases");
    for (const auto* phase : {"traversal", "invariant", "proof_bad"})
      expect (std::count (columns.begin (), columns.end (), "verify_" + std::string (phase) + suffix)
                  == 1, "verifier split is retained in the replay TSV");
  }
  if (win) {
    std::size_t queries = 1;  // maximal-antichain construction
    std::set<RankNodeId> reached {result.initial};
    std::vector<RankNodeId> todo {result.initial};
    while (not todo.empty ()) {
      const auto id = todo.back ();
      todo.pop_back ();
      ++queries;  // complete_rows
      for (const auto& choice : result.nodes[id].choices)
        if (choice.active) {
          queries += 3;  // reachability, output projection, coverage
          if (reached.insert (choice.successor).second) todo.push_back (choice.successor);
        }
    }
    expect (std::stoull (fields.at ("verify_traversal_queries")) == queries &&
            fields.at ("verify_invariant_queries") == "1", "winning query counts follow the obligations");
  }
  else {
    std::size_t queries = result.proofs.size ();  // chronology for EVERY proof
    for (const auto& proof : result.proofs)
      if (proof.record.reason == losing_reason::env_losing_input) queries += 3;
    expect (std::stoull (fields.at ("verify_proof_bad_queries")) == queries,
            "losing query count includes every proof, its rows, Bad and input restriction");
  }
}

namespace acacia::spot_lazy_game {
// SearchTestAccess exposes search state and operations under the replay-test
// build guard. Its legacy_* helpers are a test-only reference implementation
// used to compare against production expansion.
struct SearchTestAccess {
    static auto& result (Search& s) { return s.result_; }
    static auto& oracle (Search& s) { return s.oracle_; }
    static void drain (Search& s) { s.propagate_losses (); }
    static void initialize (Search& s) {
      s.result_.initial = s.intern (s.rows_->initial_rank ());
      detail::take (s.oracle_.query<bool> ([] (auto&) { return true; }));
    }
    static SolveResult scan_buckets (RowStore& store, const letters::WorkerAlphabet& alphabet) {
      Search s {store, alphabet, 2};
      s.intern (Rank {{{0, 1}}, 2});
      const auto tombstone = s.intern (Rank {{{2, 0}}, 2});
      s.result_.nodes[tombstone].losing = true;
      s.intern (Rank {{}, 2});                  // support rejection
      s.intern (Rank {{{1, 0}}, 2});            // mass rejection
      s.intern (Rank {{{1, 1}}, 2});            // equal mass, exact rejection
      s.intern (Rank {{{0, 1}, {1, 0}}, 2});    // merge join, invalidated
      s.enqueue_loss (0, losing_reason::env_unsafe);
      s.publish_counters ();
      return std::move (s.result_);
    }
    static void loss (Search& s, RankNodeId target) {
      s.result_.nodes.at (target).losing = true;
      s.losses_.push_back (target);
    }
    static void check_coverage (Search& s) {
      for (RankNodeId id = 0; id < s.result_.nodes.size (); ++id)
        expect (s.reconstruct_coverage (id) == s.result_.nodes[id].covered_inputs,
                "incremental coverage equals a fresh left-to-right OR");
    }
    static std::size_t activate (Search& s, RankNodeId source, bdd candidate, RankNodeId target) {
      auto& node = s.result_.nodes.at (source);
      const auto region = detail::take (s.oracle_.query<bdd> ([&] (auto& b) {
        return b.land (candidate, b.negate (node.covered_inputs));
      }));
      expect (region != bddfalse && not s.result_.nodes.at (target).losing,
              "activation admits only missing inputs with a live target");
      const auto id = node.choices.size ();
      node.choices.push_back (SparseChoice::constant (region, bddtrue, target));
      s.result_.nodes[target].incoming.push_back ({source, id});
      node.covered_inputs = detail::take (s.oracle_.query<bdd> ([&] (auto& b) {
        return b.lor (node.covered_inputs, region);
      }));
      check_coverage (s);
      return id;
    }
    static void legacy_rebuild (Search& s, RankNodeId id) {
      auto& node = s.result_.nodes[id];
      node.covered_inputs = detail::take (s.oracle_.query<bdd> ([&] (auto& b) {
        bdd covered = bddfalse;
        for (auto& choice : node.choices) {
          b.step ();
          if (s.result_.nodes[choice.successor].losing)
            choice.active = false;
          if (choice.active)
            covered = b.lor (covered, choice.input_region);
        }
        return covered;
      }));
    }
    static void legacy_drain (Search& s) {
      while (not s.losses_.empty ()) {
        const auto id = s.losses_.front ();
        std::set<RankNodeId> affected;
        for (const auto& ref : s.result_.nodes[id].incoming) {
          auto& choice = s.result_.nodes[ref.source].choices[ref.choice];
          if (choice.active && choice.successor == id) {
            choice.active = false;
            affected.insert (ref.source);
          }
        }
        for (const auto source : affected) {
          legacy_rebuild (s, source);
          if (not s.result_.nodes[source].losing) {
            ++s.result_.reopened_sources;
            if (not s.result_.nodes[source].queued)
              ++s.reopen_enqueues_;
            s.enqueue (source);
          }
        }
        s.losses_.pop_front ();
      }
    }
    static void same_decisions (Search& s, Search& ref) {
      const auto& a = s.result_;
      const auto& b = ref.result_;
      expect (s.open_ == ref.open_ && s.losses_ == ref.losses_ &&
              s.interned_ == ref.interned_ && s.losing_.ranks () == ref.losing_.ranks () &&
              s.losing_.proof_ids () == ref.losing_.proof_ids (),
              "S3 preserves queue order, interning and losing generators at every step");
      expect (a.initial == b.initial && a.initial_proof == b.initial_proof &&
              a.expansions == b.expansions && a.choices_created == b.choices_created &&
              a.nodes.size () == b.nodes.size () && a.proofs.size () == b.proofs.size () &&
              s.reopen_enqueues_ == ref.reopen_enqueues_ &&
              s.subsumption_scans_ == ref.subsumption_scans_ &&
              s.nodes_checked_ == ref.nodes_checked_ && s.nodes_invalidated_ == ref.nodes_invalidated_,
              "S3 preserves search decisions and actual reopen enqueues");
      // Legacy rebuilding eagerly cleared choices for other pending targets,
      // suppressing their later reopen attempts. S3 accounts for each event;
      // queued flags still ensure exactly the same actual enqueues above.
      expect (a.reopened_sources >= b.reopened_sources,
              "explicit invalidations retain every legacy reopen attempt");
      for (std::size_t id = 0; id < a.nodes.size (); ++id) {
        const auto& x = a.nodes[id];
        const auto& y = b.nodes[id];
        expect (x.rank == y.rank && x.active_rows_complete == y.active_rows_complete &&
                x.losing == y.losing && x.queued == y.queued &&
                x.covered_inputs == y.covered_inputs && x.choices.size () == y.choices.size () &&
                x.incoming.size () == y.incoming.size (), "S3 preserves every node");
        for (std::size_t j = 0; j < x.choices.size (); ++j) {
          const auto& c = x.choices[j];
          const auto& d = y.choices[j];
          expect (c.input_region == d.input_region && c.constant_output == d.constant_output &&
                  c.successor == d.successor && c.active == d.active,
                  "S3 preserves every choice, output, target and invalidation");
        }
        for (std::size_t j = 0; j < x.incoming.size (); ++j)
          expect (x.incoming[j].source == y.incoming[j].source &&
                  x.incoming[j].choice == y.incoming[j].choice,
                  "S3 preserves reverse-edge order");
      }
      for (std::size_t id = 0; id < a.proofs.size (); ++id) {
        const auto& x = a.proofs[id];
        const auto& y = b.proofs[id];
        expect (x.rank == y.rank && x.input == y.input && x.rows == y.rows &&
                x.record.id == y.record.id && x.record.reason == y.record.reason &&
                x.record.node == y.record.node && x.record.witness == y.record.witness &&
                x.record.dependencies == y.record.dependencies,
                "S3 preserves proof allocation, witnesses, dependencies and row obligations");
      }
    }
    static SolveResult compare_rebuild (RowStore& store, const letters::WorkerAlphabet& alphabet,
                                       int K) {
      Search s {store, alphabet, K};
      Search ref {store, alphabet, K};
      detail::take (detail::checked<bool> ([&] {
        store.phase = Phase::search;
        s.result_.initial = s.intern (s.rows_->initial_rank ());
        ref.result_.initial = ref.intern (ref.rows_->initial_rank ());
        for (;;) {
          s.propagate_losses ();
          legacy_drain (ref);
          same_decisions (s, ref);
          check_coverage (s);
          if (s.result_.nodes[s.result_.initial].losing || s.open_.empty ())
            break;
          for (auto* search : {&s, &ref}) {
            const auto id = search->open_.front ();
            search->open_.pop_front ();
            search->result_.nodes[id].queued = false;
            ++search->result_.expansions;
            // All losses have drained. Rebuilding before the unchanged setup
            // is equivalent to the old rebuild immediately after that setup.
            if (search == &ref)
              legacy_rebuild (ref, id);
            search->expand (id);
          }
          same_decisions (s, ref);
          check_coverage (s);
        }
        return true;
      }));
      // Finish through the real solve()/verification boundary in both cases.
      auto actual = s.solve ();
      const auto expected = ref.solve ();
      expect (actual.status == expected.status && actual.failure == expected.failure &&
              actual.generators == expected.generators, "S3 preserves verified certificates");
      return actual;
    }
};
} // namespace acacia::spot_lazy_game

std::vector<bdd> valuations (const letters::WorkerAlphabet& a, bdd vars) {
  std::vector<bdd> out {bddtrue};
  for (int v : a.order) {
    if (bdd_exist (vars, bdd_ithvar (v)) == vars)
      continue;
    std::vector<bdd> next;
    for (auto p : out) {
      next.push_back (p & bdd_nithvar (v));
      next.push_back (p & bdd_ithvar (v));
    }
    out = std::move (next);
  }
  return out;
}
// Independent arithmetic contract, including deactivated choices: target loss
// does not change the promise originally made by a region.
int choice_regions (RowStore& store, const letters::WorkerAlphabet& a, int K,
                    const SolveResult& actual, std::size_t& region_inputs) {
  for (const auto& node : actual.nodes)
    for (const auto& choice : node.choices) {
      if (not choice.constant_output.has_value ())
        return 6;
      const std::vector<bdd> outputs {*choice.constant_output};
      const auto& target = actual.nodes.at (choice.successor).rank;
      for (auto u : valuations (a, a.inputs)) {
        if ((u & choice.input_region) == bddfalse)
          continue;
        bool below = false, equal = false;
        for (auto c : outputs) {
          const auto post = rows::evaluate_sparse (store.cache, a, node.rank, u & c, K);
          if (not post.value)
            return 6;
          below |= post.value->leq (target);
          equal |= *post.value == target;
        }
        if (not below)
          return 6;
        if (not equal)
          return 7;
        ++region_inputs;
      }
    }
  return 0;
}

bdd check_losing_inputs (RowStore& store, const letters::WorkerAlphabet& a, int K,
                        const Rank& rank, const std::vector<Rank>& earlier,
                        std::size_t& input_checks) {
  Reader reader {store, false, {}};
  Oracle oracle {reader, store, a, K};
  const auto checked = oracle.bad (rank, earlier, earlier.size ());
  expect (checked.value.has_value (), "checked Bad query succeeds");
  const bdd bad = *checked.value;
  const bdd H = bdd_forall (bad, a.outputs);
  bdd expected_H = bddfalse;
  const auto inputs = valuations (a, a.inputs);
  for (auto u : inputs) {
    bool every_output_bad = true;
    for (auto c : valuations (a, a.outputs)) {
      const auto post = rows::evaluate_sparse (store.cache, a, rank, u & c, K);
      expect (post.value.has_value (), "independent successor arithmetic succeeds");
      bool unsafe_or_losing = not reader.is_safe (*post.value, K);
      for (const auto& loss : earlier)
        unsafe_or_losing |= loss.leq (*post.value);
      expect (((bad & u & c) != bddfalse) == unsafe_or_losing,
              "Bad matches unsafe or already-losing successor arithmetic");
      every_output_bad &= unsafe_or_losing;
    }
    expect (((H & u) != bddfalse) == every_output_bad,
            "H contains an input exactly when every output is bad");
    if (every_output_bad)
      expected_H |= u;
    ++input_checks;
  }
  expect (H == expected_H, "universal projection leaves exactly the losing inputs free");
  return H;
}

void check_losing_certificate (RowStore& store, const letters::WorkerAlphabet& a, int K,
                               const SolveResult& result) {
  const auto checked = verify_losing_proof (store, a, K, result);
  expect (checked.value && *checked.value, "losing proof independently replays");
  for (std::size_t id = 0; id < result.proofs.size (); ++id) {
    const auto& proof = result.proofs[id];
    expect (proof.record.id == id, "losing proof IDs are chronological");
    for (auto dep : proof.record.dependencies)
      expect (dep < id, "losing proof depends only on earlier proofs");
    if (proof.record.reason == losing_reason::env_losing_input) {
      Reader reader {store, false, {}};
      Oracle oracle {reader, store, a, K};
      expect (proof.input.has_value (), "losing input proof stores a witness");
      const auto total = oracle.restrict_total (bddtrue, *proof.input, Variables::inputs);
      expect (total.value && *total.value == bddtrue, "stored witness is a total input cube");
    }
  }
}

int differential (const Reporter& report) {
  unsigned checks = 0, corruptions = 0, arithmetic = 0;
  unsigned redundant_loss_events = 0;
  std::size_t losing_events = 0, antichain_insertions = 0;
  std::size_t broad_scans = 0, nodes_scanned = 0;
  std::size_t subsumption_queries = 0, subsumption_hits = 0;
  std::size_t expansions_total = 0, choices_total = 0, nodes_total = 0;
  std::size_t reopened_total = 0, invalidated_total = 0, removals_total = 0;
  std::size_t reopen_enqueues_total = 0, prefilter_skips_total = 0;
  std::size_t region_inputs = 0;
  std::size_t losing_input_checks = 0, symbolic_region_inputs = 0;
  for (const char* f :
       {"true", "false", "F a", "G a", "GF a", "GF a & GF b", "G(a -> F b)", "(a U b) | G c"}) {
    const auto dict = spot::make_bdd_dict ();
    auto p = spot::ltl_to_taa (spot::parse_infix_psl (f).f, dict, false);
    auto w = std::make_shared<lazy::LazyBuchiView> (p);
    Fields observed;
    RowStore store {w, {}, Reporter {[&] (const auto& key, const auto& value) {
      observed[key] = value;
      report.put (key, value);
    }}};
    store.enumerate_and_freeze ();
    const auto n = store.cache->state_count ();
    for (unsigned mask = 0; mask < (1u << p->ap ().size ()); ++mask) {
      Options opt;
      std::string part;
      for (size_t i = 0; i < p->ap ().size (); ++i)
        part += mask & (1u << i) ? 'u' : 'c';
      opt.partition = part;
      const auto a = alphabet (w, opt, report);
      std::vector<std::vector<acacia::research::action_vec>> table;
      for (auto u : valuations (a, a.inputs)) {
        table.emplace_back ();
        for (auto c : valuations (a, a.outputs)) {
          acacia::research::action_vec action (n);
          for (size_t q = 0; q < n; ++q)
            for (const auto& e : store.get (q).edges)
              if ((e.condition & u & c) != bddfalse)
                action[e.destination].emplace_back (q, e.increment);
          table.back ().push_back (std::move (action));
        }
      }
      for (int K : {1, 2, 3}) {
        const auto ref = acacia::research::solve_explicit_forward_game (
            acacia::research::initial_vector (n, store.cache->initial_id ()), table,
            static_cast<VECTOR_ELT_T> (K), n);
        const auto actual = Search {store, a, K}.solve ();
        const bool win = ref.status == acacia::research::forward_status::win_k;
        if (actual.status != (win ? solver_detail::forward_result_status::win_k
                                  : solver_detail::forward_result_status::lose_k)) {
          std::cerr << "MISMATCH " << f << ' ' << part << ' ' << K << '\n';
          return 1;
        }
        check_solve_counters (actual, observed);
        const auto incremental = SearchTestAccess::compare_rebuild (store, a, K);
        expect (incremental.status == actual.status && incremental.failure == Unknown::none,
                "incremental coverage and rebuilding match the explicit game");
        expect (choice_regions (store, a, K, actual, symbolic_region_inputs) == 0,
                "actual search preserves choice-region arithmetic");
        for (const auto& node : actual.nodes)
          check_losing_inputs (store, a, K, node.rank, {}, losing_input_checks);
        if (not win) {
          check_losing_certificate (store, a, K, actual);
          LossSet earlier;
          for (const auto& proof : actual.proofs) {
            check_losing_inputs (store, a, K, proof.rank, earlier.ranks (), losing_input_checks);
            earlier.insert (proof.rank, proof.record.id);
          }
          auto invalid = actual;
          invalid.proofs.at (*invalid.initial_proof)
              .record.dependencies.push_back (*invalid.initial_proof);
          expect (not verify_losing_proof (store, a, K, invalid, {}).value,
                  "actual proof with a nonchronological dependency is rejected");
        }
        Reader reader {store, false, {}};
        Oracle sparse {reader, store, a, K};
        for (const auto& node : actual.nodes)
          for (auto v : valuations (a, a.ap_vars)) {
            const auto expected = rows::evaluate_sparse (store.cache, a, node.rank, v, K);
            const auto computed = sparse.evaluate (node.rank, v);
            if (!expected.value || !computed.value || *expected.value != *computed.value)
              return 3;
            ++arithmetic;
          }
        // Losing-region accounting.  These hold in any correct version and are
        // the baseline the growth-gated scan has to improve without breaking:
        // today one broad scan runs per loss event, so scans >= insertions.
        if (actual.subsumption_hits > actual.subsumption_queries)
          return 8;
        if (actual.losing_antichain_size + actual.losing_removals
            != actual.losing_insertions)
          return 9;
        if (actual.losing_antichain_size > actual.losing_antichain_peak)
          return 10;
        if (actual.subsumption_nodes_invalidated > actual.subsumption_nodes_checked)
          return 11;
        if (actual.subsumption_nodes_checked
            > actual.subsumption_scans * actual.nodes.size ())
          return 12;
        // One broad scan per generator, not one per loss event: the scan is
        // what makes the region's growth known, so it runs exactly when the
        // region grows.
        if (actual.subsumption_scans != actual.losing_insertions)
          return 13;
        if (actual.reopen_enqueues > actual.reopened_sources)
          return 15;
        // A loss event whose rank was already inside the losing region: the
        // proof is recorded but no generator is added.  These are exactly the
        // events whose broad scan can find nothing.
        if (actual.proofs.size () > actual.losing_insertions)
          ++redundant_loss_events;
        expansions_total += actual.expansions;
        choices_total += actual.choices_created;
        nodes_total += actual.nodes.size ();
        reopened_total += actual.reopened_sources;
        reopen_enqueues_total += actual.reopen_enqueues;
        invalidated_total += actual.subsumption_nodes_invalidated;
        removals_total += actual.losing_removals;
        subsumption_queries += actual.subsumption_queries;
        subsumption_hits += actual.subsumption_hits;
        prefilter_skips_total += actual.subsumption_prefilter_skips;
        losing_events += actual.proofs.size ();
        antichain_insertions += actual.losing_insertions;
        broad_scans += actual.subsumption_scans;
        nodes_scanned += actual.subsumption_nodes_checked;

        if (const int code = choice_regions (store, a, K, actual, region_inputs))
          return code;

        auto corrupt = actual;
        if (win) {
          corrupt.nodes[corrupt.initial].choices.clear ();
          if (verify_winning_certificate (store, a, K, corrupt, {}).value)
            return 4;
        }
        else {
          corrupt.proofs.at (*corrupt.initial_proof)
              .record.dependencies.push_back (*corrupt.initial_proof);
          if (verify_losing_proof (store, a, K, corrupt, {}).value)
            return 5;
        }
        ++checks;
        ++corruptions;
      }
    }
  }
  std::cout << checks << " explicit-game matches, " << arithmetic << " sparse arithmetic matches, "
            << corruptions << " corrupt certificates rejected\n";
  std::cout << checks << " S2 on/off matches, " << losing_input_checks
            << " universally losing inputs checked by enumeration, " << symbolic_region_inputs
            << " S2 choice-region inputs checked\n";
  std::cout << 2 * checks << " S3/legacy rebuild traces match with S2 off/on\n";
  std::cout << 2 * checks << " V1 on/off explicit-game matches and dependency subset checks\n";
  std::cout << losing_events << " loss events, " << antichain_insertions
            << " antichain insertions, " << broad_scans << " broad scans over "
            << nodes_scanned << " node checks, " << redundant_loss_events
            << " games with a redundant loss event\n";
  std::cout << subsumption_queries << " subsumption queries, " << subsumption_hits
            << " hits, " << prefilter_skips_total << " prefilter skips\n";
  // The search itself must be untouched by how the losing region is scanned.
  std::cout << "search shape: " << expansions_total << " expansions, " << choices_total
            << " choices, " << nodes_total << " nodes, " << reopened_total
            << " reopened, " << invalidated_total << " invalidated, " << removals_total
            << " removals\n";
  std::cout << "reopen enqueues: " << reopen_enqueues_total << ", "
            << region_inputs << " choice-region inputs checked against the\n"
               "                 independent sparse arithmetic\n";
  if (region_inputs == 0) {
    std::cerr << "FAIL: no choice region was checked\n";
    return 16;
  }
  if (redundant_loss_events == 0) {
    // Without one of these the growth gate would be untested by this sweep.
    std::cerr << "FAIL: no game produced a loss event inside the known region\n";
    return 14;
  }
  {
    // A forged strategy omits the accepting successor row. The verifier must
    // request it, charge it as additional active work, and reject the strategy.
    const auto dict = spot::make_bdd_dict ();
    auto p = spot::make_twa_graph (dict);
    p->new_states (2);
    p->set_init_state (0);
    p->set_buchi ();
    p->new_edge (0, 1, bddtrue);
    p->new_edge (1, 1, bddtrue, {0});
    auto w = std::make_shared<lazy::LazyBuchiView> (p);
    RowStore store {w, {}, report};
    store.phase = Phase::search;
    store.get (0);
    SolveResult forged;
    forged.provider = w;
    forged.nodes.push_back ({Rank {{{0, 0}}, 1}, true});
    forged.nodes.push_back ({Rank {{{1, 0}}, 1}, true});
    for (auto& node : forged.nodes) {
      node.covered_inputs = bddtrue;
      node.choices.push_back (SparseChoice::constant (bddtrue, bddtrue, 1));
    }
    store.phase = Phase::verify;
    const letters::WorkerAlphabet a {bddtrue, bddtrue, bddtrue, {}};
    if (verify_winning_certificate (store, a, 1, forged, {}).value)
      return 6;
    if (store.search_sources != std::set<StateId> {0} ||
        store.verifier_sources != std::set<StateId> {0, 1} || store.verify_generated != 1)
      return 7;
  }
  return 0;
}

// These kernels use transition-Buchi rows and Search directly, with the same
// arithmetic oracle as the differential sweep. No frontend fast path is involved.
struct Kernel {
    spot::twa_graph_ptr graph = spot::make_twa_graph (spot::make_bdd_dict ());
    letters::WorkerAlphabet alphabet {bddtrue, bddtrue, bddtrue, {}};
    explicit Kernel (unsigned states) {
      graph->set_buchi ();
      graph->prop_state_acc (false);
      graph->new_states (states);
      graph->set_init_state (0);
    }
    bdd ap (const std::string& name, bool input) {
      const int var = graph->register_ap (name);
      const bdd v = bdd_ithvar (var);
      alphabet.order.push_back (var);
      alphabet.ap_vars &= v;
      (input ? alphabet.inputs : alphabet.outputs) &= v;
      return v;
    }
    auto view () const { return std::make_shared<lazy::LazyBuchiView> (graph); }
};

void verifier_phase_metrics () {
  Kernel k {1};
  k.graph->new_edge (0, 0, bddtrue);
  Fields observed;
  bool abort_invariant = false, traversal_finished = false;
  RowStore store {k.view (), {}, Reporter {[&] (const auto& key, const auto& value) {
    observed[key] = value;
    if (abort_invariant && key == "verify_traversal_queries" && value != "0")
      traversal_finished = true;
  }}};
  store.enumerate_and_freeze ();
   {
    const auto result = Search {store, k.alphabet, 1, {}}.solve ();
    check_solve_counters (result, observed);
    const auto successful = observed;
    // Warm only the rank predicates that traversal uses, then independently
    // measure the invariant via the original cumulative Oracle::report API.
    Reader reader {store, true, {}};
    Oracle oracle {reader, store, k.alphabet, 1};
    const auto& rank = result.nodes.at (result.initial).rank;
    detail::take (true
                      ? oracle.eq (rank, rank) : oracle.down (rank, rank));
    Fields before, after;
    oracle.report (Reporter {[&] (const auto& key, const auto& value) { before[key] = value; }}, "");
    expect (detail::take (oracle.invariant (rank, result.generators, 0)) == letters::Invariant::verified,
            "independent invariant succeeds with the traversal's predicate cache");
    oracle.report (Reporter {[&] (const auto& key, const auto& value) { after[key] = value; }}, "");
    for (const auto* key : {"queries", "steps", "bdd_operations"})
      expect (std::stoull (successful.at (std::string ("verify_invariant_") + key)) ==
                  std::stoull (after.at (key)) - std::stoull (before.at (key)),
              "invariant bucket equals independently measured warm-cache " + std::string (key));
    expect (std::stoull (successful.at ("verify_invariant_bdd_operations")) > 0 &&
            std::stoull (successful.at ("verify_traversal_bdd_operations")) > 0,
            "both winning phases do BDD work");

    Limits limits;
    limits.verifier_queries.max_steps = 0;
    const auto failed = verify_winning_certificate (store, k.alphabet, 1, result, limits);
    expect (not failed.value && failed.unknown == Unknown::resource_limit &&
            observed.at ("verify_traversal_queries") == "1" &&
            observed.at ("verify_invariant_queries") == "0", "early failure charges traversal only");
    check_verifier_split (observed);

    limits = {};
    limits.verifier_queries.aborted = [] (void* data) { return *static_cast<bool*> (data); };
    limits.verifier_queries.abort_data = &traversal_finished;
    abort_invariant = true;
    const auto aborted = verify_winning_certificate (store, k.alphabet, 1, result, limits);
    abort_invariant = traversal_finished = false;
    expect (not aborted.value && aborted.unknown == Unknown::aborted &&
            observed.at ("verify_invariant_queries") == "1" &&
            observed.at ("verify_invariant_steps") == "0" &&
            observed.at ("verify_invariant_bdd_operations") == "0",
            "failure entering the invariant charges its query without repeating traversal work");
    for (const auto* key : {"queries", "steps", "bdd_operations"}) {
      const auto field = std::string ("verify_traversal_") + key;
      expect (observed.at (field) == successful.at (field), "invariant failure preserves traversal totals");
    }
    check_verifier_split (observed);
  }

  Kernel loss {2};
  loss.graph->new_edge (0, 1, bddtrue);
  loss.graph->new_edge (1, 1, bddtrue, {0});
  RowStore losing_store {loss.view (), {}, store.report};
  losing_store.enumerate_and_freeze ();
  const auto result = Search {losing_store, loss.alphabet, 1}.solve ();
  expect (result.status == forward_result_status::lose_k, "proof-loop fixture loses");
  check_solve_counters (result, observed);
  const auto successful = observed;
  auto corrupt = result;
  // An orphan proof must still be checked, even though it cannot be in the
  // initial proof's transitive cone. Fail on its missing input after all the
  // original Bad obligations have completed.
  auto orphan = corrupt.proofs.at (*corrupt.initial_proof);
  orphan.record.id = corrupt.proofs.size ();
  orphan.input.reset ();
  corrupt.proofs.push_back (std::move (orphan));
  const auto failed = verify_losing_proof (losing_store, loss.alphabet, 1, corrupt);
  expect (not failed.value && failed.unknown == Unknown::invalid_query &&
          std::stoull (observed.at ("verify_proof_bad_queries")) ==
              std::stoull (successful.at ("verify_proof_bad_queries")) + 1,
          "losing replay still checks an orphan after every original proof");
  check_verifier_split (observed);
}

void certificate_shape_metrics () {
  Kernel k {1};
  Fields observed;
  RowStore store {k.view (), {}, Reporter {[&] (const auto& key, const auto& value) {
    observed[key] = value;
  }}};
  const auto scan = SearchTestAccess::scan_buckets (store, k.alphabet);
  expect (scan.subsumption_nodes_checked == 6 && scan.scan_tombstones == 2 &&
          scan.scan_prefilter_rejects == 2 && scan.scan_exact_compares == 2 &&
          scan.subsumption_nodes_invalidated == 1,
          "scan classifies tombstones, both prefilters, equal-mass rejection and merge-join success");
  expect (observed.at ("scan_tombstones") == "2" && observed.at ("scan_prefilter_rejects") == "2" &&
          observed.at ("scan_exact_compares") == "2", "scan reporting survives moving the result");
  for (bool has_root : {false, true}) {
    SolveResult published;
    {
      Search search {store, k.alphabet, 1};
      auto& result = SearchTestAccess::result (search);
      // Diamond sharing proof 0, plus orphan proof 1. Closure counts shared
      // dependencies once, while list lengths count every recorded edge.
      const std::vector<std::vector<std::size_t>> deps {{}, {}, {0}, {0}, {2, 3}};
      for (std::size_t id = 0; id < deps.size (); ++id)
        result.proofs.push_back ({{id, losing_reason::env_subsumed, 0, 0, deps[id]},
                                 Rank {{{0, 0}}, 1}, {}, {}});
      if (has_root) result.initial_proof = 4;
      search.publish_counters ();
      published = std::move (result);
    }
    expect (published.proofs_total == 5 && published.proofs_in_initial_cone == (has_root ? 4 : 0) &&
            published.dependency_list_len_sum == 4 && published.dependency_list_len_max == 2,
            "diamond closure excludes orphans, deduplicates shared dependencies and handles no root");
    expect (observed.at ("proofs_total") == "5" &&
            observed.at ("proofs_in_initial_cone") == (has_root ? "4" : "0") &&
            observed.at ("dependency_list_len_sum") == "4" &&
            observed.at ("dependency_list_len_max") == "2", "shape reporting survives moving the result");
  }
}

void incremental_coverage_events () {
  Kernel k {1};
  const bdd u = k.ap ("u", true), v = k.ap ("v", true);
  RowStore store {k.view (), {}, {}};
  Search s {store, k.alphabet, 1};
  auto& result = SearchTestAccess::result (s);
  result.nodes.resize (5, GuardedRankNode {Rank {{}, 1}});
  auto& oracle = SearchTestAccess::oracle (s);
  const auto regions = acacia::spot_lazy_game::detail::take (
      oracle.query<std::vector<bdd>> ([&] (auto& b) {
        return std::vector<bdd> {b.land (u, v), b.land (u, b.negate (v)), b.negate (u)};
      }));
  const auto first = SearchTestAccess::activate (s, 0, regions[0], 1);
  SearchTestAccess::activate (s, 0, regions[1], 1);
  SearchTestAccess::activate (s, 0, regions[2], 2);
  result.nodes[1].incoming.push_back ({0, first}); // Duplicate reference to an active choice.
  SearchTestAccess::loss (s, 1);
  SearchTestAccess::loss (s, 1); // Repeated processing of the same target loss.
  SearchTestAccess::drain (s);
  SearchTestAccess::check_coverage (s);
  expect (result.nodes[0].covered_inputs == regions[2] && result.reopened_sources == 1,
          "one target invalidates both choices once despite duplicate events/references");
  const auto replacement = SearchTestAccess::activate (s, 0, u, 3);
  result.nodes[1].incoming.push_back ({0, replacement}); // Active, but wrong target identity.
  SearchTestAccess::loss (s, 1);
  SearchTestAccess::drain (s);
  SearchTestAccess::check_coverage (s);
  expect (result.nodes[0].covered_inputs == bddtrue && result.reopened_sources == 1,
          "stale invalidations cannot subtract historical regions overlapping a replacement");

  // Two distinct pending losses affect one source. The old reconstruction
  // hid the second reopen attempt by clearing it while processing the first.
  SearchTestAccess::loss (s, 2);
  SearchTestAccess::loss (s, 3);
  SearchTestAccess::drain (s);
  SearchTestAccess::check_coverage (s);
  s.publish_counters ();
  expect (result.nodes[0].covered_inputs == bddfalse && result.reopened_sources == 3 &&
          result.reopen_enqueues == 1,
          "distinct losses count both invalidations but enqueue the affected source only once");
  SearchTestAccess::activate (s, 4, bddtrue, 4);
  SearchTestAccess::loss (s, 4);
  SearchTestAccess::loss (s, 4);
  SearchTestAccess::drain (s);
  SearchTestAccess::check_coverage (s);
  expect (not result.nodes[4].choices[0].active && result.nodes[4].covered_inputs == bddfalse &&
          result.reopened_sources == 3, "losing self-loop subtracts once without reopening itself");
}

void incremental_coverage_property () {
  Kernel k {1};
  for (unsigned i = 0; i < 4; ++i)
    k.ap ("u" + std::to_string (i), true);
  RowStore store {k.view (), {}, {}};
  std::size_t activations = 0, invalidations = 0, overlaps = 0;
  for (unsigned seed = 0; seed < 16; ++seed) {
    std::mt19937 random {seed};
    Search s {store, k.alphabet, 1};
    auto& result = SearchTestAccess::result (s);
    result.nodes.push_back (GuardedRankNode {Rank {{}, 1}});
    auto& oracle = SearchTestAccess::oracle (s);
    const auto cubes = acacia::spot_lazy_game::detail::take (
        oracle.query<std::vector<bdd>> ([&] (auto& b) {
          return b.call ([&] { return valuations (k.alphabet, k.alphabet.inputs); });
        }));
    for (unsigned step = 0; step < 256; ++step) {
      if (result.nodes.size () == 1 || (random () % 2 && result.nodes[0].covered_inputs != bddtrue)) {
        const auto region = acacia::spot_lazy_game::detail::take (oracle.query<bdd> ([&] (auto& b) {
          bdd candidate = bddfalse;
          for (auto cube : cubes)
            if (random () % 2)
              candidate = b.lor (candidate, cube);
          const auto missing = b.negate (result.nodes[0].covered_inputs);
          auto region = b.land (candidate, missing);
          if (region == bddfalse)
            region = missing;
          for (const auto& old : result.nodes[0].choices)
            if (not old.active && b.land (region, old.input_region) != bddfalse) {
              ++overlaps;
              break;
            }
          return region;
        }));
        // Reuse live targets as well as creating fresh ones, so one loss can
        // invalidate many disjoint regions without recycling historical choices.
        RankNodeId target = random () % result.nodes.size ();
        if (target == 0 || result.nodes[target].losing) {
          target = result.nodes.size ();
          result.nodes.push_back (GuardedRankNode {Rank {{}, 1}});
        }
        SearchTestAccess::activate (s, 0, region, target);
        ++activations;
      }
      else {
        const auto target = 1 + random () % (result.nodes.size () - 1);
        if (not result.nodes[target].incoming.empty ())
          result.nodes[target].incoming.push_back (result.nodes[target].incoming.front ());
        SearchTestAccess::loss (s, target);
        SearchTestAccess::drain (s);
        ++invalidations;
      }
      SearchTestAccess::check_coverage (s);
    }
  }
  expect (activations > 100 && invalidations > 100 && overlaps > 100,
          "random coverage histories exercise activation, loss and overlapping historical regions");
  std::cout << activations + invalidations << " randomized coverage operations checked, "
            << overlaps << " activations overlap inactive history\n";
}

void incremental_coverage_failure () {
  Kernel k {1};
  const bdd u = k.ap ("u", true);
  k.graph->new_edge (0, 0, bddtrue);
  RowStore store {k.view (), {}, {}};
  store.enumerate_and_freeze ();
  // Fail after each BDD operation, and at the final query checkpoint. Repeat
  // on the second choice so the loss has already been partially propagated.
  for (std::size_t checkpoint : {3, 5, 6, 9, 11, 12}) {
    Search s {store, k.alphabet, 1};
    SearchTestAccess::initialize (s);
    auto& result = SearchTestAccess::result (s);
    result.nodes.resize (2, GuardedRankNode {Rank {{}, 1}});
    auto& oracle = SearchTestAccess::oracle (s);
    const auto not_u = acacia::spot_lazy_game::detail::take (
        oracle.query<bdd> ([&] (auto& b) { return b.negate (u); }));
    SearchTestAccess::activate (s, 0, u, 1);
    SearchTestAccess::activate (s, 0, not_u, 1);
    SearchTestAccess::loss (s, 1);
    struct Budget { std::size_t calls = 0, fail_at; } budget {0, checkpoint};
    letters::QueryLimits limits;
    limits.aborted = [] (void* data) {
      auto& budget = *static_cast<Budget*> (data);
      if (++budget.calls == budget.fail_at)
        throw std::bad_alloc (); // Existing checked boundary classifies resource failures.
      return false;
    };
    limits.abort_data = &budget;
    oracle.set_limits (limits);
    const auto failed = s.solve ();
    expect (budget.calls == checkpoint && failed.status == forward_result_status::resource_limit &&
            failed.failure == Unknown::resource_limit && failed.pending_loss &&
            failed.generators.empty () && store.phase == Phase::search,
            "resource failure during coverage subtraction leaves an inconclusive pending loss");
    expect (failed.nodes[0].choices[0].active == (checkpoint <= 6) &&
            failed.nodes[0].choices[1].active &&
            failed.nodes[0].covered_inputs == (checkpoint <= 6 ? bddtrue : not_u),
            "failed update publishes neither its union nor its inactive flag");
    expect (not verify_winning_certificate (store, k.alphabet, 1, failed).value &&
            not verify_losing_proof (store, k.alphabet, 1, failed).value,
            "neither certificate verifier can accept a failed coverage update");
  }
}

void check_regions (RowStore& store, const letters::WorkerAlphabet& a, const SolveResult& result) {
  std::size_t count = 0;
  expect (choice_regions (store, a, 1, result, count) == 0 && count != 0,
          "kernel choice-region arithmetic");
}

void copy_kernels () {
  for (unsigned n : {1, 2, 4}) {
    Kernel k {2};
    bdd copy = bddtrue;
    for (unsigned j = 0; j < n; ++j) {
      // Paired AP registration and valuation order: u0,c0,u1,c1,...
      const bdd u = k.ap ("u" + std::to_string (j), true);
      const bdd c = k.ap ("c" + std::to_string (j), false);
      copy &= bdd_biimp (u, c);
    }
    k.graph->new_edge (0, 0, copy);
    k.graph->new_edge (0, 1, !copy, {0});
    k.graph->new_edge (1, 1, bddtrue, {0});
    RowStore store {k.view (), {}, {}};
    store.enumerate_and_freeze ();
    const Rank safe {{{0, 0}}, 1}, unsafe {{{1, 1}}, 1};
    // Independently establish that this is the intended relation at K=1.
    for (auto letter : valuations (k.alphabet, k.alphabet.ap_vars)) {
      const auto post = rows::evaluate_sparse (store.cache, k.alphabet, safe, letter, 1);
      expect (post.value && *post.value == ((letter & copy) != bddfalse ? safe : unsafe),
              "copy kernel has exactly one safe output per input");
    }
    expect (bdd_exist (copy, k.alphabet.outputs) == bddtrue &&
            bdd_exist (bdd_forall (copy, k.alphabet.inputs), k.alphabet.outputs) == bddfalse,
            "copy kernel distinguishes forall-input exists-output from the reverse order");
     {
      const auto result = Search {store, k.alphabet, 1, {}}.solve ();
      const auto& node = result.nodes.at (result.initial);
      const std::size_t expected = 1u << n;
      expect (result.status == forward_result_status::win_k && node.rank == safe &&
              node.covered_inputs == bddtrue && node.choices.size () == expected &&
              result.choices_created == expected, "copy kernel region count");
      for (const auto& choice : node.choices)
        expect (choice.active && choice.successor == result.initial, "copy kernel self-loop");
      check_regions (store, k.alphabet, result);
      std::cout << "copy n=" << n << ' '
                  << "constant"
                  << ": " << node.choices.size () << " regions\n";
    }
  }
}

void missing_output_kernel () {
  Kernel k {2};
  const bdd u = k.ap ("u", true), c = k.ap ("c", false);
  const bdd legal = (!u) & (!c);
  k.graph->new_edge (0, 0, legal);
  k.graph->new_edge (0, 1, !legal, {0});
  k.graph->new_edge (1, 1, bddtrue, {0});
  RowStore store {k.view (), {}, {}};
  store.enumerate_and_freeze ();
  expect (bdd_exist (legal, k.alphabet.outputs) == !u &&
          bdd_exist (legal, k.alphabet.ap_vars) == bddtrue,
          "missing-output kernel distinguishes projection over outputs from all APs");
   {
    // Keep the sampling control independent of the build's default.
    const auto result = Search {store, k.alphabet, 1, {}}.solve ();
    expect (result.status == forward_result_status::lose_k, "input with no legal output loses");
    const auto& node = result.nodes.at (result.initial);
    expect (node.covered_inputs != bddtrue && node.choices.size () == 1 &&
            node.choices[0].input_region == !u, "missing legal output prevents full coverage");
    check_regions (store, k.alphabet, result);
    check_losing_certificate (store, k.alphabet, 1, result);
  }
}

void corrupt_certificates () {
  // The two safe targets have disjoint projections. The safe ranks still form
  // an inductive invariant after changing a region/edge, so the final invariant
  // cannot hide an unsound per-choice verifier.
  Kernel k {4};
  const bdd u = k.ap ("u", true), c = k.ap ("c", false);
  k.graph->new_edge (0, 1, (!u) & (!c));
  k.graph->new_edge (0, 2, u & c);
  k.graph->new_edge (0, 3, !bdd_biimp (u, c), {0});
  k.graph->new_edge (1, 1, bddtrue);
  k.graph->new_edge (2, 2, bddtrue);
  k.graph->new_edge (3, 3, bddtrue, {0});
  RowStore store {k.view (), {}, {}};
  store.enumerate_and_freeze ();
   {
    const auto good = Search {store, k.alphabet, 1, {}}.solve ();
    expect (good.status == forward_result_status::win_k &&
            good.nodes.at (good.initial).choices.size () == 2, "split-target fixture wins");
    expect (verify_winning_certificate (store, k.alphabet, 1, good, {}).value.has_value (),
            "uncorrupted split-target certificate replays");
    check_regions (store, k.alphabet, good);
    auto reject = [&] (const std::string& name, auto mutate) {
      auto bad = good;
      mutate (bad, bad.nodes.at (bad.initial));
      const auto checked = verify_winning_certificate (store, k.alphabet, 1, bad, {});
      expect (not checked.value && checked.unknown == Unknown::invalid_query, name);
    };
    reject ("region beyond freshly rebuilt projection rejected", [] (auto&, auto& node) {
      node.choices[0].input_region = bddtrue;
    });
    reject ("changed safe target rejected", [] (auto&, auto& node) {
      node.choices[0].successor = node.choices[1].successor;
    });
    reject ("removed edge rejected", [] (auto&, auto& node) { node.choices.pop_back (); });
    reject ("covered_inputs must equal the reconstructed union", [] (auto&, auto& node) {
      node.covered_inputs = bddfalse;
    });
    reject ("omitted active row rejected", [] (auto& bad, auto& node) {
      bad.nodes[node.choices[0].successor].active_rows_complete = false;
    });
      reject ("constant choice must contain a cube", [] (auto&, auto& node) {
        node.choices[0].constant_output.reset ();
      });
      reject ("constant choice must contain a total cube", [] (auto&, auto& node) {
        node.choices[0].constant_output = bddtrue;
      });
      reject ("constant choice must contain a satisfying cube", [&] (auto&, auto& node) {
        node.choices[0].constant_output = c;
      });

  }
}

void empty_alphabets_and_failures () {
  for (unsigned mask : {0, 1, 2, 3}) {
    Kernel k {2};
    if (mask & 1)
      k.ap ("u", true);
    const bdd legal = mask & 2 ? k.ap ("c", false) : bdd (bddtrue);
    k.graph->new_edge (0, 0, legal);
    k.graph->new_edge (0, 1, !legal, {0});
    k.graph->new_edge (1, 1, bddtrue, {0});
    RowStore store {k.view (), {}, {}};
    store.enumerate_and_freeze ();
     {
      const auto result = Search {store, k.alphabet, 1, {}}.solve ();
      expect (result.status == forward_result_status::win_k && result.choices_created == 1,
              "empty input/output alphabets retain their single empty valuation");
      check_regions (store, k.alphabet, result);
      Limits limits;
      limits.verifier_queries.max_steps = 0;
      const auto checked = verify_winning_certificate (store, k.alphabet, 1, result, limits);
      expect (not checked.value && checked.unknown == Unknown::resource_limit,
              "verifier budget failure has no Boolean result");
      const auto failed = Search {store, k.alphabet, 1, limits}.solve ();
      expect (failed.status == forward_result_status::unknown && failed.failure == Unknown::resource_limit,
              "search whose verification runs out of budget stays UNKNOWN");
      limits = {};
      limits.queries.aborted = [] (void*) { return true; };
      const auto aborted = Search {store, k.alphabet, 1, limits}.solve ();
      expect (aborted.status == forward_result_status::unknown && aborted.failure == Unknown::aborted,
              "aborted search query stays UNKNOWN");
    }
    Reader reader {store, false, {}};
    Oracle oracle {reader, store, k.alphabet, 1};
    const auto present_false = oracle.query<bdd> ([&] (auto& b) {
      return b.exists (bddfalse, b.vars (Variables::outputs));
    });
    expect (present_false.value && *present_false.value == bddfalse &&
            present_false.unknown == Unknown::none, "false projection is a present result");
    bool abort_projection = false;
    letters::QueryLimits limits;
    limits.aborted = [] (void* flag) { return *static_cast<bool*> (flag); };
    limits.abort_data = &abort_projection;
    oracle.set_limits (limits);
    const auto absent = oracle.query<bdd> ([&] (auto& b) {
      abort_projection = true; // Fail inside the projection, after alphabet validation.
      return b.exists (bddfalse, b.vars (Variables::outputs));
    });
    expect (not absent.value && absent.unknown == Unknown::aborted,
            "failed existential projection is absent, distinguishable from present false");
    oracle.set_limits ({});
    const auto recovered = oracle.query<bdd> ([&] (auto& b) {
      return b.exists (bddfalse, b.vars (Variables::outputs));
    });
    expect (recovered.value && *recovered.value == bddfalse, "fresh query after failure can return false");
  }
}

// Structural identity deliberately ignores both provider and SpotRows local IDs.
// Formula text identifies closure nodes across independent factory contexts.
std::string closure_key (RowStore& store, StateId q) {
  const auto p = std::dynamic_pointer_cast<const acacia::closure_buchi::Provider> (store.provider);
  const auto id = p->state_id (store.cache->canonical_state (q));
  std::vector<std::string> obligations;
  for (auto c : p->obligations (id)) obligations.push_back (spot::str_psl (p->closure ()[c]));
  std::sort (obligations.begin (), obligations.end ());
  std::string key = std::to_string (p->cursor (id)) + ':';
  for (const auto& f : obligations) key += std::to_string (f.size ()) + ':' + f;
  return key;
}

void closure_games () {
  unsigned games = 0, arithmetic = 0, wins = 0, losses = 0;
  size_t reference_nodes = 0;
  for (const char* text : {"true", "false", "G(u <-> c)", "G(u <-> X c)",
                           "G(c <-> X u)", "GF u & GF c", "G(u -> F c)", "F(u & c)"}) {
    for (bool unreal : {false, true}) {
      auto f = spot::parse_infix_psl (text).f;
      if (unreal) {
        auto shift = [&] (auto&& self, spot::formula t) -> spot::formula {
          if (t.is (spot::op::ap) && t.ap_name () == "c") return spot::formula::X (t);
          return t.map ([&] (spot::formula child) { return self (self, child); });
        };
        f = shift (shift, f);
      }
      else f = spot::formula::Not (f);
      for (int K : {1, 2, 3}) {
        const auto dict = spot::make_bdd_dict ();
        Fields observed;
        auto lazy_store = acacia::spot_lazy_worker::make_closure_store (
            f, dict, {}, Reporter {[&] (const auto& k, const auto& v) { observed[k] = v; }});
        auto eager_store = acacia::spot_lazy_worker::make_closure_store (f, dict, {});
        auto& demand = *lazy_store;
        auto& eager = *eager_store;
        expect (!demand.view && demand.cache->complete_rows () == 0,
                "closure factory/snapshot uses generic path without TAA state cast or rows");
        eager.enumerate_and_freeze ();
        const auto n = eager.cache->state_count ();
        std::map<std::string, StateId> structural;
        for (size_t q = 0; q < n; ++q) structural.emplace (closure_key (eager, q), 0);
        StateId next = 0;
        for (auto& [key, id] : structural) id = next++;
        auto coordinate = [&] (RowStore& store, StateId q) { return structural.at (closure_key (store, q)); };
        Options opt;
        std::string part;
        std::vector<std::string> names;
        for (auto ap : eager.provider->ap ()) names.push_back (ap.ap_name ());
        std::sort (names.begin (), names.end ());
        for (const auto& name : names) part += (name == (unreal ? "c" : "u")) ? 'u' : 'c';
        opt.partition = part;
        const auto a = alphabet (eager.provider, opt, {});
        std::vector<std::vector<acacia::research::action_vec>> table;
        for (auto u : valuations (a, a.inputs)) {
          table.emplace_back ();
          for (auto c : valuations (a, a.outputs)) {
            acacia::research::action_vec action (n);
            for (size_t q = 0; q < n; ++q)
              for (const auto& e : eager.get (q).edges)
                if ((e.condition & u & c) != bddfalse)
                  action[coordinate (eager, e.destination)].emplace_back (coordinate (eager, q), e.increment);
            table.back ().push_back (std::move (action));
          }
        }
        const auto ref = acacia::research::solve_explicit_forward_game (
            acacia::research::initial_vector (n, coordinate (eager, eager.cache->initial_id ())),
            table, static_cast<VECTOR_ELT_T> (K), n);
        expect (ref.status != acacia::research::forward_status::resource_limit, "reference fully enumerates");
        reference_nodes += ref.env_nodes;
        const auto expected = ref.status == acacia::research::forward_status::win_k
                                  ? forward_result_status::win_k : forward_result_status::lose_k;
         {
          const auto lazy = Search {demand, a, K, {}}.solve ();
          const auto full = Search {eager, a, K, {}}.solve ();
          expect (lazy.status == expected && full.status == expected,
                  std::string ("closure finite-K lazy/eager/reference: ") + text);
          ++games;
          ++(expected == forward_result_status::win_k ? wins : losses);
          // Independently evaluate every valuation at every searched sparse
          // rank against the dense reference action, in structural coordinates.
          Reader reader {demand, false, {}};
          Oracle oracle {reader, demand, a, K};
          for (const auto& node : lazy.nodes) {
            acacia::research::rank_vector rank (n, -1);
            for (auto [q, v] : node.rank.entries ()) rank[coordinate (demand, q)] = v;
            size_t ui = 0;
            for (auto u : valuations (a, a.inputs)) {
              size_t ci = 0;
              for (auto c : valuations (a, a.outputs)) {
                const auto value = oracle.evaluate (node.rank, u & c);
                expect (value.value.has_value (), "complete lazy successor");
                acacia::research::rank_vector actual (n, -1);
                for (auto [q, v] : value.value->entries ()) actual[coordinate (demand, q)] = v;
                expect (actual == acacia::research::apply_forward (rank, table[ui][ci++], K),
                        "structural obligations/cursor successor matches dense reference");
                ++arithmetic;
              }
              ++ui;
            }
          }
        }
        demand.snapshot ();
        expect (observed.contains ("closure_branches_considered") &&
                observed.contains ("first_row_ms") && observed.contains ("first_useful_rank_query_ms") &&
                !observed.contains ("underlying_rows_requested"), "closure timing and counters, no TAA counters");
        const auto before = eager.cache->complete_rows ();
        const auto exported = export_graph (eager);
        expect (exported->num_states () == n && before == eager.cache->complete_rows (),
                "HOA export reuses C4 enumeration");
        for (size_t q = 0; q < n; ++q) {
          size_t i = 0;
          const auto& edges = eager.get (q).edges;
          for (const auto& edge : exported->out (q)) {
            expect (i < edges.size () && edge.dst == edges[i].destination &&
                    edge.cond == edges[i].condition && edge.acc.has (0) == edges[i].increment,
                    "export preserves every complete source edge, guard and increment");
            ++i;
          }
          expect (i == edges.size (), "export omits no source edges");
        }
      }
    }
  }
  std::cout << "closure: " << games << " finite-K lazy/eager/reference matches (" << wins << " win, "
            << losses << " loss), " << reference_nodes << " reference rank nodes, " << arithmetic
            << " structural valuation successors; 8 specs, both polarities, K=1..3, exact constant-output choices\n";
}

void closure_mutations_and_growth () {
  using namespace acacia::spot_lazy_worker;
  using acacia::closure_buchi::Provider;
  using acacia::closure_buchi::Hooks;
  using acacia::closure_buchi::FaultPoint;
  const auto dict = spot::make_bdd_dict ();
  auto make = [&] (const char* f, acacia::closure_buchi::Options options = {}) {
    return make_closure_store (spot::parse_infix_psl (f).f, dict, {}, {}, options);
  };
  unsigned mutations = 0;
  auto winning = make ("F(u xor c)");
  Options opt;
  opt.partition = "cu"; // lexical c,u
  auto a = alphabet (winning->provider, opt, {});
  const auto good = Search {*winning, a, 2}.solve ();
  expect (good.status == forward_result_status::win_k, "closure mutation winning fixture");
  auto reject = [&] (auto mutate) {
    auto bad = good;
    mutate (bad);
    expect (!verify_winning_certificate (*winning, a, 2, bad).value, "closure winning mutation rejected");
    ++mutations;
  };
  reject ([] (auto& bad) { bad.nodes[bad.initial].rank = Rank {{{0, 1}}, 2}; });
  reject ([] (auto& bad) { bad.nodes[bad.initial].choices.clear (); });
  reject ([] (auto& bad) { bad.nodes[bad.initial].choices[0].successor = bad.nodes.size (); });
  reject ([] (auto& bad) { bad.nodes[bad.nodes[bad.initial].choices[0].successor].losing = true; });

  for (bool missing : {false, true}) {
    auto store = make ("true");
    auto empty = alphabet (store->provider, {}, {});
    store->enumerate_and_freeze ();
    // Corrupt search's normalized edge data, leave original Spot marks intact
    // for fresh certificate reconstruction (the immutable provider is untouched).
    for (size_t q = 0; q < store->cache->state_count (); ++q) {
      auto& row = const_cast<rows::CompleteRankRow&> (store->get (q));
      if (missing) row.edges.clear ();
      else for (auto& edge : row.edges) edge.increment = false;
    }
    const auto failed = Search {*store, empty, 2}.solve ();
    expect (failed.status == forward_result_status::unknown,
            "missing source edge / incorrect increment cannot certify a false win");
    ++mutations;
  }
  auto losing = make ("true");
  auto empty = alphabet (losing->provider, {}, {});
  const auto loss = Search {*losing, empty, 2}.solve ();
  expect (loss.status == forward_result_status::lose_k, "closure losing fixture");
  for (int mutation : {0, 1}) {
    auto bad = loss;
    if (mutation == 0) bad.proofs.at (*bad.initial_proof).record.dependencies.push_back (*bad.initial_proof);
    else {
      bool changed = false;
      for (auto& proof : bad.proofs) if (!proof.rows.empty ()) {
        proof.rows[0].source = losing->cache->state_count () + 17;
        changed = true;
        break;
      }
      expect (changed, "row correspondence mutation exercised");
    }
    expect (!verify_losing_proof (*losing, empty, 2, bad).value,
            "closure chronology / row correspondence mutation rejected");
    ++mutations;
  }

  auto hooks = std::make_shared<Hooks> ();
  acacia::closure_buchi::Options limits;
  limits.hooks = hooks;
  auto failed = make ("X X true", limits);
  auto p = std::dynamic_pointer_cast<const Provider> (failed->provider);
  hooks->fail = [p] (FaultPoint point) { return point == FaultPoint::publication && p->complete_rows () == 1; };
  const auto result = Search {*failed, alphabet (failed->provider, {}, {}), 2}.solve ();
  expect (result.status == forward_result_status::unknown && p->complete_rows () == 1 &&
          failed->cache->complete_rows () == 1 &&
          failure_reason (failed->row_error, "missing") == "closure-buchi:injected",
          "mid-search failure after complete first row remains typed UNKNOWN");
  hooks->fail = {}; // release test hook's provider reference

  auto growing = make_closure_store (spot::formula::X (80, spot::formula::ap ("p")), dict, {});
  const Rank saved {{{0, 0}}, 3}, copy = saved, upper {{{0, 1}}, 3};
  const auto hash = saved.hash (), bytes = rank_bytes (saved), capacity = saved.entries ().capacity ();
  const auto mass = saved.mass ();
  const auto* data = saved.entries ().data ();
  growing->enumerate_and_freeze ();
  expect (growing->cache->state_count () > 80 && saved == copy && saved.hash () == hash &&
          saved.mass () == mass && saved.leq (upper) && !upper.leq (saved) &&
          saved.entries ().capacity () == capacity && saved.entries ().data () == data &&
          rank_bytes (saved) == bytes && saved.at (80) == -1,
          "unrelated state growth preserves equality/hash/mass/dominance/storage, no dense tail");
  std::cout << "closure: " << mutations << " certificate mutations rejected; injected mid-search UNKNOWN; "
            << growing->cache->state_count () << " states preserve saved sparse rank\n";
}

// A1/A2 CLI surface: --closure-row-expansion and --stop-after-first-row.
Options parse (std::vector<std::string> args) {
  args.insert (args.begin (), "acacia-spot-provider-replay");
  std::vector<char*> argv;
  for (auto& a : args) argv.push_back (a.data ());
  return parse_options (int (argv.size ()), argv.data ());
}
bool rejected (std::vector<std::string> args) {
  try {
    parse (std::move (args));
  } catch (const std::exception&) {
    return true;
  }
  return false;
}
void cli_flags () {
  const std::vector<std::string> base {"--arm", "c5", "--formula", "p", "--k", "1",
                                       "--provider", "closure-buchi"};
  auto with = [&] (std::vector<std::string> extra) {
    auto args = base;
    args.insert (args.end (), extra.begin (), extra.end ());
    return args;
  };
  expect (parse (base).row_expansion == acacia::closure_buchi::RowExpansion::symbolic_boolean,
          "default row expansion is symbolic-boolean");
  expect (!parse (base).stop_after_first_row, "default is not stop-after-first-row");
  expect (parse (with ({"--closure-row-expansion", "enumerative"})).row_expansion ==
              acacia::closure_buchi::RowExpansion::enumerative,
          "--closure-row-expansion enumerative parses");
  expect (parse (with ({"--closure-row-expansion", "symbolic-boolean"})).row_expansion ==
              acacia::closure_buchi::RowExpansion::symbolic_boolean,
          "--closure-row-expansion symbolic-boolean parses");
  expect (parse (with ({"--stop-after-first-row"})).stop_after_first_row,
          "--stop-after-first-row parses");
  expect (rejected (with ({"--closure-row-expansion", "bogus"})),
          "an unknown --closure-row-expansion value is rejected");
  expect (rejected (with ({"--closure-row-expansion", "enumerative",
                          "--closure-row-expansion", "enumerative"})),
          "a duplicate --closure-row-expansion is rejected, like every other option");
  expect (rejected ({"--arm", "c5", "--formula", "p", "--k", "1", "--provider", "taa",
                     "--closure-row-expansion", "enumerative"}),
          "--closure-row-expansion is rejected for --provider taa");
  {
    const char* temp_dir = std::getenv ("TMPDIR");
    const std::string path_template = std::string (temp_dir && *temp_dir ? temp_dir : ".") +
                                      "/replay-formula-file-test-XXXXXX";
    std::vector<char> path (path_template.begin (), path_template.end ());
    path.push_back ('\0');
    const int fd = mkstemp (path.data ());
    expect (fd >= 0, "temp file for --formula-file");
    const std::string text = "p & X q\n";
    expect (write (fd, text.data (), text.size ()) == ssize_t (text.size ()),
            "wrote formula file");
    close (fd);
    expect (parse ({"--arm", "c5", "--formula-file", path.data (), "--k", "1"}).formula == "p & X q",
            "--formula-file reads the file and strips the trailing newline");
    expect (rejected ({"--arm", "c5", "--formula", "p", "--formula-file", path.data (), "--k", "1"}),
            "--formula and --formula-file are mutually exclusive");
    expect (rejected ({"--arm", "c5", "--formula-file", "/nonexistent/path", "--k", "1"}),
            "an unreadable --formula-file is rejected");
    unlink (path.data ());
  }
  std::cout << "cli_flags: PASS\n";
}

int main (int argc, char** argv) {
  if (argc == 3 && std::string (argv[1]) == "--oracle-layout") {
    const auto layout = acacia::parse_oracle_layout (argv[2]);
    if (!layout)
      return 1;
    acacia::selected_oracle_layout = *layout;
  }
  else if (argc != 1)
    return 1;
  const int report_fd = open ("/dev/null", O_WRONLY);
  const auto report = pipe_reporter (report_fd);
  try {
     {
      if (const int code = differential (report))
        return code;
    }
    cli_flags ();
    closure_games ();
    closure_mutations_and_growth ();
    copy_kernels ();
    verifier_phase_metrics ();
    certificate_shape_metrics ();
    incremental_coverage_events ();
    incremental_coverage_property ();
    incremental_coverage_failure ();
    missing_output_kernel ();
    corrupt_certificates ();
    empty_alphabets_and_failures ();
  } catch (const std::exception& e) {
    std::cerr << e.what () << '\n';
    return 17;
  }
  std::cout << "S2 last-input, fallback, proof chronology and quantification-budget checks passed\n"
               "quantifier-order, certificate corruption, empty-alphabet and query-failure checks passed\n";
  close (report_fd);
}
