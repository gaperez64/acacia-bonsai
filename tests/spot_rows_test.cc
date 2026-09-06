// C2 compares the legacy MONA endpoint-pair actioner against complete rows on
// exactly the same frozen graph. Generic transition-Buchi fixtures are labelled
// separately: language equivalence is not a claim about the frozen fixed-K game.

#include "solver/spot_rows.hh"
#include "actioners/standard.hh"
#include "utils/verbose.hh"

#include <posets/vectors.hh>
#include <spot/twa/twaproduct.hh>
#include <spot/twaalgos/contains.hh>

#include <functional>
#include <iostream>
#include <list>
#include <string>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}
namespace posets::vectors {
  size_t bool_threshold = 0;
}

namespace {
  using namespace acacia::spot_rows;
  using solver_state = posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>;
  using transset = std::vector<std::pair<unsigned, unsigned>>;
  int failures = 0;
  std::size_t comparisons = 0;

  bool expect (const std::string& name, bool condition) {
    if (condition) return true;
    std::cerr << "FAIL: " << name << '\n';
    ++failures;
    return false;
  }

  template <typename F>
  void rejects (const std::string& name, F&& f) {
    bool rejected = false;
    try { f (); }
    catch (const std::exception&) { rejected = true; }
    expect (name, rejected);
  }

  spot::twa_graph_ptr buchi_graph (unsigned states, unsigned initial, bool state_acc) {
    auto graph = spot::make_twa_graph (spot::make_bdd_dict ());
    graph->set_buchi ();
    graph->prop_state_acc (state_acc);
    graph->new_states (states);
    graph->set_init_state (initial);
    return graph;
  }

  std::vector<bdd> valuations (const spot::const_twa_ptr& provider) {
    std::vector<bdd> result {bddtrue};
    for (const auto& ap : provider->ap ()) {
      const bdd variable = bdd_ithvar (provider->get_dict ()->varnum (ap));
      std::vector<bdd> next;
      for (const auto& cube : result) {
        next.push_back (cube & !variable);
        next.push_back (cube & variable);
      }
      result = std::move (next);
    }
    return result;
  }

