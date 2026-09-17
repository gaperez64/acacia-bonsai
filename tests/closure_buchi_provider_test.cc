#include "solver/closure_buchi_provider.hh"

#include "solver/spot_rows.hh"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/contains.hh>
#include <spot/twaalgos/translate.hh>

namespace {
  namespace cb = acacia::closure_buchi;
  using F = spot::formula;
  using Op = spot::op;
  using Provider = cb::Provider;
  using Bits = std::vector<unsigned char>;
  std::size_t assertions = 0, lasso_checks = 0, language_checks = 0, fault_checks = 0;
  void require (bool condition, const std::string& message) {
    ++assertions;
    if (!condition)
      throw std::runtime_error (message);
  }
  F parse (const std::string& text) {
    auto p = spot::parse_infix_psl (text);
    if (p.format_errors (std::cerr))
      throw std::runtime_error ("bad fixture: " + text);
    return p.f;
  }
  template <class T>
  T value (cb::Result<T> r) {
    if (auto f = std::get_if<cb::Failure> (&r)) {
      if (f->error)
        std::rethrow_exception (f->error);
      throw std::runtime_error ("provider failure " + std::to_string (int (f->kind)));
    }
    return std::get<T> (std::move (r));
  }
  template <class T>
  void failure (const cb::Result<T>& r, cb::FailureKind kind) {
    auto f = std::get_if<cb::Failure> (&r);
    require (f && f->kind == kind, "typed failure without partial value");
  }
  auto make (F f, cb::Options o = {}) {
    return value (Provider::create (f, spot::make_bdd_dict (), std::move (o)));
  }
  auto make (const std::string& text, cb::Options o = {}) {
    return make (parse (text), std::move (o));
  }
  struct Destroy {
      void operator() (const spot::state* s) const { s->destroy (); }
  };
  using Owned = std::unique_ptr<const spot::state, Destroy>;

  // Independent evaluator of the ORIGINAL syntax on finite lasso positions.
  // W uses the greatest until fixpoint; M uses the least release fixpoint.
  // Neither provider normalization nor any automaton translation is involved.
  class LassoEvaluator {
      const std::vector<unsigned>& word_;
      std::size_t prefix_;
      std::map<F, Bits> memo_;
      std::size_t next (std::size_t i) const { return i + 1 == word_.size () ? prefix_ : i + 1; }

    public:
      LassoEvaluator (const std::vector<unsigned>& word, std::size_t prefix)
        : word_ (word),
          prefix_ (prefix) {}
      Bits evaluate (F f) {
        if (auto p = memo_.find (f); p != memo_.end ())
          return p->second;
        std::vector<Bits> c;
        for (auto a : f)
          c.push_back (evaluate (a));
        Bits result (word_.size (), false);
        auto fixpoint = [&] (bool greatest, auto step) {
          std::fill (result.begin (), result.end (), greatest);
          for (;;) {
            Bits update = result;
            for (std::size_t i = 0; i < result.size (); ++i)
              update[i] = step (i, result[next (i)]);
            if (update == result)
              break;
            result = std::move (update);
          }
        };
        switch (f.kind ()) {
          case Op::tt: std::fill (result.begin (), result.end (), true); break;
          case Op::ff: break;
          case Op::ap: {
            require (f.ap_name () == "p" || f.ap_name () == "q", "evaluator alphabet");
            const unsigned mask = f.ap_name () == "p" ? 1 : 2;
            for (std::size_t i = 0; i < result.size (); ++i)
              result[i] = (word_[i] & mask) != 0;
            break;
          }
          case Op::Not:
            for (std::size_t i = 0; i < result.size (); ++i)
              result[i] = !c[0][i];
            break;
          case Op::And:
          case Op::Or:
            for (std::size_t i = 0; i < result.size (); ++i) {
              result[i] = f.is (Op::And);
              for (auto& a : c)
                result[i] = f.is (Op::And) ? result[i] && a[i] : result[i] || a[i];
            }
            break;
          case Op::Implies:
          case Op::Equiv:
          case Op::Xor:
            for (std::size_t i = 0; i < result.size (); ++i)
              result[i] = f.is (Op::Implies) ? !c[0][i] || c[1][i]
                          : f.is (Op::Equiv) ? c[0][i] == c[1][i]
                                             : c[0][i] != c[1][i];
            break;
          case Op::X:
            for (std::size_t i = 0; i < result.size (); ++i)
              result[i] = c[0][next (i)];
            break;
          case Op::F: fixpoint (false, [&] (auto i, bool n) { return c[0][i] || n; }); break;
          case Op::G: fixpoint (true, [&] (auto i, bool n) { return c[0][i] && n; }); break;
          case Op::U:
          case Op::W:
            fixpoint (f.is (Op::W), [&] (auto i, bool n) { return c[1][i] || (c[0][i] && n); });
            break;
          case Op::R:
          case Op::M:
            fixpoint (f.is (Op::R), [&] (auto i, bool n) { return c[1][i] && (c[0][i] || n); });
            break;
          default: throw std::runtime_error ("unsupported original syntax in lasso evaluator");
        }
        memo_.emplace (f, result);
        return result;
      }
  };

