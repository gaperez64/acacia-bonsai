#include "solver/parity/tlsf_dpa.hh"

#include "solver/parity/route.hh"
#include "tlsf_frontend.hh"
#include "utils/verbose.hh"
extern "C" {
#include <tlsf/ast.h>
}

#include <algorithm>
#include <deque>
#include <iostream>
#include <set>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/contains.hh>
#include <spot/twaalgos/isdet.hh>
#include <spot/twaalgos/translate.hh>
#include <spot/twaalgos/word.hh>
#include <stdexcept>

unsigned utils::verbose = 0;
utils::voutstream utils::vout;
namespace dpa = acacia::tlsf_dpa;
namespace route = acacia::parity_route;

// An upper bound may greatly exceed the emitted priorities. Exercise that
// case on every small language/label fixture without building a large product.
extern "C" uint32_t __real_tlsf_dpa_max_priority (const TlsfDpa* source);
extern "C" uint32_t __wrap_tlsf_dpa_max_priority (const TlsfDpa* source) {
  return __real_tlsf_dpa_max_priority (source) + spot::acc_cond::mark_t::max_accsets ();
}

void require (bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error (message);
}

// Use the public AST visitor, independently of the frontend's printed formula.
spot::formula from_ast (const TlsfAstNode* root) {
  using F = spot::formula;
  using storage = std::deque<F>;
  static const auto save = [] (void* ctx, F f) -> void* {
    auto& values = *static_cast<storage*> (ctx);
    values.push_back (f);
    return &values.back ();
  };
  TlsfAstVisitor visitor {};
#define CONSTANT(field, expression) \
  visitor.field = +[] (void* ctx) -> void* { return save (ctx, expression); }
#define UNARY(field, expression)                          \
  visitor.field = +[] (void* ctx, void* value) -> void* { \
    const F a = *static_cast<F*> (value);                 \
    return save (ctx, expression);                        \
  }
#define BINARY(field, expression)                                   \
  visitor.field = +[] (void* ctx, void* lhs, void* rhs) -> void* {  \
    const F a = *static_cast<F*> (lhs), b = *static_cast<F*> (rhs); \
    return save (ctx, expression);                                  \
  }
  CONSTANT (visit_true, F::tt ());
  CONSTANT (visit_false, F::ff ());
  visitor.visit_ap =
      +[] (void* ctx, const char* name) -> void* { return save (ctx, F::ap (name)); };
  UNARY (visit_not, F::Not (a));
  UNARY (visit_x, F::X (a));
  UNARY (visit_x_strong, F::X (a));
  UNARY (visit_f, F::F (a));
  UNARY (visit_g, F::G (a));
  BINARY (visit_and, F::And ({a, b}));
  BINARY (visit_or, F::Or ({a, b}));
  BINARY (visit_impl, F::Implies (a, b));
  BINARY (visit_equiv, F::Equiv (a, b));
  BINARY (visit_u, F::U (a, b));
  BINARY (visit_r, F::R (a, b));
  BINARY (visit_w, F::W (a, b));
  BINARY (visit_m, F::M (a, b));
#undef CONSTANT
#undef UNARY
#undef BINARY
  storage values;
  auto* value = static_cast<F*> (tlsf_ast_accept (root, &visitor, &values));
  require (value, "AST visitor failed");
  return *value;
}

acacia::tlsf_frontend::specification specification (const std::string& guarantee) {
  return acacia::tlsf_frontend::parse (
      "INFO { TITLE: \"bridge\" SEMANTICS: Mealy TARGET: Mealy } "
      "MAIN { INPUTS { req; a; unused_in; } OUTPUTS { grant; b; unused_out; } "
      "GUARANTEES { " +
      guarantee + "; } }");
}

dpa::context create (const acacia::tlsf_frontend::specification& spec, const dpa::limits& limits) {
  TlsfDpaStatus status;
  auto context =
      dpa::create (tlsf_ast_root (spec.ast.get ()), spec.inputs, spec.outputs, limits, status);
  require (context && status.code == TLSF_DPA_OK, "creation declined");
  return context;
}

bool evaluate (TlsfDpa& source, TlsfDpaLabel id, unsigned valuation) {
  for (;;) {
    const auto* node = tlsf_dpa_label_node (&source, id);
    require (node, "missing exported DAG node");
    if (node->kind != TLSF_DPA_DECISION)
      return node->kind == TLSF_DPA_TRUE;
    id = (valuation & (1u << node->ap)) ? node->high : node->low;
  }
}

