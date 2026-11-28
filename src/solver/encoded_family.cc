#include "encoded_family.hh"

#include "formula_normalize.hh"
#include "tlsf_frontend.hh"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <limits>
#include <map>
#include <set>
#include <spot/tl/parse.hh>
#include <spot/twa/bdddict.hh>
#include <unordered_map>

namespace symmetry {
  namespace {
    using spot::formula;
    using spot::op;
    using certificate = encoded_certificate;
    using node = const TlsfAstNode*;

    struct declined {};
    void require (bool condition) {
      if (not condition)
        throw declined {};
    }

    bool boolean_op (op kind) {
      return kind == op::Not or kind == op::And or kind == op::Or or kind == op::Xor
             or kind == op::Implies or kind == op::Equiv;
    }

    formula distribute_next (formula f) {
      return boolean_op (f.kind ())
               ? f.map ([] (formula c) { return distribute_next (c); }) : formula::X (f);
    }

    std::string lowercase (std::string name) {
      for (auto& c : name)
        c = char (std::tolower (static_cast<unsigned char> (c)));
      return name;
    }

    struct checker {
        size_t remaining;
        std::chrono::steady_clock::time_point deadline;
        certificate result;
        spot::bdd_dict_ptr dict = spot::make_bdd_dict ();
        std::set<formula> names;
        std::map<formula, certificate::slot> raw_bits;
        std::unordered_map<node, formula> parsed, lowered;
        std::map<formula, formula> selectors, substitutions, extracted;
        std::vector<std::set<size_t>> constants;

        checker (size_t work, std::chrono::steady_clock::time_point end)
            : remaining (work), deadline (end) {}
        ~checker () { dict->unregister_all_my_variables (this); }

        void charge (size_t work = 1) {
          require (work <= remaining and std::chrono::steady_clock::now () < deadline);
          remaining -= work;
        }

        void charge_product (size_t a, size_t b, size_t factor = 1) {
          require (a <= remaining / b / factor);
          charge (a * b * factor);
        }

        size_t codes (size_t bus) const { return size_t {1} << result.buses[bus].members.size (); }

        formula fresh () {
          for (size_t i = names.size ();; ++i) {
            auto ap = formula::ap ("__encoded_" + std::to_string (i));
            if (names.insert (ap).second)
              return ap;
          }
        }

        static formula from_ast (node n, auto&& child) {
          static constexpr std::array kinds {
            op::tt, op::ff, op::ap, op::Not, op::And, op::Or, op::Implies, op::Equiv,
            op::X, op::X, op::F, op::G, op::U, op::R, op::W, op::M};
          const auto kind = tlsf_ast_node_kind (n);
          require (kind < kinds.size ());
          switch (kind) {
          case TLSF_AST_TRUE: return formula::tt ();
          case TLSF_AST_FALSE: return formula::ff ();
          case TLSF_AST_AP: return formula::ap (lowercase (tlsf_ast_node_ap_name (n)));
          default:
            auto lhs = child (tlsf_ast_node_child (n, 0));
            if (tlsf_ast_node_child_count (n) == 1)
              return formula::unop (kinds[kind], lhs);
            auto rhs = child (tlsf_ast_node_child (n, 1));
            if (kind == TLSF_AST_AND or kind == TLSF_AST_OR)
              return formula::multop (kinds[kind], {lhs, rhs});
            return formula::binop (kinds[kind], lhs, rhs);
          }
        }

        formula read (node n, unsigned depth = 0) {
          if (auto it = parsed.find (n); it != parsed.end ())
            return it->second;
          charge ();
          require (depth < 1024);
          auto f = from_ast (n, [&] (node c) { return read (c, depth + 1); });
          charge (f.size ());
          return parsed.emplace (n, f).first->second;
        }

        formula cube (size_t bus, size_t code, bool next = false) const {
          std::vector<formula> bits;
          for (auto bit : result.buses[bus].members) {
            if (next)
              bit = formula::X (bit);
            bits.push_back ((code & (size_t {1} << bits.size ())) ? bit : formula::Not (bit));
          }
          return formula::And (std::move (bits));
        }