  void frozen_control (bool inputs, bool outputs) {
    const std::string name = "frozen-acacia: C2 inputs=" + std::to_string (inputs)
                             + " outputs=" + std::to_string (outputs);
    auto graph = buchi_graph (4, 2, true);
    const bdd input = inputs ? bdd_ithvar (graph->register_ap ("input")) : bdd (bddtrue);
    const bdd output = outputs ? bdd_ithvar (graph->register_ap ("output")) : bdd (bddtrue);
    // Overlapping guards, parallel edges, simultaneous branches, incomplete
    // letters, and an empty row. Source 0 and destination 1 disagree on SBA
    // acceptance; so do source 1 and destination 0, in the opposite direction.
    graph->new_edge (0, 1, input & output);
    graph->new_edge (0, 1, input);
    graph->new_edge (0, 2, input);
    graph->new_edge (1, 0, bddtrue, {0});
    graph->new_edge (1, 1, output, {0});
    graph->new_edge (2, 3, output);
    constexpr std::size_t threshold = 2;
    posets::vectors::bool_threshold = threshold;
    SpotRows rows {FrozenAcacia {graph, threshold}};
    expect (name + " labelled mode", rows.mode () == IncrementMode::frozen_acacia);
    expect (name + " nonzero initial coordinate", rows.initial_id () == 2
            && rows.initial_rank () == Rank ({-1, -1, 0, -1}));
    for (StateId q = 0; q < graph->num_states (); ++q) {
      expect (name + " graph coordinates preserved",
              graph->state_number (rows.canonical_state (q)) == q);
      expect (name + " metadata discovery requests no row", rows.state (q) == RowState::not_requested);
    }

    for (std::int32_t K : {1, 2, 3}) {
      const Rank caps = rows.safe_caps (K);
      expect (name + " mixed safe caps", caps == Rank ({K - 1, K - 1, 0, 0}));
      expect (name + " caps safe", rows.is_safe (caps, K));
      Rank unsafe = caps;
      unsafe[2] = 1;
      expect (name + " Boolean cap enforced", not rows.is_safe (unsafe, K));
      unsafe = caps;
      unsafe[0] = K;
      expect (name + " numeric cap enforced", not rows.is_safe (unsafe, K));

      for (const bdd& valuation : valuations (graph)) {
        transset enabled;
        for (unsigned p = 0; p < graph->num_states (); ++p)
          for (const auto& edge : graph->out (p))
            if ((edge.cond & valuation) != bddfalse)
              enabled.emplace_back (p, edge.dst);
        // Explicitly the payload type instantiated by MONA/semantic-MONA,
        // including when ACACIA_TRANSITION_ACCEPTANCE is enabled in this TU.
        const std::list<std::pair<bdd, std::list<transset>>> inputs_to_ios {
            {bddtrue, {enabled}}};
        auto actioner = actioners::standard<solver_state>::make (
            graph, inputs_to_ios, static_cast<VECTOR_ELT_T> (K));
        const auto& action = actioner.actions ().front ().second.front ();
        auto compare = [&] (const Rank& rank) {
          posets::utils::vector_mm<VECTOR_ELT_T> packed (rank.size (), -1);
          for (std::size_t q = 0; q < rank.size (); ++q)
            packed[q] = static_cast<VECTOR_ELT_T> (rank[q]);
          const auto expected = actioner.apply (solver_state (std::move (packed)), action,
                                                actioners::direction::forward);
          const auto actual = rows.evaluate (rank, valuation, K);
          bool equal = actual.mode == IncrementMode::frozen_acacia
                       && actual.status == Status::complete && actual.rank
                       && actual.rank->size () == expected.size ();
          if (equal)
            for (std::size_t q = 0; q < expected.size (); ++q)
              equal = equal && (*actual.rank)[q] == expected[q];
          expect (name + " exact successor", equal);
          ++comparisons;
        };
        Rank rank (graph->num_states (), -1);
        std::function<void (std::size_t)> enumerate = [&] (std::size_t q) {
          if (q == rank.size ()) {
            compare (rank);
            return;
          }
          for (rank[q] = -1; rank[q] <= caps[q]; ++rank[q])
            enumerate (q + 1);
        };
        enumerate (0);  // ALL safe mixed ranks, including empty support
        compare (Rank {K, K, 0, 0});  // already saturated inputs, too
      }
    }
    const auto row0 = rows.row (0);
    const auto row1 = rows.row (1);
    expect (name + " increment uses destination, not source mark",
            row0.row && row1.row && row0.row->edges[0].increment
            && row0.row->spot_edges[0].acceptance == spot::acc_cond::mark_t {}
            && not row1.row->edges[0].increment
            && row1.row->spot_edges[0].acceptance.has (0));
    const auto empty = rows.row (3);
    expect (name + " complete empty row", empty.status == Status::complete && empty.row
            && empty.row->edges.empty () && rows.state (3) == RowState::complete);
  }

  struct Counts {
      int created = 0, destroyed = 0, iter_created = 0, iter_destroyed = 0;
      int first = 0, dst = 0, format = 0;
      bool provider_alive = true, bad_lifetime = false, building_seen = false;
      std::vector<unsigned> sources;
  };

  struct fresh_state final : spot::state {
      unsigned value;
      std::shared_ptr<Counts> counts;
      fresh_state (unsigned value, std::shared_ptr<Counts> counts)
        : value (value), counts (std::move (counts)) { ++this->counts->created; }
      int compare (const spot::state* other) const override {
        const auto rhs = static_cast<const fresh_state*> (other)->value;
        return value < rhs ? -1 : value > rhs ? 1 : 0;
      }
      size_t hash () const override { return 7; }  // deliberate collisions
      fresh_state* clone () const override { return new fresh_state (value, counts); }
      void destroy () const override {
        ++counts->destroyed;
        counts->bad_lifetime |= not counts->provider_alive;
        delete this;
      }
  };

  class fresh_provider final : public spot::twa {
    public:
      std::shared_ptr<Counts> counts;
      unsigned edges = 3;
      bool throw_next = false, mutate_acceptance = false;
      SpotRows* observer = nullptr;
      bdd guard;

