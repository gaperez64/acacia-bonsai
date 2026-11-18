#include "solver/closure_recurrence_provider.hh"

#include "solver/closure_buchi_common.hh"

namespace acacia::closure_recurrence {
  namespace {
    using namespace closure_buchi;
    using namespace closure_buchi::detail;
    constexpr auto absent = std::numeric_limits<std::size_t>::max ();

    // Exact lowered forms: GF b, or G(F b1 & ... & F bn). Do not visit
    // their internal U nodes: an independent F b still gets its own watch.
    std::vector<Formula> events (Formula f) {
      if (!f.is (Op::R) || !f[0].is_ff ())
        return {};
      std::vector<Formula> members;
      const auto add = [&] (Formula a) {
        if (!a.is (Op::U) || !a[0].is_tt () || !a[1].is_boolean ())
          return false;
        members.push_back (a[1]);
        return true;
      };
      if (f[1].is (Op::And)) {
        for (auto a : f[1])
          if (!add (a))
            return {};
      }
      else if (!add (f[1]))
        return {};
      std::sort (members.begin (), members.end ());
      members.erase (std::unique (members.begin (), members.end ()), members.end ());
      return members;
    }

    struct Builder {
        Options options;
        Counters& stats;
        spot::twa_graph_ptr graph;
        std::size_t steps = 0, retained = 0;

        void step () {
          check (options);
          if (steps++ >= options.max_steps_per_row)
            fail (FailureKind::branch_limit);
        }
        void retain (std::size_t n) {
          bytes (options, retained, n);
          retained += n;
        }
        unsigned state () {
          hit (options, FaultPoint::interning);
          if (graph->num_states () >= options.max_states ||
              graph->num_states () == std::numeric_limits<unsigned>::max ())
            fail (FailureKind::state_limit);
          retain (128);
          const auto id = graph->new_state ();
          stats.states = graph->num_states ();
          return id;
        }
        void edge (unsigned src, unsigned dst, bdd guard, bool accepting) {
          step ();
          if (graph->num_edges () >= options.max_edges_per_row)
            fail (FailureKind::row_limit);
          retain (sizeof (spot::twa_graph::edge_storage_t));
          graph->new_edge (src, dst, guard,
                           accepting ? spot::acc_cond::mark_t {0} : spot::acc_cond::mark_t {});
          stats.edges = graph->num_edges ();
        }

        // Factor before NNF erases implication/equivalence structure. Formula
        // constructors provide only syntactic, language-preserving rewrites.
        void clauses (Formula f, std::vector<Formula>& out, std::size_t depth = 0) {
          step ();
          if (depth > options.max_normalization_depth)
            fail (FailureKind::normalization_limit);
          if (f.is (Op::And)) {
            for (auto a : f)
              clauses (a, out, depth + 1);
          }
          else if (f.is (Op::Equiv)) {
            clauses (Formula::Implies (f[0], f[1]), out, depth + 1);
            clauses (Formula::Implies (f[1], f[0]), out, depth + 1);
          }
          else if (f.is (Op::Implies)) {
            std::vector<Formula> consequents;
            clauses (f[1], consequents, depth + 1);
            for (auto c : consequents) {
              if (out.size () >= options.max_normalization_nodes)
                fail (FailureKind::normalization_limit);
              out.push_back (Formula::Implies (f[0], c));
            }
          }
          else {
            if (out.size () >= options.max_normalization_nodes)
              fail (FailureKind::normalization_limit);
            out.push_back (f);
          }
        }

        struct Alternative {
            Set next;
            bool postponed;
            bdd guard;
        };
        struct Branch {
            Builder& owner;
            std::vector<Formula> closure;
            std::unordered_map<Formula, ClosureId> ids;
            std::map<Formula, Formula> grouped;
            std::map<Formula, std::vector<Formula>> persistence_groups;
            std::map<Formula, bdd> guards;
            struct Persistence {
                Formula condition;
                ClosureId next, committed;
                std::size_t watch_owner;
            };
            std::map<ClosureId, Persistence> persistence;
            struct Watch {
                ClosureId obligation;
                Formula event;
            };
            std::vector<Watch> watches;
            std::vector<Set> reachable;
            std::vector<Set> relevant;
            std::set<ClosureId> blocks;
            using Key = std::pair<Set, std::size_t>;
            using AltKey = std::pair<Set, bool>;
            using Alternatives = std::map<AltKey, bdd>;
            std::map<std::pair<ClosureId, std::size_t>, Alternatives> memo;
            std::map<Set, Alternatives> conjunctions;
            std::map<Key, std::vector<Alternative>> expansions;
            std::map<Key, unsigned> states;

