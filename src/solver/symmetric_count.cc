#include "symmetric_count.hh"

#include "controller_check.hh"
#include "encoded_count.hh"
#include "formula_normalize.hh"
#include "parity/game.hh"
#include <unordered_map>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <limits>
#include <map>
#include <set>
#include <spot/tl/apcollect.hh>
#include <spot/tl/parse.hh>
#include <spot/tl/print.hh>
#include <spot/twa/formula2bdd.hh>
#include <spot/twaalgos/game.hh>
#include <spot/twaalgos/isdet.hh>
#include <spot/twaalgos/parity.hh>
#include <spot/twaalgos/translate.hh>
#include <stdexcept>

namespace acacia::symcount {
  namespace {
    using spot::formula;
    using spot::op;

    struct declined : std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    uint64_t now_ns () {
      timespec ts;
      clock_gettime (CLOCK_MONOTONIC, &ts);
      return uint64_t (ts.tv_sec) * 1000000000u + uint64_t (ts.tv_nsec);
    }

    bool deadline_expired (const limits& bound) {
      return bound.deadline_mono_ns and now_ns () >= bound.deadline_mono_ns;
    }

    void check_deadline (const limits& bound) {
      if (deadline_expired (bound))
        throw declined ("deadline");
    }

    // The index family: every family of the chosen size, indexed together.
    struct family_map {
        unsigned n = 0;
        std::map<std::string, std::pair<unsigned, unsigned>> member;  // ap -> (family, index)
        std::vector<std::vector<std::string>> names;                  // [family][index]
    };

    std::set<unsigned> indices (const formula& f, const family_map& fm) {
      std::set<unsigned> found;
      formula walk = f;  // traverse is not const
      walk.traverse ([&] (const formula& g) {
        if (g.is (op::ap))
          if (auto it = fm.member.find (g.ap_name ()); it != fm.member.end ())
            found.insert (it->second.second);
        return false;
      });
      return found;
    }

    void require_aps (const formula& f, const std::set<std::string>& allowed,
                      const char* reason) {
      spot::atomic_prop_set aps;
      spot::atomic_prop_collect (f, &aps);
      for (const auto& a : aps)
        if (not allowed.count (a.ap_name ()))
          throw declined (reason);
    }

    // Rename index k's family members to index 0's: the template of a local formula.
    formula to_template (const formula& f, unsigned k, const family_map& fm) {
      if (f.is (op::ap)) {
        if (auto it = fm.member.find (f.ap_name ());
            it != fm.member.end () and it->second.second == k)
          return formula::ap (fm.names[it->second.first][0]);
        return f;
      }
      formula walk = f;  // map is not const
      return walk.map ([&] (const formula& c) { return to_template (c, k, fm); });
    }

    // Split into conjuncts, pushing G over conjunctions and implication consequents.
    void flatten (const formula& f, std::vector<formula>& out) {
      if (f.is (op::tt))
        return;
      if (f.is (op::And)) {
        for (const auto& c : f)
          flatten (c, out);
        return;
      }
      if (f.is (op::G) and split_globally (f[0], [&] (formula c) { flatten (c, out); }))
        return;
      if (f.is (op::Implies) and f[1].is (op::And)) {
        for (const auto& c : f[1])
          flatten (formula::Implies (f[0], c), out);
        return;
      }
      out.push_back (f);
    }

    // A per-client predicate: a local-automaton mark, entering the sink, a letter,
    // or a since-monitor bit after the step.
    enum class pred_kind { mark, sink, letter, since };
    struct predicate {
        pred_kind kind;
        unsigned mark = 0;  // mark, or the since monitor's index
        bdd letter = bddfalse;
    };
    // Per client, m' = gamma && (m || !lambda): lambda failed since gamma last failed.
    struct since_monitor {
        bdd gamma, lambda;
        unsigned bit;
    };
    // An event over all clients: some now, every one now, an accepted count,
    // or every one since the last breakpoint (which then resets).
    enum class event_kind { any, all_now, all_inf, cardinality };
    struct event {
        event_kind kind;
        unsigned pred;
        unsigned bit = 0;  // breakpoint bit, all_inf only
        formula ap;
    };

    struct builder {
        const family_map& fm;
        const limits& bound;
        std::set<std::string> ap_names;
        unsigned next_event = 0;
        spot::bdd_dict_ptr dict = spot::make_bdd_dict ();
        int owner_token = 0;
        std::vector<predicate> preds;
        std::vector<event> events;
        struct cardinality_constraint {
            unsigned pred;
            std::vector<uint8_t> truth;
            unsigned family;
        };
        std::vector<cardinality_constraint> cardinalities;

        // Recognize a Boolean constraint over one indexed family.
        // Every transposition is checked, then the count predicate is BDD-equated
        // with the original formula before it can enter the game.
        formula boolean_orbit (const formula& f) {
          std::set<unsigned> families;
          bool has_global = false;
          formula walk = f;
          walk.traverse ([&] (const formula& a) {
            if (a.is (op::ap)) {
              auto it = fm.member.find (a.ap_name ());
              if (it == fm.member.end ())
                has_global = true;
              else
                families.insert (it->second.first);
            }
            return false;
          });
          if (has_global or families.size () != 1)
            return {};  // Preserve the existing recognizer for other aggregates.
          const unsigned family = *families.begin ();
          const bdd original = spot::formula_to_bdd (f, dict, &owner_token);
          std::vector<int> vars;
          for (const auto& a : fm.names[family])
            vars.push_back (dict->register_proposition (formula::ap (a), &owner_token));
          for (unsigned i = 0; i < fm.n; ++i)
            for (unsigned j = i + 1; j < fm.n; ++j) {
              check_deadline (bound);
              bddPair* pair = bdd_newpair ();
              bdd_setpair (pair, vars[i], vars[j]);
              bdd_setpair (pair, vars[j], vars[i]);
              const bool equal = bdd_replace (original, pair) == original;
              bdd_freepair (pair);
              if (not equal)
                return {};  // Let the existing recognizer supply its decline reason.
            }
          std::vector<uint8_t> truth (fm.n + 1);
          for (unsigned k = 0; k <= fm.n; ++k) {
            check_deadline (bound);
            bdd assignment = bddtrue;
            for (unsigned i = 0; i < fm.n; ++i)
              assignment &= i < k ? bdd_ithvar (vars[i]) : bdd_nithvar (vars[i]);
            truth[k] = (original & assignment) != bddfalse;
          }
          // Independent cardinality encoding: dynamic programming for exactly k.
          std::vector<bdd> exact (fm.n + 1, bddfalse);
          exact[0] = bddtrue;
          for (unsigned i = 0; i < fm.n; ++i) {
            check_deadline (bound);
            for (int k = int (i + 1); k >= 0; --k)
              exact[k] = (exact[k] & bdd_nithvar (vars[i])) |
                         (k ? exact[k - 1] & bdd_ithvar (vars[i]) : bdd (bddfalse));
          }
          bdd reconstructed = bddfalse;
          for (unsigned k = 0; k <= fm.n; ++k)
            if (truth[k])
              reconstructed |= exact[k];
          if (reconstructed != original)
            throw declined ("cardinality predicate BDD equivalence failed");
          const unsigned p = letter_pred (formula::ap (fm.names[family][0]));
          for (unsigned c = 0; c < cardinalities.size (); ++c)
            if (cardinalities[c].pred == p and cardinalities[c].truth == truth)
              return new_event (event_kind::cardinality, c);
          const unsigned c = cardinalities.size ();
          cardinalities.push_back ({p, truth, family});
          return new_event (event_kind::cardinality, c);
        }