      explicit fresh_provider (std::shared_ptr<Counts> counts)
        : twa (spot::make_bdd_dict ()), counts (std::move (counts)),
          guard (bdd_ithvar (register_ap ("tracked"))) { set_buchi (); }
      ~fresh_provider () override {
        // release_iter caches one iterator. Destroy it while our fixture's
        // dependencies still exist, as real pooled providers must do as well.
        delete iter_cache_;
        iter_cache_ = nullptr;
        counts->bad_lifetime |= counts->created != counts->destroyed;
        counts->provider_alive = false;
      }
      const spot::state* get_init_state () const override { return new fresh_state (0, counts); }
      std::string format_state (const spot::state*) const override {
        ++counts->format;
        return "all states have the same DISPLAY name";
      }
      bool iterator_released () const { return iter_cache_ != nullptr; }

      class iterator final : public spot::twa_succ_iterator {
        public:
          const fresh_provider* provider;
          unsigned count, index = 0;
          iterator (const fresh_provider* provider, unsigned count)
            : provider (provider), count (count) { ++provider->counts->iter_created; }
          ~iterator () override {
            ++provider->counts->iter_destroyed;
            provider->counts->bad_lifetime |= not provider->counts->provider_alive;
          }
          bool first () override {
            ++provider->counts->first;
            if (provider->observer)
              provider->counts->building_seen |= provider->observer->state (0) == RowState::building;
            index = 0;
            return not done ();
          }
          bool next () override {
            if (provider->throw_next) throw std::runtime_error ("deliberate mid-row exception");
            if (provider->mutate_acceptance)
              const_cast<fresh_provider*> (provider)->set_generalized_buchi (2);
            ++index;
            return not done ();
          }
          bool done () const override { return index >= count; }
          bdd cond () const override { return provider->guard; }
          spot::acc_cond::mark_t acc () const override { return index % 2 ? spot::acc_cond::mark_t {0}
                                                                                     : spot::acc_cond::mark_t {}; }
          const spot::state* dst () const override {
            ++provider->counts->dst;
            return new fresh_state (index < 2 ? 1 : index, provider->counts);
          }
      };

      spot::twa_succ_iterator* succ_iter (const spot::state* source) const override {
        const auto value = static_cast<const fresh_state*> (source)->value;
        counts->sources.push_back (value);
        return new iterator (this, value == 0 ? edges : 0);
      }
  };