            Formula group (Formula f) {
              owner.step ();
              if (auto p = grouped.find (f); p != grouped.end ())
                return p->second;
              auto result = f;
              if (!f.is_boolean () && events (f).empty ()) {
                result = f.map ([&] (Formula c) { return group (c); });
                if (result.is (Op::And)) {
                  std::vector<Formula> rest, members;
                  for (auto c : result) {
                    auto es = events (c);
                    if (es.empty ())
                      rest.push_back (c);
                    else
                      for (auto e : es)
                        members.push_back (Formula::U (Formula::tt (), e));
                  }
                  if (!members.empty ())
                    rest.push_back (Formula::R (Formula::ff (), Formula::And (members)));
                  result = Formula::And (rest);
                }
                else if (result.is (Op::Or)) {
                  // The normalizer has lowered FG c to true U (false R c).
                  // Factor sibling guesses into F(G c1 | ... | G cm), used as
                  // an opaque block below; a lone FG keeps its usual expansion.
                  std::vector<Formula> rest, members, conditions;
                  for (auto c : result) {
                    if (c.is (Op::U) && c[0].is_tt () && c[1].is (Op::R) &&
                        c[1][0].is_ff () && c[1][1].is_boolean ()) {
                      members.push_back (c[1]);
                      conditions.push_back (c[1][1]);
                    }
                    else
                      rest.push_back (c);
                  }
                  if (members.size () >= 2) {
                    const auto block = Formula::U (Formula::tt (), Formula::Or (members));
                    owner.retain (96 + conditions.size () * sizeof (Formula));
                    persistence_groups.emplace (block, std::move (conditions));
                    rest.push_back (block);
                    result = Formula::Or (rest);
                  }
                }
              }
              owner.retain (96);
              grouped.emplace (f, result);
              return result;
            }

            ClosureId node (Formula f) {
              if (closure.size () >= owner.options.max_normalization_nodes)
                fail (FailureKind::normalization_limit);
              const auto id = static_cast<ClosureId> (closure.size ());
              closure.push_back (f);
              owner.retain (160);
              return id;
            }

            ClosureId intern_formula (Formula f) {
              owner.step ();
              if (auto p = ids.find (f); p != ids.end ())
                return p->second;
              const auto id = node (f);
              ids.emplace (f, id);
              auto es = events (f);
              if (!es.empty ()) {
                ++owner.stats.blocks;
                blocks.insert (id);
                for (auto e : es)
                  watches.push_back ({id, e});
              }
              else if (auto p = persistence_groups.find (f); p != persistence_groups.end ()) {
                ++owner.stats.blocks;
                watches.push_back ({id, {}});
                const auto n = static_cast<ClosureId> (p->second.size ());
                for (std::size_t i = 1; i < 2 * std::size_t (n); ++i) {
                  owner.step ();
                  node (f);
                }
                for (ClosureId w = 0; w < n; ++w) {
                  owner.step ();
                  const auto next = (w + 1) % n;
                  persistence.emplace (id + w, Persistence {p->second[w], id + next, id + n + w, id});
                  persistence.emplace (id + n + w,
                                       Persistence {p->second[w], id + n + next, id + n + w, absent});
                  blocks.insert (id + w);
                  blocks.insert (id + n + w);
                  owner.retain (2 * (128 + sizeof (Persistence)));
                }
              }
              else {
                if (f.is (Op::U))
                  watches.push_back ({id, {}});
                for (auto c : f)
                  intern_formula (c);
              }
              return id;
            }

            std::size_t obligation (ClosureId id) const {
              const auto p = persistence.find (id);
              return p == persistence.end () ? id : p->second.watch_owner;
            }