        // Extract a numeric candidate from literals, then rebuild from declaration order.
        std::optional<std::pair<size_t, size_t>> equality_cube (formula f) {
          const auto head = f.is (op::And) ? f[0] : f;
          const auto first = raw_bits.find (head.is (op::Not) ? head[0] : head);
          if (first == raw_bits.end ())
            return {};
          auto [bus, next] = first->second;
          const auto& bits = result.buses[bus].members;
          if ((f.is (op::And) ? f.size () : 1) != bits.size ())
            return {};
          size_t code = 0;
          for (size_t j = 0; j < bits.size (); ++j) {
            const auto bit = next ? formula::X (bits[j]) : bits[j];
            if (f == bit)
              code |= size_t {1} << j;
            if (f.is (op::And))
              for (auto term : f)
                if (term == bit)
                  code |= size_t {1} << j;
          }
          if (f != cube (bus, code, next))
            return {};
          return {{bus, code}};
        }

        void collect (const TlsfValueOrigins& origins, std::span<const declared_family> families) {
          using site = std::pair<const TlsfValueDomain*, uint32_t>;
          std::vector<std::span<const uint16_t>> dependencies;
          std::set<std::pair<bool, uint32_t>> declarations;
          for (auto b = origins.buses; b; b = b->next) {
            const size_t k = result.buses.size ();
            require (k < families.size () and b->proved_bit_width and not b->is_enum
                     and b->bit_count > 0 and b->bit_count < std::numeric_limits<size_t>::digits);
            charge (size_t {1} << b->bit_count);
            const auto& physical = families[k];
            require (physical.is_input != b->is_output and physical.lo == b->lo
                     and physical.hi == long (b->lo) + b->bit_count - 1
                     and physical.members.size () == b->bit_count and b->declaration_id
                     and declarations.emplace (b->is_output, b->declaration_id).second);
            auto& bus = result.buses.emplace_back ();
            bus.is_input = not b->is_output;
            const auto& partition = bus.is_input ? result.inputs : result.outputs;
            for (size_t j = 0; j < b->bit_count; ++j) {
              const auto name = lowercase (b->bits[j]);
              require (name == physical.members[j] and std::ranges::find (partition, name) != partition.end ());
              auto bit = formula::ap (name);
              require (raw_bits.emplace (bit, certificate::slot {k, false}).second);
              raw_bits.emplace (formula::X (bit), certificate::slot {k, true});
              bus.members.push_back (bit);
            }
            dependencies.emplace_back (b->parameter_ids, b->parameter_count);
          }
          require (not result.buses.empty () and result.buses.size () == families.size ());

          using code_map = std::map<int64_t, size_t>;
          std::vector<std::map<site, code_map>> sites (families.size ());
          constants.resize (families.size ());
          for (auto i = origins.instances; i; i = i->next) {
            charge ();
            // Provenance locates candidates. The expanded cube determines the code.
            if (auto match = equality_cube (read (i->expanded))) {
              auto [bus, code] = *match;
              if (not i->bindings) {
                constants[bus].insert (code);
                continue;
              }
              const auto* d = i->bindings->domain;
              require (not d->outer and d->source_formula_id == i->source_formula_id
                       and d->binder_id and d->count >= 2 and d->count <= codes (bus)
                       and d->parameter_count
                       and std::ranges::includes (dependencies[bus],
                                                  std::span {d->parameter_ids, d->parameter_count}));
              auto& row = sites[bus][{d, i->source_node_id}];
              auto [it, added] = row.emplace (i->bindings->coordinate, code);
              require (added or it->second == code);
            }
          }

          std::vector<const code_map*> maps (families.size ());
          std::map<const TlsfValueDomain*, std::set<size_t>> incidence;
          std::set<const TlsfValueDomain*> domains;
          std::span<const uint16_t> parameters;
          for (size_t b = 0; b < sites.size (); ++b) {
            for (const auto& [key, row] : sites[b]) {
              const auto* d = key.first;
              charge ();
              if (domains.insert (d).second) {
                charge_product (d->count, std::bit_width (d->count));
                charge (d->parameter_count);
                std::vector<int64_t> domain (d->coordinates, d->coordinates + d->count);
                std::ranges::sort (domain);
                const std::span ids {d->parameter_ids, d->parameter_count};
                if (result.coordinates.empty ()) {
                  result.coordinates = domain;
                  parameters = ids;
                }
                require (result.coordinates == domain
                         and std::ranges::equal (parameters, ids));
              }
              // Recursive helper branches can expose the same cube at a partial site.
              if (row.size () == d->count) {
                if (not maps[b])
                  maps[b] = &row;
                incidence[d].insert (b);
              }
            }
            require (maps[b]);
            for (const auto& [key, row] : sites[b])
              for (auto [coordinate, code] : row) {
                charge ();
                auto it = maps[b]->find (coordinate);
                require (it != maps[b]->end () and it->second == code
                         and std::ranges::binary_search (result.coordinates, coordinate));
              }
          }
          std::set<size_t> connected {0};
          for (size_t previous = 0;
               connected.size () < families.size () and connected.size () != previous;) {
            previous = connected.size ();
            for (const auto& [d, buses] : incidence) {
              charge (buses.size ());
              if (std::ranges::any_of (buses, [&] (size_t b) { return connected.contains (b); }))
                connected.insert (buses.begin (), buses.end ());
              if (connected.size () == families.size ())
                break;
            }
          }
          require (connected.size () == families.size ());

          std::set<int64_t> fixed;
          for (size_t b = 0; b < maps.size (); ++b)
            for (auto [coordinate, code] : *maps[b])
              if (constants[b].contains (code))
                fixed.insert (coordinate);
          std::erase_if (result.coordinates, [&] (auto i) { return fixed.contains (i); });
          result.moving = result.coordinates.size ();
          require (result.moving >= 2);
          result.coordinates.insert (result.coordinates.end (), fixed.begin (), fixed.end ());

          for (size_t b = 0; b < maps.size (); ++b) {
            auto& bus = result.buses[b];
            std::set<size_t> used;
            for (auto coordinate : result.coordinates) {
              const auto code = maps[b]->at (coordinate);
              require (used.insert (code).second);
              auto atom = fresh ();
              bus.codes.push_back (code);
              bus.selectors.push_back (atom);
              auto raw_cube = cube (b, code);
              const auto truth = truth_table (raw_cube, {{b, false}});
              for (size_t raw = 0; raw < truth.size (); ++raw)
                require (truth[raw] == (raw == code));
              substitutions.emplace (atom, raw_cube);
              selectors.emplace (raw_cube, atom);
              selectors.emplace (cube (b, code, true), formula::X (atom));
            }
          }
        }