  void lifetime_tests () {
    const std::string name = "generic-transition-buchi: ownership";
    auto counts = std::make_shared<Counts> ();
    {
      auto provider = std::make_shared<fresh_provider> (counts);
      std::weak_ptr<spot::twa> weak = provider;
      SpotStateIds ids {provider};
      provider.reset ();
      expect (name + " arena owns provider", not weak.expired ());
      const auto a = ids.intern (new fresh_state (4, counts));
      const auto b = ids.intern (new fresh_state (5, counts));
      const auto again = ids.intern (new fresh_state (4, counts));
      expect (name + " semantic equality and collisions", a == again && b != a && ids.size () == 2);
      expect (name + " duplicate destroyed exactly once", counts->destroyed == 1);
      expect (name + " borrowed canonical pointer stable", ids[a] == ids[again]);
    }
    expect (name + " arena destruction", counts->created == counts->destroyed
            && not counts->provider_alive && not counts->bad_lifetime);

    for (const std::string scenario : {"success", "budget", "exception", "acceptance-change"}) {
      counts = std::make_shared<Counts> ();
      std::weak_ptr<spot::twa> weak_provider;
      std::weak_ptr<spot::bdd_dict> weak_dict;
      {
        auto provider = std::make_shared<fresh_provider> (counts);
        provider->throw_next = scenario == "exception";
        provider->mutate_acceptance = scenario == "acceptance-change";
        weak_provider = provider;
        weak_dict = provider->get_dict ();
        const RowLimits limits {100, scenario == "budget" ? 1U : 100U};
        SpotRows rows {GenericTransitionBuchi {provider}, limits};
        provider->observer = &rows;
        const bdd letter = provider->guard;
        auto* borrowed = provider.get ();
        provider.reset ();
        expect (name + " cache owns provider/dict", not weak_provider.expired () && not weak_dict.expired ());
        expect (name + " initially unrequested", rows.state (0) == RowState::not_requested);
        const auto absent = rows.evaluate (Rank {-1}, letter, 2);
        expect (name + " empty support never expands", absent.rank == std::optional<Rank> (Rank {-1})
                && counts->sources.empty ());
        const auto initial = rows.initial_rank ();
        const auto result = rows.evaluate (initial, letter, 2);
        const Status status = scenario == "success" ? Status::complete
                              : scenario == "budget" ? Status::resource_limit : Status::failed;
        expect (name + " " + scenario + " status", result.status == status
                && result.mode == IncrementMode::generic_transition_buchi);
        expect (name + " iterator released on " + scenario, borrowed->iterator_released ());
        expect (name + " observes building", counts->building_seen);
        expect (name + " destination discovery is not row expansion", counts->sources == std::vector<unsigned> {0});
        expect (name + " initial key unchanged", initial == Rank {0});
        const auto row = rows.row (0);
        expect (name + " row result labels mode", row.mode == IncrementMode::generic_transition_buchi);
        if (status == Status::complete) {
          expect (name + " all enabled increments contribute", result.rank == std::optional<Rank> (Rank { -1, 1, 0 }));
          expect (name + " original marks retained", row.row && row.row->spot_edges.size () == 3
                  && row.row->spot_edges[1].acceptance.has (0));
          const auto* stable = row.row;
          expect (name + " destination stays unrequested", rows.state (1) == RowState::not_requested);
          const auto empty = rows.row (1);
          expect (name + " empty row distinct from unknown", empty.status == Status::complete
                  && empty.row && empty.row->edges.empty () && rows.state (1) == RowState::complete
                  && rows.state (2) == RowState::not_requested);
          expect (name + " cached row and digest stable", rows.row (0).row == stable
                  && rows.row (0).row->digest == stable->digest);
        }
        else {
          expect (name + " no partial rank or row", not result.rank && not row.row
                  && rows.state (0) == RowState::failed_or_resource_limited);
          expect (name + " failed row not retried", counts->sources.size () == 1);
          if (scenario == "budget") expect (name + " early exit", counts->dst == 1);
          else expect (name + " retains error", bool (result.error));
        }
      }
      expect (name + " " + scenario + " destroys all owned states", counts->created == counts->destroyed);
      expect (name + " " + scenario + " destroys all iterators", counts->iter_created == counts->iter_destroyed);
      expect (name + " dependencies released last", not counts->bad_lifetime
              && weak_provider.expired () && weak_dict.expired () && counts->format == 0);
    }

    counts = std::make_shared<Counts> ();
    {
      auto provider = std::make_shared<fresh_provider> (counts);
      provider->edges = 2000;  // several deque blocks and many interner reallocations
      SpotRows rows {GenericTransitionBuchi {provider}};
      const auto row = rows.row (0);
      expect (name + " stable storage while building", row.status == Status::complete
              && row.row && row.row->edges.size () == 2000 && rows.state_count () == 2000
              && rows.complete_rows () == 1 && counts->sources.size () == 1);
    }
    expect (name + " large row cleanup", counts->created == counts->destroyed && not counts->bad_lifetime);
  }