  struct Alphabet {
      spot::bdd_dict_ptr dict;
      std::vector<bdd> letters;
      explicit Alphabet (spot::bdd_dict_ptr d) : dict (std::move (d)) {
        int p = dict->register_proposition (F::ap ("p"), this);
        int q = dict->register_proposition (F::ap ("q"), this);
        for (unsigned v = 0; v < 4; ++v)
          letters.push_back ((v & 1 ? bdd_ithvar (p) : bdd_nithvar (p)) &
                             (v & 2 ? bdd_ithvar (q) : bdd_nithvar (q)));
      }
      ~Alphabet () { dict->unregister_all_my_variables (this); }
  };
  bool accepts (const Provider& p, const Alphabet& alphabet, const std::vector<unsigned>& word,
                std::size_t prefix) {
    using Key = std::pair<cb::StateId, std::size_t>;
    std::map<Key, unsigned> ids;
    std::vector<Key> keys;
    std::vector<std::vector<std::pair<unsigned, bool>>> edges;
    auto intern = [&] (Key k) {
      auto [it, fresh] = ids.emplace (k, keys.size ());
      if (fresh) {
        keys.push_back (k);
        edges.emplace_back ();
      }
      return it->second;
    };
    intern ({p.initial_state (), 0});
    for (std::size_t v = 0; v < keys.size (); ++v) {
      auto [q, pos] = keys[v];
      auto row = value (p.request_row (q));
      for (auto& e : row)
        if ((e.condition & alphabet.letters[word[pos]]) != bddfalse) {
          auto dest = intern ({e.destination, pos + 1 == word.size () ? prefix : pos + 1});
          edges[v].emplace_back (dest, e.accepting);
        }
    }
    // Tarjan on the reachable product. An accepting internal edge in an SCC
    // lies on a reachable cycle, including the singleton self-loop case.
    std::vector<int> index (keys.size (), -1), low (keys.size ()), component (keys.size (), -1);
    std::vector<unsigned> stack;
    std::vector<bool> active (keys.size ());
    int serial = 0, components = 0;
    std::function<void (unsigned)> visit = [&] (unsigned v) {
      index[v] = low[v] = serial++;
      stack.push_back (v);
      active[v] = true;
      for (auto [w, acc] : edges[v]) {
        (void) acc;
        if (index[w] == -1) {
          visit (w);
          low[v] = std::min (low[v], low[w]);
        }
        else if (active[w])
          low[v] = std::min (low[v], index[w]);
      }
      if (low[v] == index[v]) {
        for (;;) {
          auto w = stack.back ();
          stack.pop_back ();
          active[w] = false;
          component[w] = components;
          if (w == v)
            break;
        }
        ++components;
      }
    };
    visit (0);
    for (std::size_t v = 0; v < edges.size (); ++v)
      for (auto [w, acc] : edges[v])
        if (acc && component[v] == component[w])
          return true;
    return false;
  }