        std::vector<since_monitor> since;
        unsigned bits = 0;

        // Declared first in run (), so every automaton using the dict is gone by now.
        ~builder () { dict->unregister_all_my_variables (&owner_token); }

        unsigned letter_pred (const formula& b) {
          const bdd cond = spot::formula_to_bdd (b, dict, &owner_token);
          for (unsigned p = 0; p < preds.size (); ++p)
            if (preds[p].kind == pred_kind::letter and preds[p].letter == cond)
              return p;
          preds.push_back ({pred_kind::letter, 0, cond});
          return unsigned (preds.size () - 1);
        }

        formula new_event (event_kind kind, unsigned pred) {
          for (const auto& e : events)
            if (e.kind == kind and e.pred == pred)
              return e.ap;
          std::string name;
          do {
            name = "__symcount_e" + std::to_string (next_event++);
          } while (ap_names.count (name));
          event e {kind, pred, kind == event_kind::all_inf ? bits++ : 0u, formula::ap (name)};
          events.push_back (e);
          return e.ap;
        }

        // An aggregate over every index of template t.
        formula atom (bool conj, const formula& t) {
          // Local predicates are evaluated before generated events exist.
          require_aps (t, ap_names, "generated event in a local template");
          if (t.is_boolean ())
            return new_event (conj ? event_kind::all_now : event_kind::any, letter_pred (t));
          if (t.is (op::G) and t[0].is (op::F) and t[0][0].is_boolean ()) {
            const formula b = t[0][0];
            return formula::G (formula::F (
                new_event (conj ? event_kind::all_inf : event_kind::any, letter_pred (b))));
          }
          if (t.is (op::F) and t[0].is (op::G) and t[0][0].is_boolean ()) {
            const formula nb = formula::Not (t[0][0]);
            // && FG b = FG && b = !GF || !b;  || FG b = !(&& GF !b).
            const formula e =
                new_event (conj ? event_kind::any : event_kind::all_inf, letter_pred (nb));
            return formula::F (formula::G (formula::Not (e)));
          }
          throw declined ("aggregate of an unsupported template: " + spot::str_psl (t));
        }

        // The templates of an Or over every index, each `G lambda` with lambda Boolean.
        bool always_orbit (const formula& f, formula& lambda) {
          if (not f.is (op::Or) or f.size () != fm.n)
            return false;
          std::set<unsigned> seen;
          for (const auto& c : f) {
            const auto ix = indices (c, fm);
            if (ix.size () != 1 or not c.is (op::G) or not c[0].is_boolean () or
                not seen.insert (*ix.begin ()).second)
              return false;
            const formula t = to_template (c[0], *ix.begin (), fm);
            if (seen.size () == 1)
              lambda = t;
            else if (t != lambda)
              return false;
          }
          return true;
        }

        // Preserve maximal Boolean constraints before flatten distributes G over And.
        formula boolean_constraints (const formula& f) {
          if (f.is_boolean ()) {
            if (indices (f, fm).size () > 1)
              if (auto replacement = boolean_orbit (f))
                return replacement;
            return f;
          }
          formula walk = f;
          return walk.map ([&] (const formula& c) { return boolean_constraints (c); });
        }

        // Letter aggregates are pointwise; GF/FG aggregates are prefix-independent: valid
        // anywhere.
        formula aggregates (const formula& f, bool cardinality = true) {
          if (indices (f, fm).empty ())
            return f;
          if (f.is_boolean ()) {
            if (cardinality and indices (f, fm).size () > 1)
              if (auto replacement = boolean_orbit (f))
                return replacement;
            cardinality = false;  // Keep the old behavior for the whole rejected constraint.
          }
          if (f.is (op::And) or f.is (op::Or)) {
            const bool conj = f.is (op::And);
            std::map<formula, std::set<unsigned>> groups;
            std::vector<formula> parts;
            for (const auto& c : f) {
              const auto ix = indices (c, fm);
              if (ix.size () == 1) {
                if (not groups[to_template (c, *ix.begin (), fm)].insert (*ix.begin ()).second)
                  throw declined ("an aggregate repeats an index");
              }
              else
                parts.push_back (aggregates (c, cardinality));
            }
            for (const auto& [t, members] : groups) {
              if (members.size () != fm.n)
                throw declined ("an aggregate covers part of the index range");
              parts.push_back (atom (conj, t));
            }
            return conj ? formula::And (parts) : formula::Or (parts);
          }
          if (f.is (op::ap))
            throw declined ("an indexed atom outside an aggregate");
          formula walk = f;  // map is not const
          return walk.map ([&] (const formula& c) { return aggregates (c, cardinality); });
        }
    };

    struct local_automaton {
        unsigned states = 1, sink = 0;  // sink == states when present
        bool has_sink = false;
        unsigned initial = 0;
        // [q][global letter][local letter]
        std::vector<unsigned> next;
        std::vector<uint64_t> holds;  // predicate bitmask
    };

    struct vec_hash {
        size_t operator() (const std::vector<uint32_t>& v) const {
          uint64_t h = 1469598103934665603u;
          for (auto x : v)
            h = (h ^ x) * 1099511628211u;
          return size_t (h);
        }
    };

    bdd minterm (const std::vector<int>& vars, unsigned value) {
      bdd m = bddtrue;
      for (unsigned i = 0; i < vars.size (); ++i)
        m &= (value >> i & 1) ? bdd_ithvar (vars[i]) : bdd_nithvar (vars[i]);
      return m;
    }

    // Compositions of `total` into `parts` ordered parts.
    void compositions (unsigned total, unsigned parts, std::vector<std::vector<uint16_t>>& out,
                       size_t& choices, size_t limit, const limits& bound) {
      if (choices > limit)
        throw declined ("split limit");
      uint64_t count = 1;
      for (unsigned k = 1; k < parts; ++k) {
        count = count * (total + k) / k;
        if (count > limit / choices)
          throw declined ("split limit");
      }
      choices *= size_t (count);
      std::vector<uint16_t> cur (parts, 0);
      auto rec = [&] (auto&& self, unsigned i, unsigned left) -> void {
        if (i + 1 == parts) {
          if ((out.size () & 255) == 0)
            check_deadline (bound);
          cur[i] = uint16_t (left);
          out.push_back (cur);
          return;
        }
        for (unsigned c = 0; c <= left; ++c) {
          cur[i] = uint16_t (c);
          self (self, i + 1, left - c);
        }
      };
      rec (rec, 0, total);
    }