        formula abstract_selectors (node n) {
          if (auto it = lowered.find (n); it != lowered.end ())
            return it->second;
          charge ();
          const auto raw = read (n);
          auto it = selectors.find (raw);
          auto f = it == selectors.end ()
                     ? from_ast (n, [&] (node c) { return abstract_selectors (c); }) : it->second;
          if (f.is (op::X))
            f = abstract_next (f[0]);
          return lowered.emplace (n, f).first->second;
        }

        // Bound the distinct operand-node tuples, including terminal nodes.
        static size_t states (const bdd& f) { return size_t (bdd_nodecount (f)) + 2; }

        bdd combine (int kind, const bdd& a, const bdd& b) {
          charge (0);
          charge_product (states (a), states (b));
          return bdd_apply (a, b, kind);
        }

        struct boolean_context {
            std::map<formula, bdd> memo;
            std::map<std::pair<op, std::vector<int>>, bdd> temporal;
        };

        // Boolean shells see temporal subformulas as atoms with canonical children.
        bdd canonical (formula f, boolean_context& ctx) {
          if (auto it = ctx.memo.find (f); it != ctx.memo.end ())
            return it->second;
          charge ();
          const auto child = [&] (formula c) { return canonical (c, ctx); };
          bdd value = bddtrue;
          switch (f.kind ()) {
          case op::tt: break;
          case op::ff: value = bddfalse; break;
          case op::ap: value = bdd_ithvar (dict->register_proposition (f, this)); break;
          case op::Not: {
            auto a = child (f[0]);
            charge (states (a));
            value = bdd_not (a);
            break;
          }
          case op::Implies: value = combine (bddop_imp, child (f[0]), child (f[1])); break;
          case op::Equiv: value = combine (bddop_biimp, child (f[0]), child (f[1])); break;
          case op::Xor: value = combine (bddop_xor, child (f[0]), child (f[1])); break;
          case op::And:
          case op::Or:
            value = child (f[0]);
            for (unsigned j = 1; j < f.size (); ++j)
              value = combine (f.is (op::And) ? bddop_and : bddop_or, value, child (f[j]));
            break;
          default: {
            if (f.is (op::X) and boolean_op (f[0].kind ())) {
              value = child (distribute_next (f[0]));
              break;
            }
            if (f.is (op::G) and acacia::split_globally (f[0], [&] (formula c) {
                  value = combine (bddop_and, value, child (c));
                }))
              break;
            std::vector<int> children;
            for (auto c : f)
              children.push_back (child (c).id ());
            auto [it, added] = ctx.temporal.try_emplace ({f.kind (), std::move (children)});
            if (added)
              it->second = bdd_ithvar (dict->register_anonymous_variables (1, this));
            value = it->second;
          }
          }
          return ctx.memo.emplace (f, value).first->second;
        }