  const std::vector<std::string> formulas {"true",
                                           "false",
                                           "p",
                                           "!p",
                                           "p & q",
                                           "p | q",
                                           "p -> q",
                                           "!(p -> q)",
                                           "p <-> q",
                                           "!(p <-> q)",
                                           "p xor q",
                                           "!(p xor q)",
                                           "!(p & q)",
                                           "!(p | q)",
                                           "X p",
                                           "!X p",
                                           "XX p",
                                           "p U q",
                                           "!(p U q)",
                                           "p R q",
                                           "!(p R q)",
                                           "p W q",
                                           "!(p W q)",
                                           "p M q",
                                           "!(p M q)",
                                           "F p",
                                           "G p",
                                           "!F p",
                                           "!G p",
                                           "GF p",
                                           "FG p",
                                           "G(F(p | X q))",
                                           "F(G(p & X q))",
                                           "F p | F q",
                                           "F p & F q",
                                           "X(F p & F q)",
                                           "F(X(p U q))",
                                           "(F p & X q) | (F p & !q)",
                                           "G(p -> F q)",
                                           "(p U q) & X(p U q)",
                                           "(p U q) | X(p U q)",
                                           "(p R q) & X(p R q)",
                                           "(F p) <-> (G q)",
                                           "(F p) xor (X q)",
                                           "!(X(p U q) | G(p -> X q))",
                                           "(p | q) & !p & !q",
                                           "X false",
                                           "(p U q) U (q U p)",
                                           "(p R q) R (q R p)",
                                           "G(F p & F q)"};

  void semantics () {
    for (const auto& text : formulas) {
      auto original = parse (text);
      auto p = make (original);
      require (
          p->discovered_states () == 1 && p->complete_rows () == 0 && p->counters ().raw_rows == 0,
          "local factory: " + text);
      for (auto f : p->closure ())
        require (f.is (Op::tt, Op::ff, Op::ap, Op::And) || f.is (Op::Or, Op::X, Op::U, Op::R) ||
                     (f.is (Op::Not) && f[0].is (Op::ap)),
                 "normalized closure contains only NNF operators");
      Alphabet alphabet {p->get_dict ()};
      for (std::size_t prefix = 0; prefix <= 2; ++prefix)
        for (std::size_t cycle = 1; cycle <= 2; ++cycle) {
          const auto n = prefix + cycle;
          const unsigned count = 1u << (2 * n);
          for (unsigned encoding = 0; encoding < count; ++encoding) {
            std::vector<unsigned> word (n);
            unsigned bits = encoding;
            for (auto& letter : word) {
              letter = bits & 3;
              bits >>= 2;
            }
            const bool expected = LassoEvaluator {word, prefix}.evaluate (original)[0];
            require (accepts (*p, alphabet, word, prefix) == expected,
                     "lasso mismatch: " + text + " prefix=" + std::to_string (prefix) + " cycle=" +
                         std::to_string (cycle) + " word=" + std::to_string (encoding));
            ++lasso_checks;
          }
        }
      // Reuse the existing spot_lazy_buchi_view_test.cc cross-check recipe:
      // materialize, independently translate, then spot::are_equivalent.
      // No catch/timeout/inconclusive path can turn this into a pass.
      auto graph = value (p->materialize ());
      auto reference = spot::translator {p->get_dict ()}.run (original);
      require (spot::are_equivalent (graph, reference), "Spot equivalence: " + text);
      require (graph->acc ().is_buchi () && graph->num_sets () == 1 && graph->is_existential () &&
                   !graph->prop_state_acc ().is_true (),
               "graph contract");
      require (graph->num_states () == p->discovered_states () &&
                   p->complete_rows () == p->discovered_states (),
               "complete eager policy");
      // Exercise the twa iterator separately from materialize's direct row API.
      auto through_twa = spot::make_twa_graph (p, spot::twa::prop_set::all ());
      require (spot::are_equivalent (through_twa, reference), "twa equivalence: " + text);
      language_checks += 2;
    }
    std::cout << "semantics: " << formulas.size () << " original formulas, " << lasso_checks
              << " exhaustive lassos (420/formula; 2 APs, prefix 0..2, cycle 1..2), "
              << language_checks << " Spot cross-checks\n";
  }