  void budgets_and_modes () {
    const std::string generic = "generic-transition-buchi: ";
    auto graph = buchi_graph (2, 1, false);
    graph->new_edge (1, 0, bddtrue);
    graph->new_edge (1, 0, bddtrue, {0});
    SpotRows rows {GenericTransitionBuchi {graph}};
    expect (generic + "mode", rows.mode () == IncrementMode::generic_transition_buchi);
    expect (generic + "initial rank counts no incoming mark", rows.initial_rank () == Rank {0});
    const auto value = rows.evaluate (Rank {2}, bddtrue, 2);
    expect (generic + "parallel 0/1 increments saturate", value.rank == std::optional<Rank> (Rank {-1, 2}));
    expect (generic + "all numeric caps", rows.safe_caps (2) == Rank ({1, 1})
            && not rows.is_safe (*value.rank, 2));
    const auto edge = rows.row (0).row;
    expect (generic + "parallel increments preserved", edge && edge->edges.size () == 2
            && not edge->edges[0].increment && edge->edges[1].increment);

    SpotRows limited {GenericTransitionBuchi {graph}, RowLimits {0, 10}};
    const auto no_row = limited.evaluate (Rank {0}, bddtrue, 2);
    expect (generic + "row count budget", no_row.status == Status::resource_limit && not no_row.rank);
    auto empty_graph = buchi_graph (1, 0, false);
    SpotRows empty {GenericTransitionBuchi {empty_graph}, RowLimits {1, 0}};
    const auto empty_row = empty.row (0);
    expect (generic + "zero edge budget permits complete empty row", empty_row.status == Status::complete
            && empty_row.row && empty_row.row->edges.empty ());
    SpotRows one_edge_budget {GenericTransitionBuchi {graph}, RowLimits {1, 2}};
    expect (generic + "exact edge budget completes", one_edge_budget.row (0).status == Status::complete);

    // Complete every active row before arithmetic. Row 0 succeeds, row 1 hits
    // the row budget; no successor of the first source may escape as a result.
    auto frozen = buchi_graph (2, 0, true);
    frozen->new_edge (0, 1, bddtrue);
    frozen->new_edge (1, 0, bddtrue, {0});
    SpotRows partial {FrozenAcacia {frozen, 2}, RowLimits {1, 10}};
    const auto interrupted = partial.evaluate (Rank {0, 0}, bddtrue, 2);
    expect ("frozen-acacia: all active rows before arithmetic", interrupted.status == Status::resource_limit
            && not interrupted.rank && partial.state (0) == RowState::complete
            && partial.state (1) == RowState::failed_or_resource_limited);

    rejects ("frozen-acacia: rejects transition-only metadata", [&] { SpotRows bad {FrozenAcacia {graph, 2}}; });
    rejects ("frozen-acacia: rejects invalid threshold", [&] { SpotRows bad {FrozenAcacia {frozen, 3}}; });
    rejects (generic + "rejects null provider", [&] { SpotRows bad {GenericTransitionBuchi {nullptr}}; });
    for (const std::string acceptance : {"t", "f", "Fin(0)", "Inf(0)&Inf(1)", "Fin(0)|Inf(1)"}) {
      auto unsupported = buchi_graph (1, 0, true);
      unsupported->set_acceptance (acceptance == "t" || acceptance == "f" ? 0
                                  : acceptance == "Fin(0)" ? 1 : 2,
                                  spot::acc_cond::acc_code (acceptance.c_str ()));
      rejects (generic + "rejects " + acceptance, [&] { SpotRows bad {GenericTransitionBuchi {unsupported}}; });
      rejects ("frozen-acacia: rejects " + acceptance, [&] { SpotRows bad {FrozenAcacia {unsupported, 1}}; });
    }
    auto alternating = buchi_graph (2, 0, true);
    alternating->new_univ_edge (0, {0, 1}, bddtrue);
    rejects (generic + "rejects raw alternation", [&] { SpotRows bad {GenericTransitionBuchi {alternating}}; });
    rejects ("frozen-acacia: rejects raw alternation", [&] { SpotRows bad {FrozenAcacia {alternating, 2}}; });

    auto invalid_mark = buchi_graph (1, 0, false);
    invalid_mark->new_edge (0, 0, bddtrue, {1});
    SpotRows bad_mark {GenericTransitionBuchi {invalid_mark}};
    expect (generic + "undeclared mark never becomes increment", bad_mark.row (0).status == Status::failed);
    auto alphabet = buchi_graph (1, 0, false);
    const bdd a = bdd_ithvar (alphabet->register_ap ("a"));
    SpotRows letter_rows {GenericTransitionBuchi {alphabet}};
    expect (generic + "rejects incomplete valuation", letter_rows.evaluate (Rank {0}, bddtrue, 2).status == Status::failed);
    expect (generic + "rejects empty letter set", letter_rows.evaluate (Rank {0}, bddfalse, 2).status == Status::failed);
    expect (generic + "accepts full valuation", letter_rows.evaluate (Rank {0}, a, 2).status == Status::complete);
    alphabet->set_generalized_buchi (2);
    expect (generic + "cached rows require fixed acceptance", letter_rows.row (0).status == Status::failed
            && letter_rows.state (0) == RowState::failed_or_resource_limited
            && letter_rows.complete_rows () == 0
            && letter_rows.evaluate (Rank {-1}, a, 2).status == Status::failed);
  }