    result run (const spot::formula& spec, const std::vector<std::string>& inputs,
                const std::vector<std::string>& outputs, const family_map& fm,
                const limits& bound, const encoded_alphabet* encoded = nullptr,
                formula raw = {}, bool audit = false) {
      check_deadline (bound);
      result r;
      r.n = fm.n;
      r.encoded = encoded != nullptr;
      if (fm.n > UINT16_MAX)
        throw declined ("index count exceeds uint16_t");
      const auto t0 = std::chrono::steady_clock::now ();
      builder b {fm, bound};
      b.ap_names.insert (inputs.begin (), inputs.end ());
      b.ap_names.insert (outputs.begin (), outputs.end ());
      uint64_t polls = 0;
      const auto poll = [&] {
        if ((++polls & 255) == 0)
          check_deadline (bound);
      };
      const auto table_size = [] (size_t states, size_t letters, size_t extra) {
        constexpr size_t max_entries = size_t (1) << 24;
        if (states > max_entries / letters / extra)
          throw declined ("table limit");
        return states * letters * extra;
      };

      // Partition the APs: global inputs/outputs, then index 0's local inputs/outputs.
      std::vector<std::string> gi, go, li, lo;
      std::set<std::string> in_set (inputs.begin (), inputs.end ());
      for (const auto& a : inputs)
        if (not fm.member.count (a))
          gi.push_back (a);
      for (const auto& a : outputs)
        if (not fm.member.count (a))
          go.push_back (a);
      for (const auto& names : fm.names)
        (in_set.count (names[0]) ? li : lo).push_back (names[0]);
      const unsigned ng = unsigned (gi.size () + go.size ()),
                     nl = unsigned (li.size () + lo.size ());
      std::set<std::string> global_aps (gi.begin (), gi.end ());
      global_aps.insert (go.begin (), go.end ());
      if (ng + nl > 16)
        throw declined ("too many letters for explicit local tables");
      if ((1u << nl) > bound.max_local_letters)
        throw declined ("too many local letters");

      // Only positive top-level G conjuncts over declared outputs can prune moves.
      // Stop at every other operator: F/X/implication/disjunction are not safety.
      const formula lowered = b.boolean_constraints (spec);
      std::vector<std::pair<unsigned, unsigned>> output_safety;  // (constraint, output bit)
      for (const auto& e : b.events) {
        if (e.kind != event_kind::cardinality)
          continue;
        const auto& c = b.cardinalities[e.pred];
        const auto& names = fm.names[c.family];
        if (not std::all_of (names.begin (), names.end (), [&] (const auto& a) {
              return std::find (outputs.begin (), outputs.end (), a) != outputs.end ();
            }))
          continue;
        auto under_top_g = [&] (auto&& self, const formula& f, bool under_g) -> bool {
          if (f.is (op::And)) {
            for (const auto& child : f)
              if (self (self, child, under_g))
                return true;
            return false;
          }
          if (f.is (op::G))
            return self (self, f[0], true);
          return under_g and f == e.ap;
        };
        if (under_top_g (under_top_g, lowered, false)) {
          const auto bit = std::find (lo.begin (), lo.end (), names[0]) - lo.begin ();
          output_safety.emplace_back (e.pred, unsigned (bit));
        }
      }
      // Classify conjuncts, retaining the original objective and its safety monitor.
      std::vector<formula> conjuncts, global;
      flatten (lowered, conjuncts);
      std::map<formula, std::set<unsigned>> local;
      for (const auto& c : conjuncts) {
        const auto ix = indices (c, fm);
        if (ix.empty ())
          global.push_back (c);
        else if (ix.size () == 1) {
          require_aps (c, b.ap_names, "generated event in a local template");
          if (not local[to_template (c, *ix.begin (), fm)].insert (*ix.begin ()).second)
            continue;  // a repeated conjunct
        }
        else {
          formula g, lambda;
          // Only at position 0: G (G gamma -> || G lambda) == GF !gamma || || FG !m.
          // If !gamma stops, one client must keep lambda after its last occurrence (or from 0).
          if (c.is (op::G) and c[0].is (op::Implies) and c[0][0].is (op::G) and
              c[0][0][0].is_boolean () and indices (c[0][0], fm).empty () and
              b.always_orbit (c[0][1], lambda)) {
            const formula gamma = c[0][0][0];
            require_aps (gamma, global_aps, "since antecedent is not over global APs");
            require_aps (lambda, b.ap_names, "generated event in a local template");
            b.since.push_back ({spot::formula_to_bdd (gamma, b.dict, &b.owner_token),
                                spot::formula_to_bdd (lambda, b.dict, &b.owner_token), b.bits++});
            b.preds.push_back ({pred_kind::since, unsigned (b.since.size () - 1), bddfalse});
            const formula e = b.new_event (event_kind::all_inf, unsigned (b.preds.size () - 1));
            g = formula::Or ({formula::G (formula::F (formula::Not (gamma))),
                              formula::F (formula::G (formula::Not (e)))});
          }
          else
            g = b.aggregates (c);
          if (not indices (g, fm).empty ())
            throw declined ("an indexed atom outside an aggregate");
          global.push_back (g);
        }
      }
      std::vector<formula> templates;
      for (const auto& [t, members] : local) {
        if (members.size () != fm.n)
          throw declined ("a local conjunct is not instantiated at every index");
        templates.push_back (t);
      }

      // Local automaton for the conjunction of the templates.
      std::vector<int> gvars, lvars;
      for (const auto* names : {&gi, &go})
        for (const auto& a : *names)
          gvars.push_back (b.dict->register_proposition (formula::ap (a), &b.owner_token));
      for (const auto* names : {&li, &lo})
        for (const auto& a : *names)
          lvars.push_back (b.dict->register_proposition (formula::ap (a), &b.owner_token));
      spot::translator ltr (b.dict);
      ltr.set_type (spot::postprocessor::Generic);
      ltr.set_pref (spot::postprocessor::Deterministic);
      check_deadline (bound);
      const auto laut = ltr.run (formula::And (templates));
      check_deadline (bound);
      if (not spot::is_deterministic (laut))
        throw declined ("no deterministic local automaton");
      std::vector<formula> requirements;
      for (const auto& c : laut->get_acceptance ().top_conjuncts ()) {
        if (c.is_t ())
          continue;
        const auto used = c.used_sets ();
        if (used.count () != 1)
          throw declined ("local acceptance is not a conjunction of Inf/Fin");
        const unsigned m = used.min_set () - 1;
        b.preds.push_back ({pred_kind::mark, m, bddfalse});
        const unsigned p = unsigned (b.preds.size () - 1);
        if (c == spot::acc_cond::acc_code::inf (used))
          requirements.push_back (formula::G (formula::F (b.new_event (event_kind::all_inf, p))));
        else if (c == spot::acc_cond::acc_code::fin (used))
          requirements.push_back (
              formula::F (formula::G (formula::Not (b.new_event (event_kind::any, p)))));
        else
          throw declined ("local acceptance is not a conjunction of Inf/Fin");
      }

      local_automaton la;
      la.states = laut->num_states ();
      la.initial = laut->get_init_state_number ();
      const unsigned gl = 1u << ng, ll = 1u << nl;
      // Fill the tables after all predicates exist; the sink predicate is added lazily.
      std::vector<std::pair<unsigned, spot::acc_cond::mark_t>> step (
          table_size (la.states, gl, ll), {UINT32_MAX, {}});
      for (unsigned q = 0; q < la.states; ++q)
        for (unsigned g = 0; g < gl; ++g)
          for (unsigned x = 0; x < ll; ++x) {
            poll ();
            const bdd m = minterm (gvars, g) & minterm (lvars, x);
            for (const auto& e : laut->out (q))
              if ((m & e.cond) != bddfalse) {
                step[(size_t (q) * gl + g) * ll + x] = {e.dst, e.acc};
                break;
              }
            if (step[(size_t (q) * gl + g) * ll + x].first == UINT32_MAX)
              la.has_sink = true;
          }
      if (la.has_sink) {
        b.preds.push_back ({pred_kind::sink, 0, bddfalse});
        requirements.push_back (formula::G (
            formula::Not (b.new_event (event_kind::any, unsigned (b.preds.size () - 1)))));
      }
      if (b.preds.size () > 64 or b.events.size () > 12)
        throw declined ("too many predicates or events");
      const unsigned qs = la.states + (la.has_sink ? 1 : 0);
      la.sink = la.states;
      la.next.assign (table_size (qs, gl, ll), 0);
      la.holds.assign (table_size (qs, gl, ll), 0);
      for (unsigned q = 0; q < qs; ++q)
        for (unsigned g = 0; g < gl; ++g)
          for (unsigned x = 0; x < ll; ++x) {
            poll ();
            const size_t i = (size_t (q) * gl + g) * ll + x;
            const bool from_sink = q == la.sink and la.has_sink;
            const auto [dst, acc] =
                from_sink ? std::pair {la.sink, spot::acc_cond::mark_t {}} : step[i];
            const bool to_sink = dst == UINT32_MAX;
            la.next[i] = to_sink ? la.sink : dst;
            uint64_t holds = 0;
            const bdd m = minterm (gvars, g) & minterm (lvars, x);
            for (unsigned p = 0; p < b.preds.size (); ++p) {
              const auto& pr = b.preds[p];
              bool v = false;
              if (pr.kind == pred_kind::mark)
                v = not to_sink and acc.has (pr.mark);
              else if (pr.kind == pred_kind::sink)
                v = to_sink or from_sink;
              else if (pr.kind == pred_kind::letter)
                v = (m & pr.letter) != bddfalse;
              if (v)
                holds |= uint64_t (1) << p;
            }
            la.holds[i] = holds;
          }

      // Since monitors: gamma per global letter, lambda per global and local letter.
      std::vector<std::vector<uint8_t>> since_gamma (b.since.size ()),
          since_lambda (b.since.size ());
      for (unsigned j = 0; j < b.since.size (); ++j)
        for (unsigned g = 0; g < gl; ++g) {
          since_gamma[j].push_back ((minterm (gvars, g) & b.since[j].gamma) != bddfalse);
          for (unsigned x = 0; x < ll; ++x) {
            poll ();
            since_lambda[j].push_back (
                (minterm (gvars, g) & minterm (lvars, x) & b.since[j].lambda) != bddfalse);
          }
        }
      std::vector<unsigned> since_pred (b.since.size ());
      for (unsigned p = 0; p < b.preds.size (); ++p)
        if (b.preds[p].kind == pred_kind::since)
          since_pred[b.preds[p].mark] = p;

      // Global parity automaton over the global letters and the events.
      std::vector<formula> all = global;
      all.insert (all.end (), requirements.begin (), requirements.end ());
      std::vector<int> evars;
      for (const auto& e : b.events)
        evars.push_back (b.dict->register_proposition (e.ap, &b.owner_token));
      // Lookahead predicates belong only to the global automaton.
      if (encoded)
        for (auto p : encoded->future)
          evars.push_back (b.dict->register_proposition (p->atom, &b.owner_token));
      if (evars.size () > 12)
        throw declined ("too many global events");
      spot::translator gtr (b.dict);
      gtr.set_type (spot::postprocessor::Parity);
      gtr.set_pref (spot::postprocessor::Deterministic | spot::postprocessor::Complete |
                    spot::postprocessor::Colored);
      check_deadline (bound);
      auto gaut = gtr.run (formula::And (all));
      check_deadline (bound);
      if (not spot::is_deterministic (gaut) or not spot::is_complete (gaut))
        throw declined ("no complete deterministic global parity automaton");
      spot::change_parity_here (gaut, spot::parity_kind_max, spot::parity_style_odd);
      spot::colorize_parity_here (gaut, true);
      const unsigned el = 1u << evars.size ();
      const unsigned gstates = gaut->num_states ();
      std::vector<std::pair<unsigned, unsigned>> gstep (table_size (gstates, gl, el));
      for (unsigned s = 0; s < gstates; ++s)
        for (unsigned g = 0; g < gl; ++g)
          for (unsigned ev = 0; ev < el; ++ev) {
            poll ();
            const bdd m = minterm (gvars, g) & minterm (evars, ev);
            bool found = false;
            for (const auto& e : gaut->out (s))
              if ((m & e.cond) != bddfalse) {
                if (e.acc.count () != 1)
                  throw declined ("global automaton is not colored");
                gstep[(size_t (s) * gl + g) * el + ev] = {e.dst, e.acc.max_set () - 1};
                found = true;
                break;
              }
            if (not found)
              throw declined ("global automaton is incomplete");
          }
      // A global state whose every edge loops back with one colour decides the play
      // whatever the counts do: collapse it to one absorbing vertex.
      std::vector<int> absorbing (gstates, -1);
      for (unsigned s = 0; not encoded and s < gstates; ++s) {
        int col = -1;
        bool loops = true;
        for (unsigned g = 0; g < gl and loops; ++g)
          for (unsigned ev = 0; ev < el and loops; ++ev) {
            poll ();
            const auto [dst, c] = gstep[(size_t (s) * gl + g) * el + ev];
            loops = dst == s and (col < 0 or col == int (c));
            col = int (c);
          }
        if (loops)
          absorbing[s] = col;
      }
      if (b.bits >= 32 or qs > (UINT32_MAX >> b.bits))
        throw declined ("local state limit");
      r.local_states = qs << b.bits;
      r.events = unsigned (b.events.size ());
      r.global_states = gstates;

      // Explore the count game. A state is (global state, counts per local state);
      // an environment vertex also carries the colour of the move into it.
      const unsigned nls = r.local_states, bitmask = (1u << b.bits) - 1;
      using cell = std::tuple<unsigned, unsigned, unsigned>;  // state, letter, count
      const auto advance = [&] (unsigned g, std::span<const cell> cells) {
        uint64_t any = 0, all = ~uint64_t (0);
        std::vector<unsigned> cardinalities (b.cardinalities.size (), 0);
        std::vector<unsigned> moved;
        moved.reserve (cells.size ());
        for (const auto& [ls, x, c] : cells) {
          poll ();
          const unsigned q = ls >> b.bits, bits = ls & bitmask;
          const size_t i = (size_t (q) * gl + g) * ll + x;
          uint64_t h = la.holds[i];
          unsigned nb = bits;
          for (unsigned j = 0; j < b.since.size (); ++j) {
            const unsigned bit = b.since[j].bit;
            const bool m2 =
                since_gamma[j][g] and
                ((bits >> bit & 1) or not since_lambda[j][size_t (g) * ll + x]);
            nb = m2 ? nb | 1u << bit : nb & ~(1u << bit);
            if (m2)
              h |= uint64_t (1) << since_pred[j];
          }
          for (unsigned j = 0; j < b.cardinalities.size (); ++j)
            if (h >> b.cardinalities[j].pred & 1)
              cardinalities[j] += c;
          any |= h;
          all &= h;
          for (const auto& e : b.events)
            if (e.kind == event_kind::all_inf and (h >> e.pred & 1))
              nb |= 1u << e.bit;
          moved.push_back ((la.next[i] << b.bits) | nb);
        }
        unsigned ev = 0, reset = 0;
        for (unsigned k = 0; k < b.events.size (); ++k) {
          const auto& e = b.events[k];
          bool fired = false;
          if (e.kind == event_kind::any)
            fired = any >> e.pred & 1;
          else if (e.kind == event_kind::all_now)
            fired = all >> e.pred & 1;
          else if (e.kind == event_kind::cardinality)
            fired = b.cardinalities[e.pred].truth[cardinalities[e.pred]];
          else {
            fired = std::all_of (moved.begin (), moved.end (),
                                 [&] (unsigned m) { return m >> e.bit & 1; });
            if (fired)
              reset |= 1u << e.bit;
          }
          if (fired)
            ev |= 1u << k;
        }
        for (auto& ls : moved)
          ls &= ~reset;
        return std::pair {std::move (moved), ev};
      };
      const auto feasible_weight = [&] (unsigned constraint, unsigned first, unsigned last) {
        const auto& truth = b.cardinalities[constraint].truth;
        return std::any_of (truth.begin () + first, truth.begin () + last + 1,
                            [] (auto v) { return v != 0; });
      };
      // Charge the count-key copies in the state maps to the vertex budget (256-byte units).
      const size_t max_vertices =
          std::min<size_t> (UINT32_MAX / 2, bound.max_vertices) /
          (1 + (size_t (nls) + (encoded ? 3 : 0)) / 16);
      const size_t max_edges =
          std::min<size_t> (UINT32_MAX / 2, bound.max_edges) / (encoded ? 2 : 1);
      if (not max_vertices)
        throw declined ("state storage limit");
      size_t edges = 0;
      const auto count_edges = [&] (size_t count) {
        if (count > max_edges - edges)
          throw declined ("edge limit");
        edges += count;
      };
      const unsigned nli = 1u << li.size (), nlo = 1u << lo.size ();
      const unsigned ngi = 1u << gi.size (), ngo = 1u << go.size ();
      std::unordered_map<std::vector<uint32_t>, unsigned, vec_hash> env_index;
      std::vector<std::vector<uint32_t>> env_key;  // per vertex: state, colour, counts
      std::vector<uint8_t> owner;                  // 1: system (Spot's controller)
      std::vector<unsigned> colour;                // of the edge into the vertex
      std::vector<std::vector<unsigned>> succ;
      std::vector<unsigned> work;
      auto env_vertex = [&] (std::vector<uint32_t> key) {
        if (absorbing[key[0]] >= 0) {
          key.resize (1);
          key.push_back (uint32_t (absorbing[key[0]]));
        }
        auto [it, fresh] = env_index.try_emplace (key, unsigned (owner.size ()));
        if (fresh) {
          if (owner.size () >= max_vertices)
            throw declined ("vertex limit");
          owner.push_back (0);
          colour.push_back (key[1]);
          succ.emplace_back ();
          env_key.push_back (std::move (key));
          work.push_back (it->second);
        }
        return it->second;
      };
      constexpr size_t header = 5;  // state, incoming priority, buffered letter/events/code
      constexpr auto absent = encoded_alphabet::absent;
      std::vector<std::vector<unsigned>> witnesses;
      std::vector<unsigned> global_bits, local_bits;
      const auto bit_for = [] (const auto& names, const auto& name) -> unsigned {
        const auto i = std::ranges::find (names, name);
        return i == names.end () ? 0 : 1u << (i - names.begin ());
      };
      std::vector<std::vector<unsigned>> selector_bits;
      const auto key_for = [&] (std::span<const unsigned> prefix,
                                 std::span<const unsigned> local) {
        std::vector<uint32_t> key (prefix.begin (), prefix.end ());
        key.resize (header + nls);
        for (auto ls : local)
          ++key[header + ls];
        return key;
      };
      const auto finish_round = [&] (std::span<const unsigned> state,
                                      std::span<const unsigned> local, unsigned word) {
        unsigned g = 0;
        for (unsigned i = 0; i < global_bits.size (); ++i)
          if (word >> i & 1)
            g |= global_bits[i];
        for (auto p : encoded->now)
          if (encoded->table (*p, word, word))
            g |= bit_for (go, p->atom.ap_name ()) << gi.size ();
        std::vector<cell> cells;
        for (auto ls : local)
          cells.emplace_back (ls, 0, 1);
        for (size_t b = 0; b < encoded->buses.size (); ++b) {
          const auto raw_code = encoded->code (word, b);
          const auto [i, mode] = encoded->buses[b].values[raw_code];
          if (mode == 0)
            std::get<1> (cells[i]) |= local_bits[b];
          g |= selector_bits[b][raw_code];
        }
        auto [moved, ev] = advance (g, cells);
        unsigned s = state[0], col = 0;
        const auto step = [&] (unsigned letter, unsigned events) {
          std::tie (s, col) = gstep[(size_t (s) * gl + letter) * el + events];
        };
        // Local runs advance immediately. At t+1, read the complete letter for t.
        std::vector<unsigned> next {s, col, 0, 0, absent};
        if (encoded->future.empty ())
          step (g, ev);
        else {
          if (state[4] != absent) {
            unsigned events = state[3];
            for (size_t k = 0; k < encoded->future.size (); ++k)
              if (encoded->table (*encoded->future[k], state[4], word))
                events |= 1u << (b.events.size () + k);
            step (state[2], events);
          }
          next[2] = g;
          next[3] = ev;
          next[4] = word & encoded->history_mask;
        }
        next[0] = s;
        next[1] = col;
        return std::pair {std::move (next), std::move (moved)};
      };
      const auto legal_weight = [] (size_t, unsigned weight) { return weight <= 1; };
      const auto safe_weight = [&] (size_t bus, unsigned weight) {
        return legal_weight (bus, weight)
               and std::ranges::all_of (output_safety, [&] (const auto& item) {
                     const auto [constraint, bit] = item;
                     return (local_bits[bus] >> li.size ()) != (1u << bit)
                            or feasible_weight (constraint, weight, weight);
                   });
      };
      const std::vector<unsigned> initial_encoded {
        gaut->get_init_state_number (), 0, 0, 0, absent};
      const std::vector<unsigned> initial_local (encoded ? fm.n : 0, la.initial << b.bits);
      if (encoded) {
        for (const auto& name : encoded->raw_names)
          global_bits.push_back (bit_for (gi, name) | (bit_for (go, name) << gi.size ()));
        for (size_t b = 0; b < encoded->buses.size (); ++b) {
          const auto& bus = encoded->cert.buses[b];
          const auto& name = bus.selectors[0].ap_name ();
          local_bits.push_back (bit_for (li, name) | (bit_for (lo, name) << li.size ()));
          auto& bits = selector_bits.emplace_back (encoded->buses[b].radix, 0);
          for (size_t i = fm.n; i < bus.codes.size (); ++i) {
            const auto& name = bus.selectors[i].ap_name ();
            bits[bus.codes[i]] = bit_for (gi, name) | (bit_for (go, name) << gi.size ());
          }
        }
        env_vertex (key_for (initial_encoded, initial_local));
        const size_t candidate_limit = std::min (max_vertices, max_edges);
        while (not work.empty ()) {
          poll ();
          const auto v = work.back ();
          work.pop_back ();
          const auto state = env_key[v];
          if (witnesses.size () <= v)
            witnesses.resize (v + 1);
          std::vector<unsigned> local;
          for (unsigned ls = 0; ls < nls; ++ls)
            local.insert (local.end (), state[header + ls], ls);
          size_t candidates = 0;
          encoded->moves (true, state[4], 0, local, poll, legal_weight, [&] (unsigned in) {
            if (++candidates > candidate_limit or owner.size () >= max_vertices)
              throw declined ("input split limit");
            count_edges (1);
            const unsigned u = owner.size ();
            owner.push_back (1);
            colour.push_back (0);
            succ.emplace_back ();
            env_key.emplace_back ();
            if (witnesses.size () <= u)
              witnesses.resize (u + 1);
            succ[v].push_back (u);
            witnesses[v].push_back (in);
            std::set<unsigned> successors;
            size_t output_candidates = 0;
            const auto emit = [&] (unsigned word) {
              if (++output_candidates > candidate_limit)
                throw declined ("output split limit");
              auto [next, moved] = finish_round (state, local, word);
              encoded->canonicalize (moved, next[4]);
              const auto w = env_vertex (key_for (next, moved));
              if (successors.insert (w).second) {
                count_edges (1);
                succ[u].push_back (w);
                witnesses[u].push_back (word & encoded->owner_mask (false));
              }
            };
            encoded->moves (false, state[4], in, local, poll, safe_weight, emit);
            if (succ[u].empty ()) // An original legal letter for the safety monitor to reject.
              emit (in);
          });
          ++r.count_states;
        }
      } else {
        {
          std::vector<uint32_t> key {gaut->get_init_state_number (), 0};
          key.resize (2 + nls, 0);
          key[2 + (la.initial << b.bits)] = fm.n;
          env_vertex (std::move (key));
        }
        std::unordered_map<std::vector<uint32_t>, std::vector<unsigned>, vec_hash> sys_of_state;
        while (not work.empty ()) {
          poll ();
          const unsigned v = work.back ();
          work.pop_back ();
          if (absorbing[env_key[v][0]] >= 0) {
            count_edges (1);
            succ[v] = {v};  // its edges keep the sink's colour
            continue;
          }
          std::vector<uint32_t> state = env_key[v];
          state.erase (state.begin () + 1);  // drop the colour: (global, counts...)
          if (auto known = sys_of_state.find (state); known != sys_of_state.end ()) {
            count_edges (known->second.size ());
            succ[v] = known->second;
            continue;
          }
          const unsigned s = state[0];
          std::vector<unsigned> occupied;
          for (unsigned ls = 0; ls < nls; ++ls) {
            poll ();
            if (state[1 + ls])
              occupied.push_back (ls);
          }
          // Environment: a global input and, per occupied local state, a split by local input.
          std::vector<std::vector<std::vector<uint16_t>>> in_splits (occupied.size ());
          size_t input_choices = ngi;
          for (unsigned k = 0; k < occupied.size (); ++k)
            compositions (state[1 + occupied[k]], nli, in_splits[k], input_choices,
                          std::min (max_vertices - owner.size (), max_edges - edges), bound);
          std::vector<unsigned> sys_vertices;
          std::vector<unsigned> pick (occupied.size (), 0);
          for (unsigned g_in = 0; g_in < ngi; ++g_in) {
            std::fill (pick.begin (), pick.end (), 0);
            for (;;) {
              poll ();
              // One system vertex for (state, g_in, input split).
              const unsigned u = unsigned (owner.size ());
              if (u >= max_vertices)
                throw declined ("vertex limit");
              owner.push_back (1);
              colour.push_back (0);
              succ.emplace_back ();
              env_key.emplace_back ();
              sys_vertices.push_back (u);
              // Classes (local state, local input) with their counts.
              std::vector<std::tuple<unsigned, unsigned, unsigned>> cls;
              for (unsigned k = 0; k < occupied.size (); ++k)
                for (unsigned x = 0; x < nli; ++x)
                  if (unsigned c = in_splits[k][pick[k]][x])
                    cls.emplace_back (occupied[k], x, c);
              std::vector<std::vector<uint16_t>> output (cls.size (), std::vector<uint16_t> (nlo));
              std::set<unsigned> successors;
              size_t candidates = 0;
              auto emit = [&] (unsigned g_out) {
                poll ();
                if (++candidates > std::min (max_vertices, max_edges))
                  throw declined ("split limit");
                const unsigned g = g_in | (g_out << gi.size ());
                std::vector<uint32_t> counts (nls, 0);
                std::vector<cell> cells;
                for (unsigned k = 0; k < cls.size (); ++k) {
                  const auto [ls, x, total] = cls[k];
                  for (unsigned y = 0; y < nlo; ++y)
                    if (const auto c = output[k][y])
                      cells.emplace_back (ls, x | (y << li.size ()), c);
                }
                const auto [moved, ev] = advance (g, cells);
                for (size_t k = 0; k < cells.size (); ++k)
                  counts[moved[k]] += std::get<2> (cells[k]);
                const auto [s2, col] = gstep[(size_t (s) * gl + g) * el + ev];
                std::vector<uint32_t> key {s2, col};
                key.insert (key.end (), counts.begin (), counts.end ());
                const unsigned w = env_vertex (std::move (key));
                if (not successors.count (w)) {
                  count_edges (1);
                  successors.insert (w);
                }
              };
              if (output_safety.empty ()) {
                std::vector<std::vector<std::vector<uint16_t>>> out_splits (cls.size ());
                size_t output_choices = ngo;
                for (unsigned k = 0; k < cls.size (); ++k)
                  compositions (std::get<2> (cls[k]), nlo, out_splits[k], output_choices,
                                std::min (max_vertices - owner.size (), max_edges - edges), bound);
                std::vector<unsigned> opick (cls.size (), 0);
                for (unsigned g_out = 0; g_out < ngo; ++g_out) {
                  std::fill (opick.begin (), opick.end (), 0);
                  for (;;) {
                    for (unsigned k = 0; k < cls.size (); ++k)
                      output[k] = out_splits[k][opick[k]];
                    emit (g_out);
                    unsigned k = 0;
                    while (k < cls.size () and ++opick[k] == out_splits[k].size ())
                      opick[k++] = 0;
                    if (k == cls.size ())
                      break;
                  }
                }
              }
              else {
                // Enumerate compositions directly. A partial assignment is extended
                // only if each accepted-weight set intersects its reachable interval.
                // Complete callbacks therefore always satisfy every output G predicate.
                std::vector<unsigned> weight (output_safety.size (), 0);
                auto rec = [&] (auto&& self, unsigned k, unsigned y, unsigned left,
                                unsigned remaining, unsigned g_out) -> void {
                  poll ();
                  if (k == cls.size ()) {
                    emit (g_out);
                    return;
                  }
                  const unsigned first = y + 1 == nlo ? left : 0;
                  for (unsigned c = first; c <= left; ++c) {
                    output[k][y] = uint16_t (c);
                    bool feasible = true;
                    for (unsigned j = 0; j < output_safety.size (); ++j) {
                      const auto [constraint, bit] = output_safety[j];
                      if (y >> bit & 1)
                        weight[j] += c;
                      feasible = feasible and feasible_weight (constraint, weight[j],
                                                                weight[j] + remaining - c);
                    }
                    if (feasible) {
                      if (y + 1 < nlo)
                        self (self, k, y + 1, left - c, remaining - c, g_out);
                      else if (k + 1 < cls.size ())
                        self (self, k + 1, 0, std::get<2> (cls[k + 1]), remaining - c, g_out);
                      else
                        self (self, k + 1, 0, 0, remaining - c, g_out);
                    }
                    for (unsigned j = 0; j < output_safety.size (); ++j)
                      if (y >> output_safety[j].second & 1)
                        weight[j] -= c;
                  }
                };
                for (unsigned g_out = 0; g_out < ngo; ++g_out)
                  rec (rec, 0, 0, std::get<2> (cls[0]), fm.n, g_out);
              }
              // Keep one original move when no safe output survives. The retained
              // unconditional G monitor rejects it; every stored edge stays original.
              if (successors.empty ()) {
                for (unsigned k = 0; k < cls.size (); ++k) {
                  std::fill (output[k].begin (), output[k].end (), 0);
                  output[k][0] = uint16_t (std::get<2> (cls[k]));
                }
                emit (0);
              }
              // Store only distinct successors, with no duplicate candidate capacity.
              succ[u].assign (successors.begin (), successors.end ());
              // Next input split.
              unsigned k = 0;
              while (k < occupied.size () and ++pick[k] == in_splits[k].size ())
                pick[k++] = 0;
              if (k == occupied.size ())
                break;
            }
          }
          count_edges (sys_vertices.size ());
          succ[v] = sys_vertices;
          r.count_states++;
          sys_of_state.emplace (std::move (state), std::move (sys_vertices));
        }
      }
      r.vertices = owner.size ();
      for (const auto& s : succ)
        r.edges += s.size ();
      r.build_seconds =
          std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();

      // Solve with Spot, then check the strategy independently.
      const auto t1 = std::chrono::steady_clock::now ();
      auto arena = spot::make_twa_graph (b.dict);
      arena->new_states (unsigned (owner.size ()));
      arena->set_init_state (0);
      unsigned max_colour = 0;
      for (unsigned c : colour)
        max_colour = std::max (max_colour, c);
      const unsigned sets = max_colour + 1;
      arena->set_acceptance (sets, spot::acc_cond::acc_code::parity_max_odd (sets));
      for (unsigned v = 0; v < owner.size (); ++v)
        for (unsigned w : succ[v]) {
          poll ();
          arena->new_edge (v, w, bddtrue, {colour[w]});
        }
      std::vector<bool> players (owner.begin (), owner.end ());
      spot::set_state_players (arena, players);
      check_deadline (bound);
      const bool system_wins = spot::solve_parity_game (arena, true);
      check_deadline (bound);
      r.solve_seconds =
          std::chrono::duration<double> (std::chrono::steady_clock::now () - t1).count ();
      const auto t2 = std::chrono::steady_clock::now ();
      parity::arena_game checked (arena);
      const auto solution = checked.extract ();
      check_deadline (bound);
      std::string why;
      if (not parity::check_strategy (checked.explicit_game, solution, &why))
        throw declined ("strategy check failed: " + why);
      if (solution.winner[0] != (system_wins ? 1 : 0))
        throw declined ("solver and strategy disagree on the initial vertex");
      if (audit) {
        // Replay with concrete local states. Canonicalize the full prefix at
        // the vertex where the winner moves, then invert that transporter.
        const unsigned input_mask = encoded->owner_mask (true);
        const unsigned output_mask = encoded->owner_mask (false);
        const unsigned output_step = input_mask + 1;
        using raw_state = std::vector<unsigned>;
        const auto lookup = [&] (const raw_state& state, unsigned* in = nullptr) {
          raw_state prefix (state.begin (), state.begin () + header);
          raw_state local (state.begin () + header, state.end ());
          auto p = encoded->canonicalize (local, prefix[4], in);
          auto found = env_index.find (key_for (prefix, local));
          if (found == env_index.end ())
            throw declined ("raw state missing from the quotient");
          const auto v = found->second;
          if (not in)
            return std::pair {v, std::move (p)};
          const auto at = std::ranges::find (witnesses[v], *in);
          if (at == witnesses[v].end ())
            throw declined ("raw input missing from the quotient");
          return std::pair {succ[v][at - witnesses[v].begin ()], std::move (p)};
        };
        const auto untransport = [&] (unsigned word, auto p, unsigned mask) {
          auto forward = p.coord;
          for (unsigned i = 0; i < forward.size (); ++i)
            p.coord[forward[i]] = i;
          return encoded->apply (word, mask, p, true);
        };
        const auto chosen = [&] (unsigned v, const auto& p, unsigned mask) {
          const auto edge = solution.strategy[v];
          if (edge == parity::no_vertex)
            throw declined ("missing winning letter");
          const auto w = checked.explicit_game.out (edge).front ();
          const auto at = std::ranges::find (succ[v], w);
          if (at == succ[v].end ())
            throw declined ("missing winning witness");
          return untransport (witnesses[v][at - succ[v].begin ()], p, mask);
        };
        const auto step = [&] (const raw_state& state, unsigned word) {
          auto [next, local] = finish_round (
              std::span (state).first (header), std::span (state).subspan (header), word);
          next.insert (next.end (), local.begin (), local.end ());
          return next;
        };
        auto initial = initial_encoded;
        initial.insert (initial.end (), initial_local.begin (), initial_local.end ());
        const auto safe_word = [&] (unsigned word) {
          for (size_t b = 0; b < encoded->buses.size (); ++b)
            if (not encoded->cert.buses[b].is_input) {
              const auto val = encoded->buses[b].values[encoded->code (word, b)];
              if (not safe_weight (b, unsigned (val.mode == 0)))
                return false;
            }
          return true;
        };
        bool fallback = true;
        for (unsigned out = 0; out <= output_mask and fallback; out += output_step) {
          poll ();
          fallback = not safe_word (out);
        }
        const auto replay = [&] (bool exhaustive) {
          auto controller = spot::make_twa_graph (b.dict);
          controller->set_acceptance (0, spot::acc_cond::acc_code::t ());
          std::vector<int> vars;
          for (const auto& a : encoded->raw_names)
            vars.push_back (controller->register_ap (a));
          // A counterstrategy can use any legal input after a violation of the
          // retained unconditional safety clause. Use the zero input forever.
          const unsigned sink_count =
              not exhaustive and not system_wins and not output_safety.empty ();
          if (sink_count) {
            controller->new_state ();
            bdd zero = bddtrue;
            for (unsigned k = 0; k < encoded->input_width; ++k)
              zero &= bdd_nithvar (vars[k]);
            controller->new_edge (0, 0, zero);
          }
          std::unordered_map<raw_state, unsigned, vec_hash> index;
          std::vector<raw_state> queue;
          const auto visit = [&] (raw_state target) {
            auto [at, fresh] = index.try_emplace (target, queue.size ());
            if (fresh) {
              if (queue.size () >= max_vertices / (1 + fm.n / 16))
                throw declined ("concrete state limit");
              queue.push_back (std::move (target));
              controller->new_state ();
            }
            return at->second + sink_count;
          };
          controller->set_init_state (visit (initial));
          size_t concrete_edges = 0;
          for (size_t i = 0; i < queue.size (); ++i) {
            const auto state = queue[i];
            const auto [v, p] = lookup (state);
            const auto first_in = exhaustive or system_wins ? 0 : chosen (v, p, input_mask);
            const auto last_in = exhaustive or system_wins ? input_mask : first_in;
            std::set<unsigned> inputs_seen;
            for (unsigned in = first_in; in <= last_in; ++in) {
              poll ();
              unsigned canonical_in = in;
              const auto [u, q] = lookup (state, &canonical_in);
              inputs_seen.insert (u);
              const auto first_out =
                  exhaustive or not system_wins ? 0 : chosen (u, q, output_mask);
              const auto last_out = exhaustive or not system_wins ? output_mask : first_out;
              const auto default_out = exhaustive and fallback ? untransport (0, q, output_mask) : 0;
              std::set<unsigned> outputs_seen;
              for (unsigned out = first_out; out <= last_out; out += output_step) {
                poll ();
                if (++concrete_edges > max_edges)
                  throw declined ("concrete edge limit");
                const auto word = in | out;
                if (exhaustive and (fallback ? out != default_out : not safe_word (word)))
                  continue;
                if (sink_count and not safe_word (word)) {
                  controller->new_edge (i + sink_count, 0, minterm (vars, word));
                  continue;
                }
                auto next = step (state, word);
                const auto w = lookup (next).first;
                if (exhaustive) {
                  if (std::ranges::find (succ[u], w) == succ[u].end ())
                    throw declined ("raw output or priority missing from the quotient");
                  outputs_seen.insert (w);
                  visit (std::move (next));
                } else {
                  if (solution.winner[w] != system_wins)
                    throw declined ("lifted edge leaves the winning region");
                  const auto dst = visit (std::move (next));
                  controller->new_edge (i + sink_count, dst, minterm (vars, word));
                }
              }
              if (exhaustive and outputs_seen.size () != succ[u].size ())
                throw declined ("quotient output has no raw witness");
            }
            if (exhaustive and inputs_seen.size () != succ[v].size ())
              throw declined ("quotient input has no raw witness");
          }
          return controller;
        };
        replay (true);
        const auto controller = replay (false);
        spot::translator translator (b.dict);
        const auto bad = translator.run (system_wins ? formula::Not (raw) : raw);
        if (not synthesis::controller_satisfies (bad, controller))
          throw declined ("lifted strategy fails the converted objective");
        r.lifted = true;
      }
      r.check_seconds =
          std::chrono::duration<double> (std::chrono::steady_clock::now () - t2).count ();
      check_deadline (bound);
      r.outcome = system_wins ? verdict::realizable : verdict::unrealizable;
      return r;
    }
  }  // namespace