        bool equal (formula a, formula b) {
          boolean_context ctx;
          return a == b or canonical (a, ctx) == canonical (b, ctx);
        }

        bool raw_support (formula f, std::set<certificate::slot>& slots) {
          charge ();
          if (auto it = raw_bits.find (f); it != raw_bits.end ()) {
            slots.insert (it->second);
            return true;
          }
          if (f.is (op::tt, op::ff))
            return true;
          if (not boolean_op (f.kind ()))
            return false;
          for (auto c : f)
            if (not raw_support (c, slots))
              return false;
          return true;
        }

        formula abstract_next (formula f) {
          if (boolean_op (f.kind ())) {
            std::set<certificate::slot> slots;
            // Delay the table atom as a unit when its arguments already span two rounds.
            if (not raw_support (f, slots)
                or std::ranges::none_of (slots, &certificate::slot::next))
              return f.map ([&] (formula c) { return abstract_next (c); });
          }
          return formula::X (f);
        }

        std::vector<bool> truth_table (formula f, const std::vector<certificate::slot>& slots) {
          size_t rows = 1;
          for (auto slot : slots) {
            const auto b = slot.bus;
            require (rows <= remaining / codes (b));
            rows *= codes (b);
          }
          charge (rows);
          const size_t width = std::bit_width (rows) - 1;
          boolean_context ctx;
          const auto start = dict->register_anonymous_variables (width, this);
          auto variable = start;
          for (auto [b, next] : slots)
            for (auto bit : result.buses[b].members)
              ctx.memo.emplace (next ? formula::X (bit) : bit, bdd_ithvar (variable++));
          const auto original = canonical (f, ctx);
          std::vector<bool> truth;
          truth.reserve (rows);
          for (size_t code = 0; code < rows; ++code) {
            charge (0);
            bdd cursor = original;
            while (cursor != bddtrue and cursor != bddfalse) {
              charge ();
              cursor = ((code >> (bdd_var (cursor) - start)) & 1)
                         ? bdd_high (cursor) : bdd_low (cursor);
            }
            truth.push_back (cursor == bddtrue);
          }
          auto rebuild = [&] (auto&& self, size_t bit, size_t code) -> bdd {
            charge (0);
            if (bit == width)
              return truth[code] ? bdd (bddtrue) : bdd (bddfalse);
            const auto hi = self (self, bit + 1, code | (size_t {1} << bit));
            const auto lo = self (self, bit + 1, code);
            charge_product (states (hi), states (lo), 3);
            return bdd_ite (bdd_ithvar (start + bit), hi, lo);
          };
          require (rebuild (rebuild, 0, 0) == original);
          return truth;
        }

        formula add_table (formula f, const std::set<certificate::slot>& slots) {
          if (auto it = extracted.find (f); it != extracted.end ())
            return it->second;
          auto atom = fresh ();
          std::vector<certificate::slot> support (slots.begin (), slots.end ());
          auto truth = truth_table (f, support);
          result.predicates.push_back ({atom, std::move (support), std::move (truth)});
          substitutions.emplace (atom, f);
          return extracted.emplace (f, atom).first->second;
        }

