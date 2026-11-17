#include "solver/closure_recurrence_provider.hh"

#include "solver/configured_components.hh"
#include "solver/formula_input.hh"
#include "solver/k_bounded_safety_aut.hh"

#include <iostream>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/contains.hh>
#include <spot/twaalgos/isdet.hh>
#include <spot/twaalgos/translate.hh>

#include <posets/downsets.hh>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}
namespace posets::vectors {
  size_t bool_threshold = 0;
}

namespace {
  namespace cr = acacia::closure_recurrence;
  namespace cb = acacia::closure_buchi;
  using F = spot::formula;
  void require (bool condition, const char* message) {
    if (!condition)
      throw std::runtime_error (message);
  }
  F parse (const std::string& text) {
    auto p = spot::parse_infix_psl (text);
    if (p.format_errors (std::cerr))
      throw std::runtime_error (text);
    return p.f;
  }
  spot::twa_graph_ptr graph (F bad, const spot::bdd_dict_ptr& dict,
                             cr::Counters* counts = nullptr) {
    auto result = cr::build (bad, dict, {}, counts);
    if (auto failure = std::get_if<cr::Failure> (&result)) {
      if (failure->error)
        std::rethrow_exception (failure->error);
      throw std::runtime_error ("construction failure " + std::to_string (int (failure->kind)));
    }
    return std::get<spot::twa_graph_ptr> (result);
  }
  void structure (const spot::twa_graph_ptr& g) {
    require (g->num_sets () == 1 && g->prop_state_acc ().is_true (), "state based Buchi");
    require (spot::is_complete (g), "complete");
    std::vector<bool> seen (g->num_states ());
    std::vector<unsigned> todo {g->get_init_state_number ()};
    while (!todo.empty ()) {
      const auto q = todo.back ();
      todo.pop_back ();
      if (seen[q])
        continue;
      seen[q] = true;
      std::optional<spot::acc_cond::mark_t> mark;
      for (const auto& e : g->out (q)) {
        require (e.cond != bddfalse, "no false edges");
        require (!mark || *mark == e.acc, "every outgoing edge has the source mark");
        mark = e.acc;
        todo.push_back (e.dst);
      }
    }
    require (std::all_of (seen.begin (), seen.end (), [] (bool b) { return b; }), "reachable");
  }
  void equivalent (F bad, cr::Counters* counts = nullptr) {
    auto dict = spot::make_bdd_dict ();
    auto g = graph (bad, dict, counts);
    structure (g);
    auto reference = spot::translator (dict).run (bad);
    require (spot::are_equivalent (g, reference), "language equivalence in both directions");
  }
  void failed (F bad, cb::Options options, cb::FailureKind expected) {
    auto result = cr::build (bad, spot::make_bdd_dict (), options);
    auto failure = std::get_if<cb::Failure> (&result);
    require (failure && failure->kind == expected, "typed failure, no partial graph");
  }

  F c2 (unsigned n) {
    std::vector<F> fair, exclusive;
    const auto g = F::ap ("g");
    for (unsigned i = 0; i < n; ++i) {
      const auto r = F::ap ("r" + std::to_string (i));
      fair.push_back (F::G (F::F (r)));
      for (unsigned j = i + 1; j < n; ++j)
        exclusive.push_back (F::Implies (F::And ({r, F::ap ("r" + std::to_string (j))}),
                                         F::G (F::Not (g))));
    }
    return F::And ({F::Equiv (F::And (fair), F::G (F::F (g))), F::G (F::And (exclusive))});
  }

  bool backward_win (F positive) {
    auto dict = spot::make_bdd_dict ();
    const auto i = dict->register_proposition (F::ap ("i"), &dict);
    const auto o = dict->register_proposition (F::ap ("o"), &dict);
    auto g = graph (F::Not (positive), dict);
    dict->unregister_all_my_variables (&dict);
    using V = posets::vectors::VECTOR_IMPL_AUTO<VECTOR_ELT_T>;
    using D = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<V>;
    using A = actioners::standard<V>;
    posets::vectors::bool_threshold = g->num_states ();
    auto worker = k_bounded_safety_aut_detail<D, IOS_PRECOMPUTER, A, INPUT_PICKER> (
        g, 1, 3, 1, bdd_ithvar (i), bdd_ithvar (o), IOS_PRECOMPUTER (), A (), INPUT_PICKER ());
    return worker.solve ().has_value ();
  }