            Set can_pending (ClosureId id) {
              owner.step ();
              Set result;
              for (std::size_t i = 0; i < watches.size (); ++i) {
                owner.step ();
                if (watches[i].obligation == obligation (id))
                  insert (result, static_cast<ClosureId> (i));
              }
              if (!blocks.contains (id))
                for (auto c : closure[id])
                  for (auto w : reachable[ids.at (c)]) {
                    owner.step ();
                    insert (result, w);
                  }
              return result;
            }

            explicit Branch (Builder& b, Formula f) : owner (b) {
              intern_formula (group (f));
              owner.stats.watches += watches.size ();
              reachable.resize (closure.size ());
              relevant.resize (closure.size ());
              // Shared DAG nodes need a dependency traversal, not an assumption
              // that every child was assigned a larger interned ID.
              std::vector<bool> done (closure.size ());
              std::function<void (ClosureId)> visit = [&] (ClosureId id) {
                if (done[id])
                  return;
                done[id] = true;
                if (!blocks.contains (id))
                  for (auto c : closure[id])
                    visit (ids.at (c));
                reachable[id] = can_pending (id);
                for (auto w : reachable[id])
                  if (watches[w].obligation == obligation (id))
                    insert (relevant[id], w);
                if (!blocks.contains (id) && !closure[id].is (Op::X))
                  for (auto c : closure[id])
                    for (auto w : relevant[ids.at (c)]) {
                      owner.step ();
                      insert (relevant[id], w);
                    }
                owner.retain ((reachable[id].size () + relevant[id].size ()) * sizeof (ClosureId));
              };
              for (ClosureId id = 0; id < closure.size (); ++id)
                visit (id);
            }

            bdd boolean (Formula f) {
              owner.step ();
              if (auto p = guards.find (f); p != guards.end ())
                return p->second;
              hit (owner.options, FaultPoint::guard);
              bdd result;
              switch (f.kind ()) {
                case Op::tt: result = bddtrue; break;
                case Op::ff: result = bddfalse; break;
                case Op::ap: result = bdd_ithvar (owner.graph->get_dict ()->varnum (f)); break;
                case Op::Not: result = !boolean (f[0]); break;
                case Op::And:
                case Op::Or:
                  result = f.is (Op::And) ? bdd (bddtrue) : bdd (bddfalse);
                  for (auto c : f)
                    result = f.is (Op::And) ? result & boolean (c) : result | boolean (c);
                  break;
                default: fail (FailureKind::unsupported_operator);
              }
              check (owner.options);
              owner.retain (64);
              guards.emplace (f, result);
              return result;
            }