  void structure () {
    auto p = make ("p");
    require (p->untils ().empty (), "zero untils");
    auto row = value (p->request_row (0));
    require (row.size () == 1 && row[0].accepting, "literal accepting transition");
    auto empty = row[0].destination;
    require (p->obligations (empty).empty () && !p->is_complete (empty), "discovery only");
    auto stable = row.data ();
    auto continuation = value (p->request_row (empty));
    require (continuation.size () == 1 && continuation[0].destination == empty &&
                 continuation[0].condition == bddtrue && continuation[0].accepting,
             "empty obligation set has actual true self-continuation");
    require (value (p->request_row (0)).data () == stable, "immutable row cache");
    auto dead = make ("(p | q) & !p & !q");
    require (value (dead->request_row (0)).empty () && dead->is_complete (0),
             "contradictory row complete and empty");
    require (dead->counters ().branches_pruned > 0, "contradictory BDDs pruned");
    auto never = make ("false");
    require (value (never->request_row (0)).empty (), "false has no sink totalization");
    auto lowered = make ("p -> (p | q)");
    require (lowered->normalized_formula ().is (Op::Or) && lowered->ap ().size () == 2 &&
                 lowered->complete_rows () == 0,
             "normalization retains original worker AP inventory after Boolean lowering");
    auto disjunction = make ("p | q");
    auto dr = value (disjunction->request_row (0));
    auto dict = disjunction->get_dict ();
    require (dr.size () == 1 && dr[0].condition == (bdd_ithvar (dict->varnum (F::ap ("p"))) |
                                                    bdd_ithvar (dict->varnum (F::ap ("q")))),
             "equal T and P merge by guard disjunction");
    require (make ("p W q")->normalized_formula () == make ("q R (p | q)")->normalized_formula (),
             "explicit weak-until identity");
    require (make ("p M q")->normalized_formula () == make ("q U (p & q)")->normalized_formula (),
             "explicit strong-release identity");

    auto f = F::ap ("p");
    for (int i = 0; i < 50; ++i)
      f = F::X (f);
    auto chain = make (f);
    require (chain->discovered_states () == 1 && chain->closure ().size () == 51,
             "long X fixed closure only");
    for (cb::StateId q = 0; q < 51; ++q) {
      auto r = value (chain->request_row (q));
      require (r.size () == 1 && r[0].destination == q + 1, "long X small row");
      require (chain->complete_rows () == q + 1 && chain->discovered_states () == q + 2 &&
                   !chain->is_complete (q + 1),
               "long X no upfront expansion");
    }

    // More untils than Spot's mark capacity, all initially protected by X.
    std::vector<F> terms;
    for (unsigned i = 0; i < SPOT_MAX_ACCSETS + 1; ++i)
      terms.push_back (F::X (i + 1, F::F (F::ap ("a" + std::to_string (i)))));
    auto many = make (F::And (terms));
    require (many->untils ().size () == SPOT_MAX_ACCSETS + 1,
             "unbounded syntactic until inventory");
    require (many->num_sets () == 1, "single ordinary acceptance mark");
    require (value (many->request_row (0)).size () == 1,
             "large until inventory has small initial row");
    std::cout << "inventory: " << many->untils ().size ()
              << " untils > SPOT_MAX_ACCSETS=" << SPOT_MAX_ACCSETS << "; long-X depth=50\n";

    terms.clear ();
    for (unsigned i = 0; i < SPOT_MAX_ACCSETS + 1; ++i)
      terms.push_back (F::F (F::ap ("a" + std::to_string (i))));
    auto wide = make (F::Or (terms));
    require (wide->ap ().size () == SPOT_MAX_ACCSETS + 1, "all APs registered before first row");
    auto first = value (wide->request_row (0));
    auto discharged = std::find_if (first.begin (), first.end (), [&] (const auto& e) {
      return wide->obligations (e.destination).empty ();
    });
    require (discharged != first.end (), "wide cursor can discharge immediately");
    auto empty_cursor = discharged->destination;
    const auto raw_before = wide->counters ().raw_rows;
    std::size_t wraps = 0;
    for (std::size_t i = 0; i < wide->untils ().size (); ++i) {
      auto j = wide->cursor (empty_cursor);
      auto r = value (wide->request_row (empty_cursor));
      require (r.size () == 1 && r[0].condition == bddtrue &&
                   wide->cursor (r[0].destination) == (j + 1) % wide->untils ().size () &&
                   r[0].accepting == (j + 1 == wide->untils ().size ()),
               "empty cursor advances exactly one, beyond Spot's color limit");
      wraps += r[0].accepting;
      empty_cursor = r[0].destination;
    }
    require (wraps == 1 && wide->counters ().raw_rows == raw_before + 1,
             "65 empty cursor rows share one expansion");

    auto repeated = make ("(p U q) & X(p U q)");
    require (repeated->untils ().size () == 1, "shared until closure identity");
    auto rr = value (repeated->request_row (0));
    require (rr.size () == 2 && rr[0].destination == rr[1].destination &&
                 rr[0].accepting != rr[1].accepting,
             "same next obligations retain different postponement/acceptance");
    auto choice = make ("F p | F q");
    Alphabet alphabet {choice->get_dict ()};
    require (accepts (*choice, alphabet, {1}, 0),
             "unchosen disjunct does not leave outstanding until");
    auto both = make ("F p & F q");
    require (!accepts (*both, Alphabet {both->get_dict ()}, {1}, 0),
             "conjunction needs both untils");
    auto eventual = make ("F p");
    require (!accepts (*eventual, Alphabet {eventual->get_dict ()}, {0}, 0),
             "indefinite postponement rejected");
    auto global = make ("G p");
    require (!accepts (*global, Alphabet {global->get_dict ()}, {1, 0}, 1),
             "G p blocks on false p");

    auto graph = value (both->materialize ());
    (void) graph;
    bool found = false;
    for (cb::StateId a = 0; a < both->discovered_states (); ++a)
      for (cb::StateId b = a + 1; b < both->discovered_states (); ++b)
        if (std::ranges::equal (both->obligations (a), both->obligations (b))) {
          require (both->cursor (a) != both->cursor (b), "same S different cursor ids");
          Owned sa {both->state_from_id (a)}, sb {both->state_from_id (b)};
          require (sa->compare (sb.get ()) != 0, "cursor Spot identity distinct");
          found = true;
        }
    require (found && both->counters ().raw_rows < both->complete_rows (),
             "raw cache shared across cursors");
    auto other = make ("F p & F q");
    Owned a {both->get_init_state ()}, b {other->get_init_state ()}, clone {a->clone ()};
    const auto hash = a->hash ();
    require (a->compare (clone.get ()) == 0 && hash == clone->hash (), "clone identity");
    require (a->compare (b.get ()) != 0 && a->compare (b.get ()) == -b->compare (a.get ()),
             "context identity");
    both.reset ();  // owned states retain context, dictionary and stable IDs
    require (a->hash () == hash && a->compare (clone.get ()) == 0, "state outlives adapter");
    bool foreign = false;
    try {
      other->succ_iter (a.get ());
    } catch (const cb::AdapterFailure& e) {
      foreign = e.failure ().kind == cb::FailureKind::invalid_state;
    }
    require (foreign, "foreign state rejected");

    // Thin existing SpotRows route: all numeric, transition increment, initial 0.
    auto adapter = make ("F p");
    acacia::spot_rows::SpotRows rows {acacia::spot_rows::GenericTransitionBuchi {adapter}};
    require (rows.initial_rank () == acacia::spot_rows::Rank {0}, "generic initial rank zero");
    auto r = rows.row (0);
    require (r.status == acacia::spot_rows::Status::complete && r.row->edges.size () == 2,
             "existing row representation compatibility");
  }

