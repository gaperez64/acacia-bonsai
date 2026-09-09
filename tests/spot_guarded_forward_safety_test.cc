#include "solver/spot_guarded_forward_safety.hh"
#include "solver/spot_lazy_game.hh"
#include "solver/forward_reachable_safety.hh"
#include "solver/k_bounded_safety_aut.hh"
#include "actioners/standard.hh"
#include "input_pickers/critical.hh"
#include "research/explicit_forward_game.hh"
#include "tiny_spot_game.hh"
#include "utils/verbose.hh"
#include <posets/downsets/vector_backed.hh>
#include <posets/vectors.hh>
#include <iostream>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}
namespace posets::vectors {
  size_t bool_threshold = 0;
}

namespace {
  namespace guarded = acacia::spot_guarded;
  namespace reference = acacia::research;
  namespace games = acacia::testing::spot_games;
  using namespace acacia::spot_rows;
  using namespace acacia::spot_letters;
  using Status = acacia::solver_detail::forward_result_status;
  // Dense numeric storage retains even malformed Boolean-tail overflows from
  // the P3 generator; the safe envelope still has the exact mixed-domain caps.
  using State = posets::vectors::vector_backed<VECTOR_ELT_T>;
  using Downset = posets::downsets::vector_backed<State>;
  using Table = std::list<std::pair<bdd, std::list<reference::action_vec>>>;
  int failures = 0;
  unsigned agreeing_games = 0, wins = 0, losses = 0, unknowns = 0;
  unsigned replayed = 0, exhaustive_choices = 0, table_certificates = 0;

  bool expect (const std::string& name, bool condition) {
    if (condition) return true;
    if (failures < 30) std::cerr << "FAIL: " << name << '\n';
    ++failures;
    return false;
  }
  State state (const Rank& rank) {
    reference::rank_vector vector (rank.size (), -1);
    for (std::size_t p = 0; p < rank.size (); ++p) vector[p] = rank[p];
    return State {std::move (vector)};
  }
  Table action_table (SpotRows& rows, const WorkerAlphabet& ap) {
    Table actions;
    for (const auto& u : games::letters (ap, Variables::inputs)) {
      actions.emplace_back (u, std::list<reference::action_vec> {});
      for (const auto& c : games::letters (ap, Variables::outputs)) {
        reference::action_vec action (rows.state_count ());
        for (StateId p = 0; p < rows.state_count (); ++p) {
          const auto row = rows.row (p);
          if (not row.row) throw std::runtime_error ("reference row failed");
          for (const auto& edge : row.row->edges)
            if ((edge.condition & u & c) != bddfalse)
              action[edge.destination].emplace_back (p, edge.increment);
        }
        actions.back ().second.push_back (std::move (action));
      }
    }
    return actions;
  }
  // Feed the SAME explicit total-letter table into the current forward and
  // backward searches. Arithmetic remains actioners::standard's; no third
  // game solver or transition evaluator is introduced by this test adapter.
  struct TableFactory {
      const Table& table;
      auto make (const spot::twa_graph_ptr& aut, int, VECTOR_ELT_T K) const {
        std::list<std::pair<bdd, std::list<std::vector<std::pair<unsigned, unsigned>>>>> empty;
        auto actioner = actioners::standard<State>::make (aut, empty, K);
        actioner.actions () = table;
        return actioner;
      }
  };
  struct NoPrecomputation {
      auto make (const spot::twa_graph_ptr&, bdd, bdd) const { return [] { return 0; }; }
  };