void exact_relation (TlsfDpa& source, const dpa::result& imported) {
  auto graph = imported.automaton;
  std::map<uint32_t, unsigned> priorities;
  for (const auto& [id, mapped] : imported.states) {
    const TlsfDpaEdge* edges;
    size_t count;
    require (tlsf_dpa_successors (&source, id, &edges, &count).code == TLSF_DPA_OK,
             "completed row unavailable");
    for (size_t e = 0; e < count; ++e)
      priorities.emplace (edges[e].priority, 0);
  }
  unsigned compressed = priorities.begin ()->first & 1u;
  for (auto& [raw, mapped] : priorities) {
    compressed += (raw & 1u) != (compressed & 1u);
    mapped = compressed;
  }
  require (imported.states.at (tlsf_dpa_initial (&source)) == graph->get_init_state_number (),
           "wrong initial state map");
  require (graph->num_states () == tlsf_dpa_state_count (&source), "BFS lost reachable states");
  require (graph->ap ().size () == tlsf_dpa_ap_count (&source), "unused AP lost");
  for (const auto& [id, mapped] : imported.states) {
    const TlsfDpaEdge *edges, *again;
    size_t count, repeated;
    require (tlsf_dpa_successors (&source, id, &edges, &count).code == TLSF_DPA_OK,
             "completed row unavailable");
    require (tlsf_dpa_successors (&source, id, &again, &repeated).code == TLSF_DPA_OK &&
                 again == edges && repeated == count,
             "unstable row");
    for (unsigned valuation = 0; valuation < (1u << tlsf_dpa_ap_count (&source)); ++valuation) {
      bdd cube = bddtrue;
      for (unsigned ap = 0; ap < tlsf_dpa_ap_count (&source); ++ap) {
        auto variable = bdd_ithvar (
            graph->get_dict ()->varnum (spot::formula::ap (tlsf_dpa_ap_name (&source, ap))));
        cube &= (valuation & (1u << ap)) ? variable : !variable;
      }
      // Every triple, including absent edges; OR repeated guards per (target, priority).
      for (const auto& [target, destination] : imported.states)
        for (unsigned priority = 0; priority <= compressed; ++priority) {
          bool expected = false;
          for (size_t e = 0; e < count; ++e)
            if (edges[e].successor == target && priorities.at (edges[e].priority) == priority)
              expected |= evaluate (source, edges[e].label, valuation);
          bdd actual = bddfalse;
          for (const auto& edge : graph->out (mapped))
            if (edge.dst == destination && edge.acc == spot::acc_cond::mark_t {priority})
              actual |= edge.cond;
          require (expected == ((actual & cube) != bddfalse), "labelled edge relation changed");
        }
    }
  }
}

void equivalent (const spot::twa_graph_ptr& graph, spot::formula formula) {
  spot::translator translate (graph->get_dict ());
  auto reference = translate.run (formula);
  if (!spot::are_equivalent (graph, reference)) {
    auto witness = graph->exclusive_word (reference);
    if (witness)
      std::cerr << *witness << '\n';
    throw std::runtime_error ("DPA language mismatch");
  }
}

void check (const std::string& text, bool real) {
  std::cout << "checking " << text << std::endl;
  const auto spec = specification (text);
  const auto formula = from_ast (tlsf_ast_root (spec.ast.get ()));
  const auto printed = spot::parse_infix_psl (spec.formula);
  require (printed.f == formula, "conversion AST differs from printed objective");
  for (unsigned permutation = 0; permutation < 3; ++permutation) {
    dpa::limits limits;
    auto source = create (spec, limits);
    auto dictionary = spot::make_bdd_dict ();
    auto owner = spot::make_twa_graph (dictionary);
    auto names = spec.inputs;
    names.insert (names.end (), spec.outputs.begin (), spec.outputs.end ());
    if (permutation == 1)
      std::reverse (names.begin (), names.end ());
    if (permutation == 2)
      std::rotate (names.begin (), names.begin () + 2, names.end ());
    for (size_t i = 0; i < names.size (); ++i) {
      owner->register_ap (spot::formula::ap ("gap" + std::to_string (i)));
      owner->register_ap (spot::formula::ap (names[i]));
    }
    auto imported = dpa::import (*source, limits, dictionary);
    require (imported.automaton && imported.status.code == TLSF_DPA_OK, "import declined");
    require (spot::is_deterministic (imported.automaton) && spot::is_complete (imported.automaton),
             "import not deterministic and complete");
    exact_relation (*source, imported);
    auto repeated = dpa::import (*source, limits, dictionary);
    require (repeated.states == imported.states && repeated.labels == imported.labels,
             "repeated import changed IDs or memo size");
    equivalent (imported.automaton, formula);
    source.reset ();
    equivalent (imported.automaton, formula);  // graph owns all BDD references
    if (permutation)
      continue;
    route::request input;
    input.automaton = imported.automaton;
    input.specification = input.formula = formula;
    input.inputs = spec.inputs;
    input.outputs = spec.outputs;
    input.frame = route::timing_frame::effective_mealy;
    route::options config;
    config.verify = true;
    bool solved = false;
    config.on_solved = [&] { solved = true; };
    const auto answer =
        route::run_positive_dpa (input, config, [] (const char* stage, const auto&, double) {
          const std::string name = stage;
          require (
              name != "translate" && name != "orient" && name != "classify" && name != "parity",
              "positive DPA was translated, complemented or classified");
        });
    require (solved && answer.checked && answer.verdict == (real ? "REALIZABLE" : "UNREALIZABLE"),
             "wrong native verdict or unchecked controller");
    input.automaton.reset ();
    input.frame = route::timing_frame::original;
    const auto reference = route::run (input, config);
    require (answer.verdict == reference.verdict, "native verdict differs from Spot arm");
  }
}