            const std::vector<Alternative>& expand (const Set& pending, std::size_t watch) {
              owner.steps = 0;  // the step limit is per row, as in closure_buchi
              const Key key {pending, watch};
              if (auto p = expansions.find (key); p != expansions.end ())
                return p->second;
              if (expansions.size () >= owner.options.max_rows)
                fail (FailureKind::row_limit);
              if (owner.options.max_branches_per_row == 0)
                fail (FailureKind::branch_limit);
              // Share results across rows and across irrelevant watches.
              // Merge each union/product before using it in the next product.
              std::size_t branches = 0, guard_ops = 0, scratch = 0;
              const auto entry_bytes = [] (const AltKey& k) {
                return 96 + k.first.capacity () * sizeof (ClosureId);
              };
              const auto retain = [&] (const Alternatives& result) {
                for (const auto& [k, guard] : result) {
                  scratch -= entry_bytes (k);
                  owner.retain (entry_bytes (k));
                }
              };
              const auto combine = [&] (bdd a, bdd b, bool disjoin = false) {
                owner.step ();
                hit (owner.options, FaultPoint::guard);
                if (guard_ops++ >= owner.options.max_guards_per_row)
                  fail (FailureKind::guard_limit);
                auto result = disjoin ? a | b : a & b;
                check (owner.options);
                return result;
              };
              const auto add = [&] (Alternatives& result, AltKey k, bdd guard) {
                owner.step ();
                hit (owner.options, FaultPoint::branch);
                if (branches++ >= owner.options.max_branches_per_row)
                  fail (FailureKind::branch_limit);
                if (guard == bddfalse)
                  return;
                auto [it, fresh] = result.emplace (std::move (k), guard);
                if (fresh)
                  scratch += entry_bytes (it->first);
                else
                  it->second = combine (it->second, guard, true);
                bytes (owner.options, owner.retained, scratch);
              };
              const auto append = [&] (Alternatives& result, const Alternatives& other) {
                for (const auto& [k, guard] : other)
                  add (result, k, guard);
              };
              const auto product = [&] (Alternatives& result, const Alternatives& other) {
                auto left = std::move (result);
                result.clear ();
                for (const auto& [a, ga] : left)
                  for (const auto& [b, gb] : other) {
                    const auto guard = combine (ga, gb);
                    if (guard == bddfalse)
                      continue;
                    AltKey k {a.first, a.second || b.second};
                    for (auto id : b.first) {
                      owner.step ();
                      insert (k.first, id);
                    }
                    add (result, std::move (k), guard);
                  }
                for (const auto& [k, guard] : left)
                  scratch -= entry_bytes (k);
              };
              std::function<const Alternatives& (ClosureId)> expand_formula = [&] (ClosureId id)
                  -> const Alternatives& {
                owner.step ();
                const auto phase = std::binary_search (relevant[id].begin (), relevant[id].end (),
                                                       watch) ? watch : absent;
                auto [it, fresh] = memo.try_emplace (std::make_pair (id, phase));
                if (!fresh)
                  return it->second;
                owner.retain (128);
                auto& result = it->second;
                const auto f = closure[id];
                const auto child = [&] (unsigned i) -> const Alternatives& {
                  return expand_formula (ids.at (f[i]));
                };
                const bool watched = watch < watches.size () &&
                                     watches[watch].obligation == obligation (id);
                if (auto p = persistence.find (id); p != persistence.end ()) {
                  // L(block) = L(FG c1 | ... | FG cm). Waiting shares one
                  // eventuality watch at every index; committed has no watch.
                  // A waiting step may commit after consuming this letter, at
                  // the updated index. Only committing discharges the watch.
                  // If ci holds forever after t, wrapping eventually stops;
                  // committing after the last wrap accepts. Conversely, an
                  // accepting block run commits once and never wraps, advances
                  // at most m-1 more times, then stays at j with bj = !cj false
                  // forever. All commits from one waiting run share its index
                  // and subsequent events until they die on a wrap. Canonical
                  // IDs therefore give at most one waiting and one committed
                  // obligation per index, not a subset of member guesses.
                  const auto& s = p->second;
                  const auto event = !boolean (s.condition);
                  const bool waiting = s.watch_owner != absent;
                  add (result, {{id}, waiting && watched}, !event);
                  // A committed wrap targets a smaller ID; it is dead.
                  if (waiting || s.next > id)
                    add (result, {{s.next}, waiting && watched}, event);
                  if (waiting) {
                    add (result, {{s.committed}, false}, !event);
                    add (result, {{persistence.at (s.next).committed}, false}, event);
                  }
                }
                else if (blocks.contains (id)) {
                  if (watched) {
                    const auto event = boolean (watches[watch].event);
                    add (result, {{id}, false}, event);
                    add (result, {{id}, true}, !event);
                  }
                  else
                    add (result, {{id}, false}, bddtrue);
                }
                else if (f.is_boolean ())
                  add (result, {{}, false}, combine (bddtrue, boolean (f)));
                else
                  switch (f.kind ()) {
                    case Op::And:
                      add (result, {{}, false}, bddtrue);
                      for (auto c : f) {
                        product (result, expand_formula (ids.at (c)));
                        if (result.empty ())
                          break;
                      }
                      break;
                    case Op::Or:
                      for (auto c : f)
                        append (result, expand_formula (ids.at (c)));
                      break;
                    case Op::X: add (result, {{ids.at (f[0])}, false}, bddtrue); break;
                    case Op::U:
                      append (result, child (1));
                      for (const auto& [k, guard] : child (0)) {
                        auto next = k.first;
                        insert (next, id);
                        add (result, {std::move (next), k.second || watched}, guard);
                      }
                      break;
                    case Op::R:
                      append (result, child (0));
                      add (result, {{id}, false}, bddtrue);
                      product (result, child (1));
                      break;
                    default: fail (FailureKind::unsupported_operator);
                  }
                // Entries now belong to the branch, rather than this row's scratch.
                retain (result);
                return result;
              };
              // Expand each obligation once per row: A & A = A, and expanding a
              // copy again only adds mixed choices (e.g. fulfilling and postponing
              // F X p in the same G(F X p & ...) row). Flatten the pending set
              // through And and G (= false R f, which carries itself and expands f
              // on every path); a general R also expands its right operand on every
              // path, so conjuncts of that operand are covered wherever they recur.
              const auto conjuncts = [&] (ClosureId root) {
                Set out;
                std::vector<ClosureId> work {root};
                while (!work.empty ()) {
                  owner.step ();
                  const auto id = work.back ();
                  work.pop_back ();
                  const auto f = closure[id];
                  if (!blocks.contains (id) && !f.is_boolean () && f.is (Op::And))
                    for (auto c : f)
                      work.push_back (ids.at (c));
                  else
                    insert (out, id);
                }
                return out;
              };
              Set leaves, redundant, seen;
              std::vector<ClosureId> work (pending.begin (), pending.end ());
              while (!work.empty ()) {
                owner.step ();
                const auto id = work.back ();
                work.pop_back ();
                if (std::binary_search (seen.begin (), seen.end (), id))
                  continue;
                insert (seen, id);
                const auto f = closure[id];
                const bool plain = !blocks.contains (id) && !f.is_boolean ();
                if (plain && f.is (Op::And))
                  for (auto c : f)
                    work.push_back (ids.at (c));
                else if (plain && f.is (Op::R) && f[0].is_ff ()) {
                  insert (leaves, id);  // G: the R itself only carries to next
                  work.push_back (ids.at (f[1]));
                }
                else {
                  insert (leaves, id);
                  if (plain && f.is (Op::R))
                    for (auto c : conjuncts (ids.at (f[1])))
                      insert (redundant, c);
                }
              }
              Set fixed, varying, carried;
              for (auto id : leaves) {
                const auto f = closure[id];
                if (!blocks.contains (id) && !f.is_boolean () && f.is (Op::R) && f[0].is_ff ())
                  carried.push_back (id);  // leaves is sorted, so carried is too
                else if (!std::binary_search (redundant.begin (), redundant.end (), id))
                  (std::binary_search (relevant[id].begin (), relevant[id].end (), watch)
                       ? varying : fixed).push_back (id);
              }
              // Pending rows share large conjunctions of watch-independent
              // obligations too. Cache their product instead of rebuilding
              // increasingly long next sets for every phase.
              auto [it, fresh] = conjunctions.try_emplace (fixed);
              if (fresh) {
                owner.retain (128 + fixed.size () * sizeof (ClosureId));
                add (it->second, {{}, false}, bddtrue);
                for (auto id : fixed) {
                  product (it->second, expand_formula (id));
                  if (it->second.empty ())
                    break;
                }
                retain (it->second);
              }
              Alternatives merged;
              append (merged, it->second);
              for (auto id : varying) {
                if (merged.empty ())
                  break;
                product (merged, expand_formula (id));
              }
              if (!carried.empty () && !merged.empty ())
                product (merged, Alternatives {{{carried, false}, bddtrue}});
              std::vector<Alternative> result;
              for (const auto& [k, guard] : merged)
                result.push_back ({k.first, k.second, guard});
              owner.retain (128 + pending.size () * sizeof (ClosureId));
              for (auto& a : result)
                owner.retain (sizeof (Alternative) + a.next.size () * sizeof (ClosureId));
              bytes (owner.options, owner.retained, scratch);
              return expansions.emplace (key, std::move (result)).first->second;
            }