  void random_games () {
    std::mt19937 rng {games::seed};
    for (unsigned game = 0; game < 5000; ++game) {
      const auto fixture = games::random_game (rng, game, true);
      const auto& g = fixture.graph;
      const auto& ap = fixture.alphabet;
      const auto K = static_cast<VECTOR_ELT_T> (fixture.K);
      const FrozenAcacia view {g, fixture.bool_threshold};
      const auto actual = guarded::solve (view, ap, K);
      acacia::spot_lazy_game::RowStore sparse_store {view, {}};
      acacia::spot_lazy_game::Search sparse_search {sparse_store, ap, K};
      const auto sparse = sparse_search.solve ();
      expect ("C3/C3s fixed-K agreement on identical frozen mixed domain", sparse.status == actual.status);
      if (sparse.status == Status::win_k) {
        auto corrupt = sparse;
        corrupt.nodes.at (corrupt.initial).choices.clear ();
        expect ("sparse verifier rejects uncovered inputs",
                !acacia::spot_lazy_game::verify_winning_certificate (sparse_store, ap, K, corrupt).value);
      }

      if (actual.status != Status::win_k && actual.status != Status::lose_k) {
        ++unknowns;
        expect ("random completed " + std::to_string (game), false);
        continue;
      }
      SpotRows rows {view};
      const auto table = action_table (rows, ap);
      const auto initial = rows.initial_rank ();
      const auto safe = rows.safe_caps (K);
      const auto explicit_result = reference::solve_explicit_forward_game (
          reference::initial_vector (rows.state_count (), rows.initial_id ()), table, K, fixture.bool_threshold);
      posets::vectors::bool_threshold = fixture.bool_threshold;
      const TableFactory factory {table};
      auto actioner = factory.make (g, 0, K);
      const auto forward = acacia::solver_detail::solve_forward_reachable_safety<Downset> (
          state (initial), state (safe), table, actioner);
      const NoPrecomputation precomputer;
      const input_pickers::critical picker;
      k_bounded_safety_aut_detail<Downset, NoPrecomputation, TableFactory, input_pickers::critical>
          backward {g, K, K, 1, ap.inputs, ap.outputs, precomputer, factory, picker};
      const bool backward_win = backward.solve ().has_value ();
      const bool actual_win = actual.status == Status::win_k;
      const bool completed = explicit_result.status != reference::forward_status::resource_limit
                             && (forward.status == Status::win_k || forward.status == Status::lose_k);
      if (expect ("four fixed-K verdicts " + std::to_string (game), completed
                  && actual_win == (explicit_result.status == reference::forward_status::win_k)
                  && actual.status == forward.status && actual_win == backward_win)) ++agreeing_games;
      if (actual_win) {
        ++wins;
        std::vector<State> maxima;
        for (const auto& rank : actual.generators) maxima.push_back (state (rank));
        const Downset candidate {std::move (maxima)}, envelope {state (safe)};
        expect ("existing action-table certificate verifier", acacia::solver_detail::verify_winning_certificate (
            envelope, candidate, state (initial), table, actioner));
        ++table_certificates;
        for (const auto& node : actual.nodes) {
          if (node.losing) continue;
          for (const auto& choice : node.choices) if (choice.active)
            for (const auto& u : games::letters (ap, Variables::inputs))
              if ((u & choice.input_region) != bddfalse) {
                const auto successor = rows.evaluate (node.rank, u & choice.output, K);
                expect ("exhaustive guarded semantic image", successor.rank
                        && *successor.rank == actual.nodes[choice.successor].rank);
                ++exhaustive_choices;
              }
        }
      }
      else {
        ++losses;
        const auto replay = guarded::verify_losing_proof (view, ap, K, actual);
        expect ("all random refutations replay", replay.value && *replay.value);
        ++replayed;
      }
    }
    expect ("5000 completed agreeing frozen games", agreeing_games == 5000 && unknowns == 0);
    expect ("both verdicts exercised", wins > 0 && losses > 0);
  }