  using Signature = std::vector<std::tuple<std::vector<cb::ClosureId>, std::size_t, bool, int>>;
  Signature signature (const Provider& p, cb::Row row) {
    Signature result;
    for (const auto& e : row) {
      auto s = p.obligations (e.destination);
      result.emplace_back (std::vector<cb::ClosureId> (s.begin (), s.end ()),
                           p.cursor (e.destination), e.accepting, e.condition.id ());
    }
    std::sort (result.begin (), result.end ());
    return result;
  }
  void faults () {
    const auto f = parse ("(F p & F q) | X(p U q)");
    auto dict = spot::make_bdd_dict ();
    auto clean = value (Provider::create (f, dict));
    const auto expected = signature (*clean, value (clean->request_row (0)));
    for (auto point : {cb::FaultPoint::branch, cb::FaultPoint::guard, cb::FaultPoint::interning,
                       cb::FaultPoint::publication}) {
      auto hooks = std::make_shared<cb::Hooks> ();
      cb::Options o;
      o.hooks = hooks;
      auto observed = value (Provider::create (f, dict, o));
      std::size_t count = 0;
      hooks->fail = [&] (auto p) {
        count += p == point;
        return false;
      };
      value (observed->request_row (0));
      require (count != 0, "fault seam exercised");
      // Fail at EVERY hit of each stage, including after some destinations
      // have been interned and immediately before the joint cache publication.
      for (std::size_t ordinal = 1; ordinal <= count; ++ordinal) {
        hooks->fail = {};
        auto p = value (Provider::create (f, dict, o));
        std::size_t seen = 0;
        hooks->fail = [&] (auto stage) { return stage == point && ++seen == ordinal; };
        failure (p->request_row (0), cb::FailureKind::injected);
        require (!p->is_complete (0) && p->complete_rows () == 0 && p->counters ().raw_rows == 0,
                 "neither partial raw nor cursor row cached");
        hooks->fail = {};
        auto retry = value (p->request_row (0));
        require (signature (*p, retry) == expected, "retry matches clean complete row");
        auto stats = p->counters ();
        hooks->fail = [] (auto) { return true; };
        require (value (p->request_row (0)).data () == retry.data () &&
                     p->counters ().branches_considered == stats.branches_considered &&
                     p->counters ().raw_rows == stats.raw_rows,
                 "complete cache never rebuilt");
        ++fault_checks;
      }
    }
    auto hooks = std::make_shared<cb::Hooks> ();
    cb::Options o;
    o.hooks = hooks;
    auto p = value (Provider::create (f, dict, o));
    hooks->cancelled = [] { return true; };
    failure (p->request_row (0), cb::FailureKind::cancelled);
    failure (p->materialize (), cb::FailureKind::cancelled);
    hooks->cancelled = {};
    hooks->fail = [] (auto) -> bool { throw std::runtime_error ("injected unexpected failure"); };
    failure (p->request_row (0), cb::FailureKind::unexpected);
    hooks->fail = [] (auto) -> bool { throw std::bad_alloc (); };
    failure (p->request_row (0), cb::FailureKind::memory_limit);
    Owned s {p->get_init_state ()};
    bool adapter_failed = false;
    try {
      p->succ_iter (s.get ());
    } catch (const cb::AdapterFailure& e) {
      adapter_failed = e.failure ().kind == cb::FailureKind::memory_limit;
    }
    require (adapter_failed && !p->is_complete (0),
             "adapter cannot expose decisive empty iterator");
    hooks->fail = {};
    require (signature (*p, value (p->request_row (0))) == expected, "cancel/exception retry");
    failure (p->request_row (std::numeric_limits<cb::StateId>::max ()),
             cb::FailureKind::invalid_state);
    std::cout
        << "transactions: " << fault_checks
        << " injected positions plus cancellation, allocation, exception, adapter failures\n";
  }