            std::size_t due (const Set& next, std::size_t after) {
              auto result = watches.size ();  // completion
              for (auto id : next)
                for (auto w : reachable[id]) {
                  owner.step ();
                  if (w >= after)
                    result = std::min (result, std::size_t (w));
                }
              return result;
            }
        };

        struct State {
            std::size_t branch;
            Set pending;
            std::size_t phase;
        };
        std::vector<std::unique_ptr<Branch>> branches;
        std::vector<State> states {{absent, {}, 0}};  // initial slot
        std::optional<unsigned> sink;
        unsigned dead () {
          if (!sink) {
            sink = state ();
            states.push_back ({absent, {}, 0});
          }
          return *sink;
        }
        std::optional<unsigned> intern (std::size_t branch, const Set& pending,
                                        std::size_t phase) {
          auto& b = *branches[branch];
          const Branch::Key key {pending, phase};
          if (auto p = b.states.find (key); p != b.states.end ())
            return p->second;
          if (b.expand (pending, phase).empty ())
            return {};  // no dead obligation states
          const auto id = state ();
          retain (128 + 2 * pending.size () * sizeof (ClosureId));
          b.states.emplace (key, id);
          states.push_back ({branch, pending, phase});
          return id;
        }
        void row (unsigned src, std::size_t branch, const Set& pending, std::size_t phase,
                  bool accepting) {
          auto& b = *branches[branch];
          for (const auto& a : b.expand (pending, phase)) {
            const auto next_phase =
                a.postponed ? phase : b.due (a.next, phase == b.watches.size () ? 0 : phase + 1);
            if (auto dst = intern (branch, a.next, next_phase))
              edge (src, *dst, a.guard, accepting);
          }
        }
        void complete (unsigned src, bool accepting) {
          bdd covered = bddfalse;
          for (const auto& e : graph->out (src)) {
            step ();
            covered |= e.cond;
          }
          if (covered != bddtrue)
            edge (src, dead (), !covered, accepting);
        }