  games::TinySpotGame reopening_game (bool alternative) {
    auto g = games::graph (4, true);
    const auto ap = games::alphabet (g, true, true);
    // The stable output c=false reaches 1, which loses one expansion later.
    // c=true reaches a safe cycle at 3. State 2 is accepting (destination
    // increment), so the all-output successor of 1 is unsafe at K=1.
    g->new_edge (0, 1, alternative ? !ap.outputs : bdd (bddtrue));
    if (alternative) g->new_edge (0, 3, ap.outputs);
    g->new_edge (1, 2, bddtrue);
    g->new_edge (2, 2, bddtrue, {0});
    g->new_edge (3, 3, bddtrue);
    return {g, ap, 1, 4};
  }
  void fixtures () {
    const auto fx = reopening_game (true);
    const FrozenAcacia view {fx.graph, fx.bool_threshold};
    const auto win = guarded::solve (view, fx.alphabet, 1);
    expect ("target loss reopens and source wins with alternative", win.status == Status::win_k
            && win.reopened_sources > 0 && not win.nodes[0].losing);
    expect ("failed semantic successor stays excluded", win.nodes[0].choices.size () == 2
            && not win.nodes[0].choices[0].active && win.nodes[0].choices[1].active
            && win.nodes[0].choices[1].output == fx.alphabet.outputs);
    auto cone = reopening_game (true);
    const bdd c = cone.alphabet.outputs;
    const int dv = cone.graph->register_ap ("d");
    cone.alphabet.order.push_back (dv);
    cone.alphabet.outputs &= bdd_ithvar (dv);
    cone.alphabet.ap_vars = cone.graph->ap_vars ();
    const auto blocked = guarded::solve ({cone.graph, cone.bool_threshold}, cone.alphabet, 1);
    expect ("losing cone blocks every output with the failed semantic successor",
            blocked.status == Status::win_k && blocked.nodes[0].choices.size () == 2
            && blocked.nodes[0].choices.back ().output == (c & bdd_nithvar (dv)));

    guarded::Limits stop_after_loss;
    stop_after_loss.max_expansions = 2;
    const auto reopened = guarded::solve (view, fx.alphabet, 1, stop_after_loss);
    expect ("propagation reopens coverage without losing source", reopened.status == Status::resource_limit
            && reopened.reopened_sources == 1 && not reopened.nodes[0].losing
            && reopened.nodes[0].covered_inputs == bddfalse && reopened.nodes[0].queued);

    auto reject = [&] (const std::string& name, auto mutate) {
      auto corrupt = win;
      mutate (corrupt);
      const auto verified = guarded::verify_winning_certificate (view, fx.alphabet, 1, corrupt);
      expect (name, not verified.value);
    };
    reject ("uncovered inputs block WIN even with stale covered=true", [&] (auto& c) {
      c.nodes[0].choices.back ().input_region = !fx.alphabet.inputs;
    });
    reject ("empty choice cannot certify coverage", [] (auto& c) { c.nodes[0].choices.back ().input_region = bddfalse; });
    reject ("missing selected target blocks WIN", [] (auto& c) { c.nodes[0].choices.back ().successor = c.nodes.size (); });
    reject ("losing selected target blocks WIN", [] (auto& c) { c.nodes[c.nodes[0].choices.back ().successor].losing = true; });
    reject ("incomplete rows block WIN", [] (auto& c) { c.nodes[0].active_rows_complete = false; });
    reject ("pending loss blocks WIN", [] (auto& c) { c.pending_loss = true; });
    reject ("unresolved failure blocks WIN", [] (auto& c) { c.failure = Unknown::row_failure; });
    reject ("independent semantic check rejects changed output", [&] (auto& c) { c.nodes[0].choices.back ().output = !fx.alphabet.outputs; });
    reject ("only input support allowed in region", [&] (auto& c) { c.nodes[0].choices.back ().input_region = fx.alphabet.outputs; });
    reject ("output must be total", [] (auto& c) { c.nodes[0].choices.back ().output = bddtrue; });
    reject ("initial rank is bound to frozen graph", [] (auto& c) { c.nodes[0].rank.assign (4, -1); });

    const auto again = guarded::solve (view, fx.alphabet, 1);
    expect ("stable deterministic selection", again.nodes.size () == win.nodes.size ()
            && again.choices_created == win.choices_created && again.proofs.size () == win.proofs.size ());
    for (std::size_t n = 0; n < win.nodes.size (); ++n) {
      expect ("stable ranks", again.nodes[n].rank == win.nodes[n].rank);
      for (std::size_t c = 0; c < win.nodes[n].choices.size (); ++c) {
        const auto& a = win.nodes[n].choices[c];
        const auto& b = again.nodes[n].choices[c];
        expect ("stable regions and constant outputs", a.input_region == b.input_region
                && a.output == b.output && a.successor == b.successor && a.active == b.active);
      }
    }

    const auto lf = reopening_game (false);
    const FrozenAcacia losing_view {lf.graph, lf.bool_threshold};
    const auto loss = guarded::solve (losing_view, lf.alphabet, 1);
    const auto proof = guarded::verify_losing_proof (losing_view, lf.alphabet, 1, loss);
    expect ("all-output refutation replays", loss.status == Status::lose_k && proof.value && *proof.value);
    expect ("all-output proof uses earlier losing cone", loss.initial_proof
            && not loss.proofs[*loss.initial_proof].record.dependencies.empty ());
    for (const auto& p : loss.proofs)
      for (const auto d : p.record.dependencies) expect ("proof is acyclic", d < p.record.id);
    auto corrupt = loss;
    corrupt.proofs.back ().record.dependencies = {corrupt.proofs.back ().record.id};
    expect ("cyclic proof rejected", not guarded::verify_losing_proof (losing_view, lf.alphabet, 1, corrupt).value);
    corrupt = loss;
    corrupt.proofs.back ().record.dependencies.clear ();
    expect ("missing all-output dependency rejected", not guarded::verify_losing_proof (losing_view, lf.alphabet, 1, corrupt).value);
    corrupt = loss;
    ++corrupt.proofs.front ().rows.front ().digest;
    expect ("row digest must rebuild", not guarded::verify_losing_proof (losing_view, lf.alphabet, 1, corrupt).value);
    corrupt = loss;
    corrupt.proofs.front ().rows.clear ();
    expect ("complete-row identities required", not guarded::verify_losing_proof (losing_view, lf.alphabet, 1, corrupt).value);

    corrupt = loss;
    const auto subsumption_id = corrupt.proofs.size ();
    corrupt.proofs.push_back ({{subsumption_id, acacia::solver_detail::losing_reason::env_subsumed,
                               corrupt.initial, 0, {*corrupt.initial_proof}},
                              corrupt.nodes[corrupt.initial].rank, {}, {}});
    corrupt.initial_proof = subsumption_id;
    expect ("subsumption proof replays against earlier generator",
            guarded::verify_losing_proof (losing_view, lf.alphabet, 1, corrupt).value == true);
    corrupt.proofs.back ().record.dependencies = {0};  // target rank does not dominate initial
    expect ("subsumption inequality is checked",
            not guarded::verify_losing_proof (losing_view, lf.alphabet, 1, corrupt).value);

    std::weak_ptr<const spot::twa_graph> weak;
    guarded::SolveResult retained;
    {
      const auto owned = reopening_game (true);
      weak = owned.graph;
      retained = guarded::solve ({owned.graph, owned.bool_threshold}, owned.alphabet, 1);
    }
    expect ("certificate owns graph and AP registrations", not weak.expired ());
    retained = {};
    expect ("certificate releases provider", weak.expired ());

    for (unsigned budget = 0; budget < 8; ++budget) {
      guarded::Limits limit;
      if (budget == 0) limit.rows.max_rows = 0;
      if (budget == 1) limit.queries.max_steps = 0;
      if (budget == 2) limit.max_choices = 0;
      if (budget == 3) limit.max_rank_nodes = 0;
      if (budget == 4) limit.max_expansions = 0;
      if (budget == 5) limit.rows.max_edges_per_row = 0;
      if (budget == 6) limit.rows.max_rows = 1;
      if (budget == 7) limit.queries.max_live_nodes = 0;
      const auto limited = guarded::solve (losing_view, lf.alphabet, 1, limit);
      expect ("budget is RESOURCE_LIMIT, never LOSE", limited.status == Status::resource_limit);
      expect ("budget is never a proof reason", limited.proofs.empty ());
      acacia::spot_lazy_game::RowStore sparse_store {losing_view, limit.rows};
      acacia::spot_lazy_game::Search sparse_search {sparse_store, lf.alphabet, 1, limit};
      const auto sparse = sparse_search.solve ();
      expect ("sparse budget is RESOURCE_LIMIT, never a verdict", sparse.status == Status::resource_limit);
    }
    for (unsigned failure = 0; failure < 3; ++failure) {
      guarded::Limits limit;
      if (failure == 0) limit.verifier_queries.max_steps = 0;
      if (failure == 1) limit.verifier_rows.max_rows = 0;
      if (failure == 2) limit.verifier_queries.aborted = [] (void*) { return true; };
      const auto limited = guarded::solve (view, fx.alphabet, 1, limit);
      expect ("verifier failure is UNKNOWN, never WIN", limited.status == Status::unknown);
      expect ("failed verification publishes no generators", limited.generators.empty ());
      const auto limited_loss = guarded::solve (losing_view, lf.alphabet, 1, limit);
      expect ("failed proof replay is UNKNOWN, never LOSE", limited_loss.status == Status::unknown);
      acacia::spot_lazy_game::RowStore sparse_store {view, limit.rows};
      acacia::spot_lazy_game::Search sparse_search {sparse_store, fx.alphabet, 1, limit};
      const auto sparse = sparse_search.solve ();
      expect ("sparse verifier failure is UNKNOWN", sparse.status == Status::unknown);
      expect ("sparse failed verification publishes no generators", sparse.generators.empty ());
      acacia::spot_lazy_game::RowStore sparse_loss_store {losing_view, limit.rows};
      acacia::spot_lazy_game::Search sparse_loss_search {sparse_loss_store, lf.alphabet, 1, limit};
      expect ("sparse failed proof replay is UNKNOWN", sparse_loss_search.solve ().status == Status::unknown);
    }
  }
}
int main () {
  try {
    const auto dict = spot::make_bdd_dict ();
    BuddyErrors errors;
    fixtures ();
    random_games ();
  }
  catch (const std::exception& e) { expect (std::string {"unexpected exception: "} + e.what (), false); }
  std::cout << "spot-guarded: seed=" << games::seed << "; agreeing_games=" << agreeing_games
            << " (win=" << wins << ", lose=" << losses << "); unknowns=" << unknowns
            << "; replayed=" << replayed << "; exhaustive_choices=" << exhaustive_choices
            << "; action_table_certificates=" << table_certificates << '\n';
  return failures ? 1 : 0;
}
