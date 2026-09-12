// Unmeasured correctness reference only. This is not a C4/C5 timing arm.
// Compile the research specialization in isolation without its CLI entry point.
#define ACACIA_PROVIDER_REPLAY_TESTING
#include "research/spot_provider_replay.cc"

#include "research/explicit_forward_game.hh"

#include <fcntl.h>
#include <type_traits>
using namespace replay;
static_assert (not std::is_aggregate_v<SparseChoice>);

constexpr ChoiceSemantics all_semantics[] {
    {SuccessorRelation::exact, OutputChoice::constant},
    {SuccessorRelation::exact, OutputChoice::existential},
    {SuccessorRelation::downward, OutputChoice::constant},
    {SuccessorRelation::downward, OutputChoice::existential}};

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
// does not change the promise originally made by a region. Only this tiny test
// oracle enumerates outputs to check existential choices.
int choice_regions (RowStore& store, const letters::WorkerAlphabet& a, int K,
                    const SolveResult& actual, std::size_t& region_inputs) {
  for (const auto& node : actual.nodes)
    for (const auto& choice : node.choices) {
      const bool constant = actual.semantics.output_choice == OutputChoice::constant;
      if (choice.constant_output.has_value () != constant)
        return 6;
      const auto outputs = constant ? std::vector<bdd> {*choice.constant_output}
                                    : valuations (a, a.outputs);
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
        if (actual.semantics.successor_relation == SuccessorRelation::exact && not equal)
          return 7;
        ++region_inputs;
      }
    }
  return 0;
}

int differential (const Reporter& report, ChoiceSemantics semantics) {
  unsigned checks = 0, corruptions = 0, arithmetic = 0;
  unsigned redundant_loss_events = 0;
  std::size_t losing_events = 0, antichain_insertions = 0;
  std::size_t broad_scans = 0, nodes_scanned = 0;
  std::size_t subsumption_queries = 0, subsumption_hits = 0;
  std::size_t expansions_total = 0, choices_total = 0, nodes_total = 0;
  std::size_t reopened_total = 0, invalidated_total = 0, removals_total = 0;
  std::size_t reopen_enqueues_total = 0, prefilter_skips_total = 0;
  std::size_t region_inputs = 0;
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
        // Exercise the actual omitted-option default in each build as well as
        // all four explicit combinations across this sweep.
        const auto actual = semantics == default_choice_semantics
                                ? Search {store, a, K}.solve ()
                                : Search {store, a, K, {}, semantics}.solve ();
        if (actual.semantics != semantics)
          return 2;
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

        if (const int code = choice_regions (store, a, K, actual, region_inputs))
          return code;

        auto corrupt = actual;
        if (win) {
          corrupt.nodes[corrupt.initial].choices.clear ();
          if (verify_winning_certificate (store, a, K, corrupt, {}, semantics).value)
            return 4;
        }
        else {
          corrupt.proofs.at (*corrupt.initial_proof)
              .record.dependencies.push_back (*corrupt.initial_proof);
          if (verify_losing_proof (store, a, K, corrupt, {}, semantics).value)
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
    forged.semantics = semantics;
    forged.nodes.push_back ({Rank {{{0, 0}}, 1}, true});
    forged.nodes.push_back ({Rank {{{1, 0}}, 1}, true});
    for (auto& node : forged.nodes) {
      node.covered_inputs = bddtrue;
      node.choices.push_back (semantics.output_choice == OutputChoice::constant
                                 ? SparseChoice::constant (bddtrue, bddtrue, 1)
                                 : SparseChoice::existential (bddtrue, 1));
    }
    store.phase = Phase::verify;
    const letters::WorkerAlphabet a {bddtrue, bddtrue, bddtrue, {}};
    if (verify_winning_certificate (store, a, 1, forged, {}, semantics).value)
      return 6;
    if (store.search_sources != std::set<StateId> {0} ||
        store.verifier_sources != std::set<StateId> {0, 1} || store.verify_generated != 1)
      return 7;
  }
  return 0;
}