        void run (Formula bad) {
          // Retain the worker's complete declaration inventory and BDD order.
          Normalizer normalizer {options, &retained};
          normalizer.run (bad);
          const auto declarations = spot::make_twa_graph (graph->get_dict ());
          for (auto ap : normalizer.ap_inventory ())
            declarations->register_ap (ap);
          for (const auto& info : graph->get_dict ()->bdd_map)
            if (info.type == spot::bdd_dict::var)
              graph->register_ap (info.f);
          graph->set_buchi ();
          graph->prop_state_acc (true);
          state ();
          graph->set_init_state (0);
          std::vector<Formula> positive;
          clauses (Formula::Not (bad), positive);
          stats.branches = positive.size ();
          bool initial_accepting = false;
          for (auto clause : positive) {
            const auto i = branches.size ();
            branches.push_back (std::make_unique<Branch> (*this, normalizer.run (clause, true)));
            auto& b = *branches.back ();
            const auto phase = b.due ({0}, 0);
            if (positive.size () == 1) {
              if (b.expand ({0}, phase).empty ()) {
                sink = 0;
                break;
              }
              retain (128 + 2 * sizeof (ClosureId));
              b.states.emplace (Branch::Key {{0}, phase}, 0);
              states[0] = {i, {0}, phase};
              initial_accepting = phase == b.watches.size ();
            }
            row (0, i, {0}, phase, initial_accepting);
          }
          complete (0, initial_accepting);
          for (std::size_t q = 1; q < states.size (); ++q) {
            const auto s = states[q];  // discoveries may reallocate states
            if (s.branch == absent) {
              edge (q, q, bddtrue, false);
              continue;
            }
            const bool accepting = s.phase == branches[s.branch]->watches.size ();
            row (q, s.branch, s.pending, s.phase, accepting);
            complete (q, accepting);
          }
          graph->prop_complete (true);
          hit (options, FaultPoint::publication);
        }
    };
  }

  closure_buchi::Result<spot::twa_graph_ptr> build (spot::formula bad,
                                                    const spot::bdd_dict_ptr& dict,
                                                    Options options, Counters* counters) noexcept {
    Counters stats;
    const auto started = Clock::now ();
    const auto publish = [&] {
      stats.construction_ns = elapsed (started);
      if (counters)
        *counters = stats;
    };
    try {
      if (!dict || !bad)
        fail (FailureKind::unsupported_operator);
      spot_letters::BuddyErrors errors;
      Builder b {std::move (options), stats, spot::make_twa_graph (dict)};
      b.run (bad);
      stats.states = b.graph->num_states ();
      stats.edges = b.graph->num_edges ();
      publish ();
      return b.graph;
    } catch (...) {
      publish ();
      return caught ();
    }
  }
}