  void product_control () {
    const std::string name = "generic-transition-buchi: genuine twa_product";
    auto left = buchi_graph (2, 1, false);
    const bdd a = bdd_ithvar (left->register_ap ("a"));
    left->new_edge (1, 0, a, {0});
    left->new_edge (1, 1, bddtrue);
    left->new_edge (0, 1, !a);
    left->new_edge (0, 0, a, {0});
    auto right = spot::make_twa_graph (left->get_dict ());
    right->new_states (2);
    right->set_init_state (0);
    right->set_acceptance (0, spot::acc_cond::acc_code::t ());
    const bdd b = bdd_ithvar (right->register_ap ("b"));
    right->new_edge (0, 1, b);
    right->new_edge (0, 0, !b);
    right->new_edge (1, 0, bddtrue);
    // A one-set Buchi factor times a zero-set true factor still exposes exactly
    // Inf(0). No cursor wrapper, acceptance rewriting, or lazy translation.
    spot::const_twa_ptr product = std::make_shared<spot::twa_product> (left, right);
    SpotRows rows {GenericTransitionBuchi {product}};
    expect (name + " construction has only initial state", rows.state_count () == 1 && rows.complete_rows () == 0);
    auto materialized = spot::make_twa_graph (product, spot::twa::prop_set::all ());
    std::vector<unsigned> copy_ids {materialized->get_init_state_number ()};
    bool exact = true;
    for (StateId p = 0; p < rows.state_count (); ++p) {
      const auto row = rows.row (p);
      if (not expect (name + " row complete", row.status == Status::complete && row.row)) return;
      auto copy_edges = materialized->out (copy_ids.at (p));
      auto it = copy_edges.begin ();
      for (const auto& edge : row.row->spot_edges) {
        if (it == copy_edges.end ()) { exact = false; break; }
        exact = exact && edge.condition == it->cond && edge.acceptance == it->acc;
        if (edge.destination == copy_ids.size ()) copy_ids.push_back (it->dst);
        else exact = exact && copy_ids.at (edge.destination) == it->dst;
        ++it;
      }
      exact = exact && it == copy_edges.end ();
    }
    expect (name + " exact transition correspondence", exact && rows.state_count () == materialized->num_states ());
    auto sorted_ids = copy_ids;
    std::sort (sorted_ids.begin (), sorted_ids.end ());
    expect (name + " state correspondence is bijective", std::adjacent_find (sorted_ids.begin (), sorted_ids.end ()) == sorted_ids.end ());

    auto reconstructed = spot::make_twa_graph (product->get_dict ());
    reconstructed->copy_ap_of (product);
    reconstructed->set_buchi ();
    reconstructed->new_states (rows.state_count ());
    reconstructed->set_init_state (rows.initial_id ());
    for (StateId p = 0; p < rows.state_count (); ++p)
      for (const auto& edge : rows.row (p).row->spot_edges)
        reconstructed->new_edge (p, edge.destination, edge.condition, edge.acceptance);
    expect (name + " same omega language as fully materialized copy", spot::are_equivalent (reconstructed, materialized));

    SpotRows eager_rows {GenericTransitionBuchi {materialized}};
    for (const auto& letter : valuations (product)) {
      const auto lazy_rank = rows.evaluate (rows.initial_rank (), letter, 2);
      const auto eager_rank = eager_rows.evaluate (eager_rows.initial_rank (), letter, 2);
      bool same = lazy_rank.status == Status::complete && eager_rank.status == Status::complete;
      if (same)
        for (StateId q = 0; q < eager_rows.state_count (); ++q) {
          const auto graph_id = materialized->state_number (eager_rows.canonical_state (q));
          const auto found = std::find (copy_ids.begin (), copy_ids.end (), graph_id);
          same = same && found != copy_ids.end () && (*eager_rank.rank)[q] == (*lazy_rank.rank)[found - copy_ids.begin ()];
        }
      expect (name + " same normalized update under every valuation", same);
    }
  }
}  // namespace

int main () {
  try {
    for (bool inputs : {false, true})
      for (bool outputs : {false, true}) frozen_control (inputs, outputs);
    lifetime_tests ();
    budgets_and_modes ();
    product_control ();
  }
  catch (const std::exception& error) {
    expect (std::string {"unexpected exception: "} + error.what (), false);
  }
  if (failures) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "spot-rows: frozen-acacia C2 " << comparisons
            << " exact rank/valuation comparisons; generic-transition-buchi ownership/product checks passed\n";
  return 0;
}