  result solve (const spot::formula& spec, const std::vector<std::string>& inputs,
                const std::vector<std::string>& outputs,
                std::span<const symmetry::declared_family> families, const limits& bound) {
    if (deadline_expired (bound))
      return {verdict::declined, "deadline"};
    // Try each family size, largest first; families of that size are indexed together.
    std::set<unsigned, std::greater<>> sizes;
    for (const auto& f : families)
      if (f.members.size () >= 2)
        sizes.insert (unsigned (f.members.size ()));
    result last;
    last.reason = "no indexed family";
    for (unsigned n : sizes) {
      family_map fm;
      fm.n = n;
      for (const auto& f : families) {
        if (f.members.size () != n)
          continue;
        const unsigned j = unsigned (fm.names.size ());
        fm.names.push_back (f.members);
        for (unsigned k = 0; k < n; ++k)
          fm.member[f.members[k]] = {j, k};
      }
      try {
        return run (spec, inputs, outputs, fm, bound);
      } catch (const declined& d) {
        last = result {};
        last.n = n;
        last.reason = d.what ();
        if (deadline_expired (bound))
          return {verdict::declined, "deadline"};
      }
    }
    return last;
  }

  result solve_encoded (const std::string& objective, const std::vector<std::string>& inputs,
                        const std::vector<std::string>& outputs,
                        const symmetry::encoded_certificate& cert, const limits& bound,
                        bool audit) {
    try {
      check_deadline (bound);
      if (objective != cert.objective or inputs != cert.inputs or outputs != cert.outputs)
        throw declined ("encoded certificate objective or partition mismatch");
      const auto raw_width = inputs.size () + outputs.size ();
      if (raw_width >= std::numeric_limits<unsigned>::digits or cert.moving > UINT16_MAX)
        throw declined ("encoded alphabet width limit");
      if ((size_t {1} << raw_width) > bound.max_edges)
        throw declined ("raw alphabet limit");
      const encoded_alphabet alphabet (cert);
      family_map fm;
      fm.n = cert.moving;
      for (const auto& bus : cert.buses) {
        auto& names = fm.names.emplace_back ();
        for (unsigned i = 0; i < fm.n; ++i) {
          names.push_back (bus.selectors[i].ap_name ());
          fm.member.emplace (names.back (), std::pair {fm.names.size () - 1, i});
        }
      }
      const auto raw = spot::parse_infix_psl (objective);
      if (not raw.f or not raw.errors.empty ())
        throw declined ("invalid encoded objective");
      return run (cert.abstract, alphabet.inputs, alphabet.outputs, fm, bound,
                  &alphabet, raw.f, audit);
    } catch (const declined& d) {
      return {verdict::declined, d.what ()};
    } catch (const std::bad_alloc&) {
      return {verdict::declined, "resource"};
    }
  }
}  // namespace acacia::symcount