void expect (bool condition, const std::string& name) {
  if (not condition)
    throw std::runtime_error ("FAIL: " + name);
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
    for (auto semantics : all_semantics) {
      const auto result = Search {store, k.alphabet, 1, {}, semantics}.solve ();
      const auto& node = result.nodes.at (result.initial);
      const std::size_t expected = semantics.output_choice == OutputChoice::constant ? 1u << n : 1;
      expect (result.status == forward_result_status::win_k && node.rank == safe &&
              node.covered_inputs == bddtrue && node.choices.size () == expected &&
              result.choices_created == expected, "copy kernel region count");
      for (const auto& choice : node.choices)
        expect (choice.active && choice.successor == result.initial, "copy kernel self-loop");
      check_regions (store, k.alphabet, result);
      if (semantics.successor_relation == SuccessorRelation::exact)
        std::cout << "copy n=" << n << ' '
                  << (semantics.output_choice == OutputChoice::constant ? "constant" : "existential")
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
  for (auto semantics : all_semantics) {
    const auto result = Search {store, k.alphabet, 1, {}, semantics}.solve ();
    expect (result.status == forward_result_status::lose_k, "input with no legal output loses");
    const auto& node = result.nodes.at (result.initial);
    expect (node.covered_inputs != bddtrue && node.choices.size () == 1 &&
            node.choices[0].input_region == !u, "missing legal output prevents full coverage");
    check_regions (store, k.alphabet, result);
    auto corrupt = result;
    corrupt.semantics.successor_relation = semantics.successor_relation == SuccessorRelation::exact
                                             ? SuccessorRelation::downward : SuccessorRelation::exact;
    expect (not verify_losing_proof (store, k.alphabet, 1, corrupt, {}, semantics).value,
            "losing certificate also validates the requested tag");
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
  for (auto semantics : all_semantics) {
    const auto good = Search {store, k.alphabet, 1, {}, semantics}.solve ();
    expect (good.status == forward_result_status::win_k &&
            good.nodes.at (good.initial).choices.size () == 2, "split-target fixture wins");
    expect (verify_winning_certificate (store, k.alphabet, 1, good, {}, semantics).value.has_value (),
            "uncorrupted split-target certificate replays");
    check_regions (store, k.alphabet, good);
    auto reject = [&] (const std::string& name, auto mutate) {
      auto bad = good;
      mutate (bad, bad.nodes.at (bad.initial));
      const auto checked = verify_winning_certificate (store, k.alphabet, 1, bad, {}, semantics);
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
    reject ("forged successor-relation tag rejected", [&] (auto& bad, auto&) {
      bad.semantics.successor_relation = semantics.successor_relation == SuccessorRelation::exact
                                            ? SuccessorRelation::downward : SuccessorRelation::exact;
    });
    reject ("forged output-choice tag rejected", [&] (auto& bad, auto&) {
      bad.semantics.output_choice = semantics.output_choice == OutputChoice::constant
                                       ? OutputChoice::existential : OutputChoice::constant;
    });
    if (semantics.output_choice == OutputChoice::existential) {
      reject ("existential choice must not contain an invalid constant cube", [] (auto&, auto& node) {
        node.choices[0].constant_output = bddtrue; // Real outputs exist: this is not total.
      });
      reject ("existential choice must not contain even a total constant cube", [&] (auto&, auto& node) {
        node.choices[0].constant_output = !c;
      });
    }
    else {
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
    for (auto invalid : {ChoiceSemantics {static_cast<SuccessorRelation> (99), semantics.output_choice},
                         ChoiceSemantics {semantics.successor_relation, static_cast<OutputChoice> (99)}}) {
      auto bad = good;
      bad.semantics = invalid;
      // Matching malformed tags are still invalid, even when supplied by the caller.
      const auto checked = verify_winning_certificate (store, k.alphabet, 1, bad, {}, invalid);
      expect (not checked.value && checked.unknown == Unknown::invalid_query,
              "malformed semantics rejected even when matching the request");
      const auto searched = Search {store, k.alphabet, 1, {}, invalid}.solve ();
      expect (searched.status == forward_result_status::unknown &&
              searched.failure == Unknown::invalid_query, "invalid search option is inconclusive");
    }
  }
}

void successor_relations () {
  Kernel k {3};
  const bdd u = k.ap ("u", true);
  k.ap ("c", false);
  k.graph->new_edge (0, 1, bddtrue);
  k.graph->new_edge (0, 2, u);
  k.graph->new_edge (1, 1, bddtrue);
  k.graph->new_edge (2, 2, bddtrue);
  RowStore store {k.view (), {}, {}};
  store.enumerate_and_freeze ();
  for (auto semantics : all_semantics) {
    auto result = Search {store, k.alphabet, 1, {}, semantics}.solve ();
    expect (result.status == forward_result_status::win_k, "successor-relation fixture wins");
    auto& node = result.nodes.at (result.initial);
    expect (node.choices.size () == 2, "successor-relation fixture has two sampled targets");
    auto upper = node.choices.back ();
    expect (result.nodes[upper.successor].rank == Rank {{{1, 0}, {2, 0}}, 1},
            "upper target has both coordinates");
    upper.input_region = bddtrue;
    node.choices = {upper};
    const auto checked = verify_winning_certificate (store, k.alphabet, 1, result, {}, semantics);
    const bool downward = semantics.successor_relation == SuccessorRelation::downward;
    expect (checked.value.has_value () == downward, "verifier respects independent successor relation");
    std::size_t count = 0;
    expect (choice_regions (store, k.alphabet, 1, result, count) == (downward ? 0 : 7),
            "arithmetic distinguishes equality from strict downward reachability");
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
    for (auto semantics : all_semantics) {
      const auto result = Search {store, k.alphabet, 1, {}, semantics}.solve ();
      expect (result.status == forward_result_status::win_k && result.choices_created == 1,
              "empty input/output alphabets retain their single empty valuation");
      check_regions (store, k.alphabet, result);
      Limits limits;
      limits.verifier_queries.max_steps = 0;
      const auto checked = verify_winning_certificate (store, k.alphabet, 1, result, limits, semantics);
      expect (not checked.value && checked.unknown == Unknown::resource_limit,
              "verifier budget failure has no Boolean result");
      const auto failed = Search {store, k.alphabet, 1, limits, semantics}.solve ();
      expect (failed.status == forward_result_status::unknown && failed.failure == Unknown::resource_limit,
              "search whose verification runs out of budget stays UNKNOWN");
      limits = {};
      limits.queries.aborted = [] (void*) { return true; };
      const auto aborted = Search {store, k.alphabet, 1, limits, semantics}.solve ();
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

int main () {
  const int report_fd = open ("/dev/null", O_WRONLY);
  const auto report = pipe_reporter (report_fd);
  for (auto semantics : all_semantics) {
    std::cout << (semantics.successor_relation == SuccessorRelation::exact ? "exact" : "downward")
              << '+' << (semantics.output_choice == OutputChoice::constant ? "constant" : "existential")
              << (semantics == default_choice_semantics ? " (default)" : "") << '\n';
    if (const int code = differential (report, semantics))
      return code;
  }
  try {
    copy_kernels ();
    missing_output_kernel ();
    corrupt_certificates ();
    successor_relations ();
    empty_alphabets_and_failures ();
  } catch (const std::exception& e) {
    std::cerr << e.what () << '\n';
    return 17;
  }
  std::cout << "quantifier-order, certificate corruption, empty-alphabet and query-failure checks passed\n";
  close (report_fd);
}
