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

void expect (bool condition, const std::string& name) {
  if (not condition)
    throw std::runtime_error ("FAIL: " + name);
}

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

bdd check_losing_inputs (RowStore& store, const letters::WorkerAlphabet& a, int K,
                        const Rank& rank, const std::vector<Rank>& earlier,
                        std::size_t& input_checks) {
  Reader reader {store, false, {}};
  Oracle oracle {reader, store, a, K};
  const auto projected = oracle.bad (rank, earlier, earlier.size (), bddtrue);
  expect (projected.value.has_value (), "checked universal projection succeeds");
  const auto [bad, H] = *projected.value;
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
  const bdd missing = inputs.front ();
  const auto masked = oracle.bad (rank, earlier, earlier.size (), missing);
  expect (masked.value && masked.value->first == bad && masked.value->second == (missing & H),
          "D excludes covered inputs and shares the same Bad predicate");
  return H;
}

void check_losing_certificate (RowStore& store, const letters::WorkerAlphabet& a, int K,
                               const SolveResult& result) {
  const auto checked = verify_losing_proof (store, a, K, result, {}, result.semantics);
  expect (checked.value && *checked.value, "symbolic losing proof independently replays");
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
  std::size_t losing_input_checks = 0, symbolic_region_inputs = 0;
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
        const auto symbolic = Search {store, a, K, {}, semantics, LosingInputSearch::on}.solve ();
        expect (symbolic.status == actual.status && symbolic.failure == Unknown::none,
                "S2 on/off fixed-K outcomes agree for every automaton, partition and mode");
        expect (choice_regions (store, a, K, symbolic, symbolic_region_inputs) == 0,
                "symbolic search preserves choice-region arithmetic");
        for (const auto& node : symbolic.nodes)
          check_losing_inputs (store, a, K, node.rank, {}, losing_input_checks);
        if (not win) {
          check_losing_certificate (store, a, K, symbolic);
          LossSet earlier;
          for (const auto& proof : symbolic.proofs) {
            check_losing_inputs (store, a, K, proof.rank, earlier.ranks (), losing_input_checks);
            earlier.insert (proof.rank, proof.record.id);
          }
          auto invalid = symbolic;
          invalid.proofs.at (*invalid.initial_proof)
              .record.dependencies.push_back (*invalid.initial_proof);
          expect (not verify_losing_proof (store, a, K, invalid, {}, semantics).value,
                  "symbolic proof with a nonchronological dependency is rejected");
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
  std::cout << checks << " S2 on/off matches, " << losing_input_checks
            << " universally losing inputs checked by enumeration, " << symbolic_region_inputs
            << " S2 choice-region inputs checked\n";
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

void last_losing_input () {
  Kernel k {2};
  const bdd u = k.ap ("u", true), c = k.ap ("c", false);
  const bdd v = k.ap ("v", true), d = k.ap ("d", false);
  const bdd last = u & v;
  const bdd legal = !last & bdd_biimp (u, c) & bdd_biimp (v, d);
  k.graph->new_edge (0, 0, legal);
  k.graph->new_edge (0, 1, !legal, {0});
  k.graph->new_edge (1, 1, bddtrue, {0});
  RowStore store {k.view (), {}, {}};
  store.enumerate_and_freeze ();
  Reader reader {store, false, {}};
  Oracle oracle {reader, store, k.alphabet, 1};
  std::size_t input_checks = 0;
  expect (check_losing_inputs (store, k.alphabet, 1, reader.initial_rank (), {}, input_checks) == last,
          "the only losing input is the final total valuation");
  bdd remaining = bddtrue;
  const auto inputs = valuations (k.alphabet, k.alphabet.inputs);
  for (auto u : inputs) {
    const auto picked = oracle.model (remaining, Variables::inputs);
    expect (picked.value && *picked.value && **picked.value == u,
            "test enumeration agrees with deterministic model ordering");
    remaining &= !u;
  }
  expect (inputs.back () == last && remaining == bddfalse, "losing input is last in model order");
  for (auto semantics : all_semantics) {
    const auto sampled = Search {store, k.alphabet, 1, {}, semantics}.solve ();
    const auto symbolic = Search {store, k.alphabet, 1, {}, semantics, LosingInputSearch::on}.solve ();
    expect (sampled.status == forward_result_status::lose_k && symbolic.status == sampled.status,
            "last-input kernel loses in both searches");
    expect (symbolic.expansions == 1 && symbolic.choices_created == 0 &&
            symbolic.expansions < sampled.expansions && sampled.choices_created > 0,
            "symbolic search finds the final losing input before any arbitrary selection");
    const auto& proof = symbolic.proofs.at (*symbolic.initial_proof);
    expect (proof.record.reason == losing_reason::env_losing_input && proof.input == last &&
            proof.record.dependencies.empty (), "immediate symbolic witness uses the existing rule");
    check_losing_certificate (store, k.alphabet, 1, symbolic);
    std::cout << "last losing input "
              << (semantics.output_choice == OutputChoice::constant ? "constant" : "existential")
              << ": S2 off/on " << sampled.expansions << '/' << symbolic.expansions
              << " expansions, " << sampled.choices_created << '/' << symbolic.choices_created
              << " choices\n";
  }
}

void later_losing_input () {
  Kernel k {3};
  k.ap ("u", true);
  k.ap ("c", false);
  k.ap ("unused_input", true);
  k.graph->new_edge (0, 1, bddtrue);
  k.graph->new_edge (1, 2, bddtrue, {0});
  k.graph->new_edge (2, 2, bddtrue, {0});
  RowStore store {k.view (), {}, {}};
  store.enumerate_and_freeze ();
  Reader reader {store, false, {}};
  std::size_t input_checks = 0;
  expect (check_losing_inputs (store, k.alphabet, 1, reader.initial_rank (), {}, input_checks)
              == bddfalse, "delayed-loss root has no immediate losing input");
  for (auto semantics : all_semantics) {
    const auto sampled = Search {store, k.alphabet, 1, {}, semantics}.solve ();
    const auto symbolic = Search {store, k.alphabet, 1, {}, semantics, LosingInputSearch::on}.solve ();
    expect (symbolic.status == forward_result_status::lose_k && symbolic.status == sampled.status,
            "empty D falls back and still discovers the later loss");
    const auto& root = symbolic.nodes.at (symbolic.initial);
    expect (symbolic.choices_created == 1 && root.choices.size () == 1 &&
            root.choices[0].input_region == bddtrue && not root.choices[0].active &&
            symbolic.reopened_sources > 0, "fallback creates a choice that is invalidated and reopened");
    expect (symbolic.expansions == sampled.expansions &&
            symbolic.choices_created == sampled.choices_created &&
            symbolic.reopened_sources == sampled.reopened_sources,
            "fallback retains the delayed-loss search behavior");
    const auto& proof = symbolic.proofs.at (*symbolic.initial_proof);
    expect (not proof.record.dependencies.empty (), "later root loss needs an earlier proof");
    std::vector<Rank> earlier;
    for (auto dep : proof.record.dependencies)
      earlier.push_back (symbolic.proofs.at (dep).rank);
    expect (check_losing_inputs (store, k.alphabet, 1, root.rank, earlier, input_checks) == bddtrue,
            "known losing successors make every root input losing");
    check_losing_certificate (store, k.alphabet, 1, symbolic);
    // H=true omits both inputs, but the witness must supply both of them.
    expect (proof.input == valuations (k.alphabet, k.alphabet.inputs).front (),
            "symbolic proof fills even inputs absent from H");
    auto corrupt = symbolic;
    corrupt.proofs.at (*corrupt.initial_proof).input = bddtrue;
    const auto rejected = verify_losing_proof (store, k.alphabet, 1, corrupt, {}, semantics);
    expect (not rejected.value && rejected.unknown == Unknown::invalid_query,
            "verifier rejects storing a symbolic region instead of a total input cube");
  }
}

void losing_input_query_failure () {
  // Enough row work that the Bad query budget exceeds all expansion setup
  // queries. Its projection is false: a failed projection must not return it.
  Kernel k {8};
  k.ap ("u", true);
  k.ap ("c", false);
  for (unsigned q = 1; q < 8; ++q) {
    k.graph->new_edge (0, q, bddtrue);
    k.graph->new_edge (q, q, bddtrue);
  }
  RowStore store {k.view (), {}, {}};
  store.enumerate_and_freeze ();
  Reader reader {store, false, {}};
  const Rank rank = reader.initial_rank ();
  std::size_t checkpoints = 0;
  letters::QueryLimits counting;
  counting.aborted = [] (void* data) { ++*static_cast<std::size_t*> (data); return false; };
  counting.abort_data = &checkpoints;
  Oracle measure {reader, store, k.alphabet, 1};
  measure.set_limits (counting);
  expect (measure.query<bool> ([] (auto&) { return true; }).value.has_value (),
          "validate alphabet before measuring Bad");
  checkpoints = 0;
  expect (measure.bad (rank, {}, 0).value.has_value (), "measure the complete Bad query");
  const auto bad_steps = checkpoints;
  auto operations = [] (Oracle& oracle) {
    std::size_t result = 0;
    oracle.report (Reporter {[&] (const auto& key, const auto& value) {
      if (key == "bdd_operations") result = std::stoull (value);
    }}, "");
    return result;
  };
  for (bool limited : {false, true}) {
    Oracle oracle {reader, store, k.alphabet, 1};
    expect (oracle.query<bool> ([] (auto&) { return true; }).value.has_value (),
            "validate fresh alphabet before the budgeted query");
    const auto before = operations (oracle);
    letters::QueryLimits limits;
    if (limited) limits.max_steps = bad_steps;
    oracle.set_limits (limits);
    const auto result = oracle.bad (rank, {}, 0, bddtrue);
    if (limited) {
      expect (not result.value && result.unknown == Unknown::resource_limit,
              "budget failure during forall is UNKNOWN, not a present false projection");
      // The old query's final step pays for entry to forall; its post-operation
      // checkpoint exhausts this budget, before the land forming D can run.
      expect (operations (oracle) == operations (measure) + 1,
              "budget fails after the new forall operation and before conjunction");
      oracle.set_limits ({});
      const auto recovered = oracle.bad (rank, {}, 0, bddtrue);
      expect (recovered.value && recovered.value->second == bddfalse,
              "a fresh query after quantification failure can return a present false");
    }
    else {
      expect (result.value && result.value->second == bddfalse && result.unknown == Unknown::none,
              "unsatisfiable D is a present result");
      expect (operations (oracle) > before, "projection uses the checked BDD boundary");
    }
  }
  for (auto semantics : all_semantics) {
    Limits limits;
    limits.queries.max_steps = bad_steps;
    const auto failed = Search {store, k.alphabet, 1, limits, semantics, LosingInputSearch::on}.solve ();
    // Search retains its existing resource-limit subtype of an inconclusive
    // result; neither WIN_K nor LOSE_K is published.
    expect (failed.status == forward_result_status::resource_limit &&
            failed.failure == Unknown::resource_limit && failed.expansions == 1 &&
            failed.nodes.at (failed.initial).active_rows_complete &&
            failed.choices_created == 0 && failed.proofs.empty (),
            "search quantification budget exhaustion remains inconclusive");
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
  try {
    for (auto semantics : all_semantics) {
      std::cout << (semantics.successor_relation == SuccessorRelation::exact ? "exact" : "downward")
                << '+' << (semantics.output_choice == OutputChoice::constant ? "constant" : "existential")
                << (semantics == default_choice_semantics ? " (default)" : "") << '\n';
      if (const int code = differential (report, semantics))
        return code;
    }
    copy_kernels ();
    missing_output_kernel ();
    last_losing_input ();
    later_losing_input ();
    losing_input_query_failure ();
    corrupt_certificates ();
    successor_relations ();
    empty_alphabets_and_failures ();
  } catch (const std::exception& e) {
    std::cerr << e.what () << '\n';
    return 17;
  }
  std::cout << "S2 last-input, fallback, proof chronology and quantification-budget checks passed\n"
               "quantifier-order, certificate corruption, empty-alphabet and query-failure checks passed\n";
  close (report_fd);
}