void declines () {
  std::cout << "checking declines" << std::endl;
  const auto spec = specification ("X X X grant");
  dpa::limits limits;
  limits.construction.max_states = 2;
  auto source = create (spec, limits);
  const TlsfDpaEdge* row;
  size_t count;
  require (
      tlsf_dpa_successors (source.get (), tlsf_dpa_initial (source.get ()), &row, &count).code ==
          TLSF_DPA_OK,
      "first row declined");
  const auto saved = row[0];
  const auto failed = dpa::import (*source, limits);
  require (!failed.automaton && failed.edges && failed.stage == std::string ("expand") &&
               failed.status.reason == TLSF_DPA_STATE_LIMIT &&
               tlsf_dpa_status (source.get ()).code == TLSF_DPA_DECLINED,
           "later expansion published a partial graph");
  require (row[0].label == saved.label && row[0].successor == saved.successor &&
               tlsf_dpa_label_node (source.get (), saved.label),
           "previous row invalidated");
  require (!dpa::import (*source, limits).automaton, "declined context revived");

  limits.construction.max_states = 100;
  source = create (spec, limits);
  require (tlsf_dpa_expand (source.get ()).code == TLSF_DPA_OK, "healthy expansion declined");
  const auto healthy = dpa::import (*source, limits);
  require (bool (healthy.automaton), "healthy import failed");
  limits.import_bytes = healthy.import_bytes - 1;
  const auto capped = dpa::import (*source, limits);
  require (!capped.automaton && capped.edges && capped.labels &&
               capped.status.reason == TLSF_DPA_HOST_LIMIT,
           "import cap published a partial graph");
  limits.import_bytes = healthy.import_bytes;
  require (bool (dpa::import (*source, limits).automaton), "exact import allowance failed");

  struct cancellation {
      unsigned calls = 0, at = 0;
  } stop;
  limits.construction.cancel_ctx = &stop;
  limits.construction.cancelled = +[] (void* ctx) {
    auto& stop = *static_cast<cancellation*> (ctx);
    return int (++stop.calls == stop.at);
  };
  auto counted = dpa::import (*source, limits);
  require (bool (counted.automaton), "counting import failed");
  bool during_labels = false, after_edges = false;
  const auto polls = stop.calls;
  for (unsigned at = 1; at <= polls; ++at) {
    stop = {0, at};
    const auto cancelled = dpa::import (*source, limits);
    require (!cancelled.automaton && cancelled.status.reason == TLSF_DPA_CANCELLED,
             "cancelled import published an answer");
    during_labels |= cancelled.labels > 0;
    after_edges |= cancelled.edges > 0;
  }
  require (during_labels && after_edges, "cancellation sweep missed a populated import");
  limits = {};
  limits.construction.deadline_mono_ns = 1;
  require (dpa::import (*source, limits).status.reason == TLSF_DPA_DEADLINE, "deadline ignored");
  const auto unsupported = specification ("F G F grant");
  TlsfDpaStatus status;
  auto absent = dpa::create (tlsf_ast_root (unsupported.ast.get ()), unsupported.inputs,
                             unsupported.outputs, {}, status);
  require (!absent && status.reason == TLSF_DPA_UNSUPPORTED, "unsupported syntax admitted");
}

void symbolic () {
  std::cout << "checking shared DAG" << std::endl;
  // A shared parity DAG on 28 used APs must not enumerate its 2^28 letters.
  auto arena = std::unique_ptr<Arena, decltype (&arena_free)> (arena_new (4096), arena_free);
  Node* root = node_false (arena.get ());
  std::vector<std::string> names;
  names.reserve (128);
  for (unsigned i = 0; i < 128; ++i) {
    names.push_back ("p" + std::to_string (i));
    if (i < 28)
      root = node_equiv (arena.get (), root, node_ap (arena.get (), names.back ().c_str ()));
  }
  dpa::limits limits;
  TlsfDpaStatus status;
  auto source =
      dpa::create (reinterpret_cast<const TlsfAstNode*> (root), names, {"unused"}, limits, status);
  require (source && status.code == TLSF_DPA_OK, "shared DAG creation failed");
  arena.reset ();
  auto imported = dpa::import (*source, limits);
  require (imported.automaton && imported.labels < 128 && imported.automaton->ap ().size () == 129,
           "shared DAG or large unused partition expanded");
}

int main () {
  try {
    for (const auto& [text, real] :
         std::vector<std::pair<std::string, bool>> {{"G(req -> X grant)", true},
                                                    {"F grant", true},
                                                    {"true", true},
                                                    {"false", false},
                                                    {"G F (a && X b)", false},
                                                    {"F G (!a || X !b)", true},
                                                    {"!(G(req -> X grant))", false},
                                                    {"!(F grant)", true},
                                                    {"G F (a && X X b)", false},
                                                    {"!(G F (a && X X b))", true}})
      check (text, real);
    declines ();
    symbolic ();
    std::cout << "Exact label relation, language, checked synthesis and declines passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
}