  bool periodic (const spot::twa_graph_ptr& g, bdd letter) {
    std::map<unsigned, std::size_t> seen;
    std::vector<bool> marks;
    auto q = g->get_init_state_number ();
    while (!seen.contains (q)) {
      seen.emplace (q, marks.size ());
      std::optional<unsigned> next;
      for (const auto& e : g->out (q))
        if ((e.cond & letter) != bddfalse) {
          require (!next, "wide block is deterministic on a letter");
          next = e.dst;
          marks.push_back (bool (e.acc));
        }
      require (next.has_value (), "complete wide block");
      q = *next;
    }
    return std::any_of (marks.begin () + seen.at (q), marks.end (), [] (bool b) { return b; });
  }
}

int main () {
  try {
    // These are signed worker formulas (bad), not positive objectives.
    for (const auto* text : {"true",
                             "false",
                             "p",
                             "!p",
                             "p & q",
                             "p | q",
                             "X p",
                             "XX p",
                             "p U q",
                             "p R q",
                             "p W q",
                             "p M q",
                             "F p",
                             "G p",
                             "p xor q",
                             "!(p U q)",
                             "!(p R q)",
                             "!(p W q)",
                             "!(p M q)",
                             "G(p U q)",
                             "G((p U q) | X(p U q))",
                             "G F p",
                             "G(F p & F q)",
                             "G F p & G F q",
                             "X(G F p & G F q)",
                             "(G F p & G F q) | G !p",
                             "F p & G F p",
                             "F p & G F p & G F q",
                             "G p & F !p",
                             "p & !p",
                             "!(G p & G q)",
                             "!((G p) <-> (F q))",
                             "!(p -> (G q & F p))",
                             "!((p & X q) -> (G p & G q))",
                             "!((G p) -> ((F q) <-> (X p)))",
                             "!(G p & G !p)",
                             "!((G p) & (G q) & (G(p | q)))"}) {
      equivalent (parse (text));
    }
    cr::Counters counts;
    equivalent (parse ("!((p & X q) -> (G p & G q))"), &counts);
    require (counts.branches == 2, "consequent split retains full antecedent");
    equivalent (parse ("!((G p) <-> (F q))"), &counts);
    require (counts.branches == 2, "equivalence split before NNF");
    equivalent (parse ("F p & G F p & G F q"), &counts);
    require (counts.blocks == 1 && counts.watches == 3, "sibling block and separate eventuality");
    equivalent (parse ("X G(F p & F q)"), &counts);
    require (counts.blocks == 1 && counts.watches == 2, "block below X remains reachable");

    for (unsigned n : {4, 6, 25}) {
      auto phi = c2 (n);
      const auto real_bad = F::Not (phi);
      // The formula-unreal arm swaps I/O, shifts the new input g, and
      // translates the positive objective (without negating it).
      acacia::formula_input::shift_inputs (phi, {"g"});
      for (auto bad : {real_bad, phi})
        if (n > 4)  // Spot's equivalence check itself runs out of memory at n = 6.
          structure (graph (bad, spot::make_bdd_dict ()));
        else
          equivalent (bad);
    }

    // Theta's duplicate F X acc expansion, with only one watch so its
    // state count does not depend on the order of other obligations.
    auto repeated = graph (parse ("G F X p"), spot::make_bdd_dict ());
    structure (repeated);
    require (repeated->num_states () == 5 && repeated->num_edges () == 11,
             "a pending release right operand is expanded once");
    // A release left operand is not on every path, so it must not be dropped.
    equivalent (parse ("(p R q) & X p"));
    // Obligations shared between a pending G and the pending set are expanded
    // once (the per-path DFS built 62 states; expanding them twice built 290).
    auto shared = graph (parse ("G(F X p0 & F X p1 & F X p2 & F X p3)"), spot::make_bdd_dict ());
    structure (shared);
    std::cout << "shared obligations: " << shared->num_states () << " states\n";
    require (shared->num_states () <= 62, "shared obligations are expanded once per row");

    std::vector<F> members;
    for (unsigned i = 0; i < 70; ++i)
      members.push_back (F::F (F::ap ("p" + std::to_string (i))));
    // Exercise all 70 live phases, including the last watch being postponed.
    const auto wide = F::G (F::And (members));
    auto wide_graph = graph (wide, spot::make_bdd_dict (), &counts);
    structure (wide_graph);
    require (counts.blocks == 1 && counts.watches == 70, "more than 64 block watches");
    bdd all = bddtrue, missing_last = bddtrue;
    for (unsigned i = 0; i < 70; ++i) {
      const auto var = wide_graph->get_dict ()->varnum (F::ap ("p" + std::to_string (i)));
      all &= bdd_ithvar (var);
      missing_last &= i == 69 ? bdd_nithvar (var) : bdd_ithvar (var);
    }
    require (periodic (wide_graph, all) && !periodic (wide_graph, missing_last),
             "70-watch fairness");
    // This equivalent-to-false fixture keeps the >64-watch language comparison
    // fast on Spot builds whose acceptance marks have only 64 bits.
    equivalent (F::And ({wide, parse ("(r | s) & !r & !s")}), &counts);
    require (counts.watches == 70, "more than 64 watches in language comparison");

    require (backward_win (parse ("G o")) && backward_win (parse ("G !o")),
             "clauses individually realizable");
    require (!backward_win (parse ("G o & G !o")),
             "branch union loses in the actual backward solver");

    const auto f = parse ("G(F p & F q) | (p U q)");
    cb::Options o;
    o.max_states = 1;
    failed (f, o, cb::FailureKind::state_limit);
    o = {};
    o.max_edges_per_row = 0;
    failed (f, o, cb::FailureKind::row_limit);
    o = {};
    o.max_rows = 0;
    failed (f, o, cb::FailureKind::row_limit);
    o = {};
    o.max_branches_per_row = 0;
    failed (f, o, cb::FailureKind::branch_limit);
    o = {};
    o.max_steps_per_row = 0;
    failed (f, o, cb::FailureKind::branch_limit);
    o = {};
    o.max_guards_per_row = 0;
    failed (f, o, cb::FailureKind::guard_limit);
    o = {};
    o.max_live_bdd_nodes = 0;
    failed (f, o, cb::FailureKind::guard_limit);
    o = {};
    o.max_normalization_nodes = 1;
    failed (f, o, cb::FailureKind::normalization_limit);
    o = {};
    o.max_normalization_depth = 1;
    failed (f, o, cb::FailureKind::normalization_limit);
    o = {};
    o.max_bytes = 0;
    failed (f, o, cb::FailureKind::memory_limit);
    // The builder alone fits; its live normalizer must share the same budget.
    o.max_bytes = 1536;
    failed (F::tt (), o, cb::FailureKind::memory_limit);
    o = {};
    o.hooks = std::make_shared<cb::Hooks> ();
    o.hooks->cancelled = [] { return true; };
    failed (f, o, cb::FailureKind::cancelled);
    o.hooks->cancelled = {};
    for (auto point : {cb::FaultPoint::branch, cb::FaultPoint::guard, cb::FaultPoint::interning,
                       cb::FaultPoint::publication}) {
      o.hooks->fail = [point] (auto p) { return p == point; };
      failed (f, o, cb::FailureKind::injected);
    }
    // Dictionary order, including declarations absent from the formula.
    auto dict = spot::make_bdd_dict ();
    const auto z = F::ap ("z"), a = F::ap ("a"), unused = F::ap ("unused");
    dict->register_proposition (z, &dict);
    dict->register_proposition (a, &dict);
    dict->register_proposition (unused, &dict);
    auto g = graph (F::F (a), dict);
    require (g->ap () == std::vector<F> {z, a, unused}, "worker dictionary order and unused APs");
    dict->unregister_all_my_variables (&dict);
    std::cout << "closure recurrence: languages, structure, limits and dictionary passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what () << '\n';
    return 1;
  }
}