        formula abstract_tables (formula f) {
          if (auto it = extracted.find (f); it != extracted.end ())
            return it->second;
          // A complete exclusion under until is a current raw-bus observation.
          // This lets the original-objective lift check project the whole clause.
          if (f.is (op::U) and f[0].is (op::And) and f[0].size () == result.moving)
            for (size_t b = 0; b < result.buses.size (); ++b) {
              charge (result.moving);
              std::vector<formula> excluded;
              for (size_t i = 0; i < result.moving; ++i)
                excluded.push_back (formula::Not (result.buses[b].selectors[i]));
              if (f[0] == formula::And (excluded)) {
                auto now = add_table (substitute (f[0], substitutions), {{b, false}});
                return extracted.emplace (f, formula::U (now, abstract_tables (f[1]))).first->second;
              }
            }
          std::set<certificate::slot> slots;
          if (raw_support (f, slots) and not slots.empty ())
            return add_table (f, slots);
          if (f.is (op::And, op::Or)) {
            std::vector<formula> raw, other;
            slots.clear ();
            for (auto c : f) {
              std::set<certificate::slot> part;
              if (raw_support (c, part) and not part.empty ()) {
                slots.insert (part.begin (), part.end ());
                raw.push_back (c);
              } else
                other.push_back (abstract_tables (c));
            }
            if (not raw.empty ())
              other.push_back (add_table (formula::multop (f.kind (), std::move (raw)), slots));
            return extracted.emplace (f, formula::multop (f.kind (), std::move (other))).first->second;
          }
          auto g = f.map ([&] (formula c) { return abstract_tables (c); });
          return extracted.emplace (f, g).first->second;
        }

        bool invariant (const certificate::generator& swaps) {
          for (const auto& p : result.predicates) {
            charge_product (p.slots.size (), swaps.size () + 1);
            if (std::ranges::none_of (p.slots, [&] (auto slot) {
                  return std::ranges::any_of (swaps, [=] (auto s) { return slot.bus == s.bus; });
                }))
              continue;
            for (size_t row = 0; row < p.truth.size (); ++row) {
              charge_product (p.slots.size (), swaps.size () + 1);
              size_t image = row, stride = 1;
              for (auto slot : p.slots) {
                auto code = row / stride % codes (slot.bus);
                for (auto [b, first, second] : swaps)
                  if (b == slot.bus) {
                    if (code == first)
                      image = image - first * stride + second * stride;
                    if (code == second)
                      image = image - second * stride + first * stride;
                  }
                stride *= codes (slot.bus);
              }
              if (p.truth[row] != p.truth[image])
                return false;
            }
          }
          return true;
        }

        void infer_aliases (size_t b, const std::set<size_t>& unused) {
          auto& bus = result.buses[b];
          // A relational slice can link an unused code to a selected one even when
          // another table (e.g. a unary predicate) distinguishes their classes.
          for (const auto& p : result.predicates) {
            if (p.slots.size () < 2)
              continue;
            size_t stride = 1;
            for (auto slot : p.slots) {
              const auto radix = codes (slot.bus);
              if (slot.bus == b) {
                charge (p.truth.size ());
                std::vector<std::vector<bool>> signature (radix);
                for (size_t row = 0; row < p.truth.size (); ++row)
                  signature[row / stride % radix].push_back (p.truth[row]);
                std::map<std::vector<bool>, std::optional<size_t>> matches;
                for (size_t i = 0; i < bus.codes.size (); ++i) {
                  auto [it, added] = matches.try_emplace (signature[bus.codes[i]], i);
                  if (not added)
                    it->second.reset ();
                }
                for (auto code : unused) {
                  if (constants[b].contains (code))
                    continue;
                  charge (1 + signature[code].size ());
                  auto it = matches.find (signature[code]);
                  if (it == matches.end () or not it->second)
                    continue;
                  auto& alias = bus.aliases[*it->second];
                  require (not alias or alias == code);
                  alias = code;
                }
              }
              stride *= radix;
            }
          }
          std::set<size_t> linked;
          for (auto alias : bus.aliases)
            require (not alias or linked.insert (*alias).second);
        }

