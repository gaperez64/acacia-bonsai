// Unmeasured correctness reference only. This is not a C4/C5 timing arm.
// Compile the research specialization in isolation without its CLI entry point.
#define ACACIA_PROVIDER_REPLAY_TESTING
#include "research/spot_provider_replay.cc"

#include "research/explicit_forward_game.hh"

#include <fcntl.h>
using namespace replay;
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
int main () {
  const int report_fd = open ("/dev/null", O_WRONLY);
  const auto report = pipe_reporter (report_fd);
  unsigned checks = 0, corruptions = 0, arithmetic = 0;
  unsigned redundant_loss_events = 0;
  std::size_t losing_events = 0, antichain_insertions = 0;
  std::size_t broad_scans = 0, nodes_scanned = 0;
  std::size_t subsumption_queries = 0, subsumption_hits = 0;
  std::size_t expansions_total = 0, choices_total = 0, nodes_total = 0;
  std::size_t reopened_total = 0, invalidated_total = 0, removals_total = 0;
  std::size_t reopen_enqueues_total = 0, prefilter_skips_total = 0;
  for (const char* f :
       {"true", "false", "F a", "G a", "GF a", "GF a & GF b", "G(a -> F b)", "(a U b) | G c"}) {
    const auto dict = spot::make_bdd_dict ();
    auto p = spot::ltl_to_taa (spot::parse_infix_psl (f).f, dict, false);
    auto w = std::make_shared<lazy::LazyBuchiView> (p);
    RowStore store {w, {}, report};
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
        Search search {store, a, K};
        const auto actual = search.solve ();
        const bool win = ref.status == acacia::research::forward_status::win_k;
        if (actual.status != (win ? solver_detail::forward_result_status::win_k
                                  : solver_detail::forward_result_status::lose_k)) {
          std::cerr << "MISMATCH " << f << ' ' << part << ' ' << K << '\n';
          return 1;
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

        auto corrupt = actual;
        if (win) {
          corrupt.nodes[corrupt.initial].choices.clear ();
          if (verify_winning_certificate (store, a, K, corrupt).value)
            return 4;
        }
        else {
          corrupt.proofs.at (*corrupt.initial_proof)
              .record.dependencies.push_back (*corrupt.initial_proof);
          if (verify_losing_proof (store, a, K, corrupt).value)
            return 5;
        }
        ++checks;
        ++corruptions;
      }
    }
  }
  std::cout << checks << " explicit-game matches, " << arithmetic << " sparse arithmetic matches, "
            << corruptions << " corrupt certificates rejected\n";
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
  std::cout << "reopen enqueues: " << reopen_enqueues_total << "\n";
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
      node.choices.push_back ({bddtrue, bddtrue, 1, true});
    }
    store.phase = Phase::verify;
    const letters::WorkerAlphabet a {bddtrue, bddtrue, bddtrue, {}};
    if (verify_winning_certificate (store, a, 1, forged).value)
      return 6;
    if (store.search_sources != std::set<StateId> {0} ||
        store.verifier_sources != std::set<StateId> {0, 1} || store.verify_generated != 1)
      return 7;
  }
  close (report_fd);
}