  void limits_and_declines () {
    auto dict = spot::make_bdd_dict ();
    for (auto f :
         {F::strong_X (F::ap ("p")), F::eword (), parse ("{p; q}"), parse ("{p[*]} |-> q")})
      failure (Provider::create (f, dict), cb::FailureKind::unsupported_operator);
    cb::Options o;
    o.max_normalization_nodes = 1;
    failure (Provider::create (parse ("F p & F q"), dict, o),
             cb::FailureKind::normalization_limit);
    o = {};
    o.max_normalization_depth = 4;
    failure (Provider::create (F::X (50, F::ap ("p")), dict, o),
             cb::FailureKind::normalization_limit);
    o = {};
    o.max_bytes = 1;
    failure (Provider::create (parse ("p"), dict, o), cb::FailureKind::memory_limit);
    o = {};
    o.max_states = 0;
    failure (Provider::create (parse ("p"), dict, o), cb::FailureKind::state_limit);
    for (unsigned kind = 0; kind < 6; ++kind) {
      o = {};
      cb::FailureKind expected;
      switch (kind) {
        case 0:
          o.max_branches_per_row = 1;
          expected = cb::FailureKind::branch_limit;
          break;
        case 1:
          o.max_guards_per_row = 0;
          expected = cb::FailureKind::guard_limit;
          break;
        case 2:
          o.max_states = 1;
          expected = cb::FailureKind::state_limit;
          break;
        case 3:
          o.max_rows = 0;
          expected = cb::FailureKind::row_limit;
          break;
        case 4:
          o.max_edges_per_row = 1;
          expected = cb::FailureKind::row_limit;
          break;
        default:
          o.max_steps_per_row = 1;
          expected = cb::FailureKind::branch_limit;
          break;
      }
      auto p = make ("F p & F q", o);
      failure (p->request_row (0), expected);
      require (!p->is_complete (0) && p->counters ().raw_rows == 0, "resource failure not cached");
    }
    o = {};
    o.max_live_bdd_nodes = 0;
    failure (Provider::create (parse ("p"), dict, o), cb::FailureKind::guard_limit);
    // Factory can fit while the first row's scratch storage cannot.
    const auto memory_stress = parse ("F a & F b & F c & F d & F e & F f & F g & F h");
    auto baseline = make (memory_stress);
    o = {};
    o.max_bytes = baseline->counters ().retained_bytes * 4;
    auto p = make (memory_stress, o);
    failure (p->request_row (0), cb::FailureKind::memory_limit);
    require (p->complete_rows () == 0, "row scratch memory limit");

    // Nested Boolean abbreviations share a DAG; this would be exponential if
    // the normalizer expanded shared subformulas into independent trees.
    auto dag = F::ap ("p");
    for (unsigned i = 0; i < 30; ++i)
      dag = F::Equiv (dag, F::X (F::ap ("shared" + std::to_string (i))));
    auto shared = make (dag);
    require (shared->closure ().size () < 600, "normalization preserves DAG sharing");
  }