        certificate::generator star (size_t i) const {
          certificate::generator swaps;
          for (size_t b = 0; b < result.buses.size (); ++b) {
            const auto& bus = result.buses[b];
            swaps.push_back ({b, bus.codes[0], bus.codes[i]});
            require (bus.aliases[0].has_value () == bus.aliases[i].has_value ());
            if (bus.aliases[i])
              swaps.push_back ({b, *bus.aliases[0], *bus.aliases[i]});
          }
          return swaps;
        }

        void check_action () {
          std::vector<std::set<size_t>> unused (result.buses.size ());
          for (size_t b = 0; b < unused.size (); ++b) {
            auto& bus = result.buses[b];
            bus.aliases.resize (bus.codes.size ());
            for (size_t c = 0; c < codes (b); ++c)
              unused[b].insert (c);
            for (auto c : bus.codes)
              unused[b].erase (c);
          }

          size_t checked = 1;
          while (checked < result.moving and invariant (star (checked)))
            ++checked;
          if (checked < result.moving) {
            for (size_t b = 0; b < unused.size (); ++b)
              infer_aliases (b, unused[b]);
            require (star (checked).size () > result.buses.size ());
            checked = 1;
          }

          for (size_t i = 1; i < result.moving; ++i) {
            auto swaps = star (i);
            require (i < checked or invariant (swaps));
            std::map<formula, formula> rename;
            for (const auto& bus : result.buses) {
              rename.emplace (bus.selectors[0], bus.selectors[i]);
              rename.emplace (bus.selectors[i], bus.selectors[0]);
            }
            require (equal (result.abstract, substitute (result.abstract, rename)));
            result.generators.push_back (std::move (swaps));
          }

          for (size_t b = 0; b < unused.size (); ++b) {
            auto& bus = result.buses[b];
            for (auto alias : bus.aliases)
              if (alias)
                unused[b].erase (*alias);
            for (auto code : unused[b]) {
              if (constants[b].contains (code)) {
                bus.unused_classes.push_back ({code});
                continue;
              }
              bool joined = false;
              for (auto& group : bus.unused_classes) {
                charge ();
                if (constants[b].contains (group.front ()))
                  continue;
                certificate::generator swap {{b, group.front (), code}};
                if (invariant (swap)) {
                  result.generators.push_back (std::move (swap));
                  group.push_back (code);
                  joined = true;
                  break;
                }
              }
              if (not joined)
                bus.unused_classes.push_back ({code});
            }
          }
        }

        formula substitute (formula f, const std::map<formula, formula>& replacements) {
          charge ();
          if (auto it = replacements.find (f); it != replacements.end ())
            return it->second;
          return f.map ([&] (formula c) { return substitute (c, replacements); });
        }
    };
  }  // namespace

  std::shared_ptr<const encoded_certificate> check_encoded_family (
      const acacia::tlsf_frontend::specification& objective, size_t work,
      std::chrono::steady_clock::time_point deadline) {
    const auto* origins = objective.value_origins;
    try {
      require (objective.ast and origins and origins->complete and origins->buses and
               objective.metadata.tlsf_effective_target == "Mealy");
      checker check (work, deadline);
      auto instance = origins->instances;
      for (; instance and not instance->bindings; instance = instance->next)
        check.charge ();
      require (instance);
      check.charge (objective.formula.size ());
      auto& result = check.result;
      result.objective = objective.formula;
      result.inputs = objective.inputs;
      result.outputs = objective.outputs;
      for (auto partition : {&result.inputs, &result.outputs})
        for (const auto& name : *partition)
          require (check.names.insert (formula::ap (name)).second);
      auto exact = spot::parse_infix_psl (result.objective);
      require (exact.f and exact.errors.empty ());
      exact.f.traverse ([&] (formula f) {
        require (not f.is (op::ap) or check.names.contains (f));
        return false;
      });
      check.collect (*origins, objective.metadata.tlsf_declared_families);
      const auto root = tlsf_ast_root (objective.ast.get ());
      require (root);
      result.abstract = check.abstract_tables (check.abstract_selectors (root));
      require (check.equal (exact.f, check.substitute (result.abstract, check.substitutions)));
      check.check_action ();

      const auto certified = std::make_shared<const certificate> (std::move (result));
      check.charge (0);
      return certified;
    } catch (const declined&) {
    } catch (const std::bad_alloc&) {
    }
    return {};
  }

}  // namespace symmetry