  void stress () {
    for (unsigned n : {4, 6, 8}) {
      std::vector<F> terms;
      for (unsigned i = 0; i < n; ++i)
        terms.push_back (F::F (F::ap ("p" + std::to_string (i))));
      auto formula = F::And (terms);
      auto start = std::chrono::steady_clock::now ();
      auto p = make (formula);
      auto row = value (p->request_row (0));
      auto us = std::chrono::duration_cast<std::chrono::microseconds> (
                    std::chrono::steady_clock::now () - start)
                    .count ();
      require (row.size () == (1u << n), "eventuality first row exponential edge count");
      std::cout << "negative stress: n=" << n
                << " first-row branches=" << p->counters ().branches_considered
                << " edges=" << row.size () << " factory+row_us=" << us << '\n';
      cb::Options limited;
      limited.max_branches_per_row = 4;
      auto capped = make (formula, limited);
      failure (capped->request_row (0), cb::FailureKind::branch_limit);
      require (capped->complete_rows () == 0 && capped->counters ().raw_rows == 0,
               "explosion stops with typed failure");
    }
  }
}

int main () {
  try {
    structure ();
    faults ();
    limits_and_declines ();
    semantics ();
    stress ();
    std::cout << "closure-buchi: PASS, " << assertions << " assertions\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what () << '\n';
  } catch (...) {
    std::cerr << "FAIL: unexpected nonstandard exception\n";
  }
  return 1;
}
