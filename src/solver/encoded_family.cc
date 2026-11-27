#include "encoded_family.hh"

#include "tlsf_frontend.hh"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <map>
#include <set>
#include <spot/tl/parse.hh>
#include <spot/twa/formula2bdd.hh>
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

        checker (size_t work, std::chrono::steady_clock::time_point end)
            : remaining (work), deadline (end) {}
        ~checker () { dict->unregister_all_my_variables (this); }

        void charge (size_t work = 1) {
          require (work <= remaining and std::chrono::steady_clock::now () < deadline);
          remaining -= work;
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
          auto literal = [] (formula bit) { return bit.is (op::Not) ? bit[0] : bit; };
          const auto first = raw_bits.find (literal (f.is (op::And) ? f[0] : f));
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
            bus.declaration = b->declaration_id;
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

          std::vector<std::map<site, std::map<int64_t, size_t>>> sites (families.size ());
          std::vector<std::set<size_t>> constants (families.size ());
          for (auto i = origins.instances; i; i = i->next) {
            charge ();
            if (auto match = equality_cube (read (i->expanded))) {
              auto [bus, code] = *match;
              if (not i->bindings) {
                constants[bus].insert (code);
                continue;
              }
              const auto* d = i->bindings->domain;
              require (not d->outer and d->source_formula_id == i->source_formula_id
                       and d->binder_id and d->count >= 2 and d->parameter_count
                       and std::ranges::includes (dependencies[bus],
                                                  std::span {d->parameter_ids, d->parameter_count}));
              auto& row = sites[bus][{d, i->source_node_id}];
              auto [it, added] = row.emplace (i->bindings->coordinate, code);
              require (added or it->second == code);
            }
          }

          std::vector<std::map<int64_t, size_t>> maps (families.size ());
          std::map<const TlsfValueDomain*, std::set<size_t>> incidence;
          for (size_t b = 0; b < sites.size (); ++b) {
            require (not sites[b].empty ());
            for (const auto& [key, row] : sites[b]) {
              const auto* d = key.first;
              std::vector<int64_t> domain (d->coordinates, d->coordinates + d->count);
              std::ranges::sort (domain);
              const std::vector<uint16_t> parameters (d->parameter_ids,
                                                       d->parameter_ids + d->parameter_count);
              if (result.coordinates.empty ()) {
                result.coordinates = domain;
                result.parameters = parameters;
              }
              require (result.coordinates == domain and result.parameters == parameters);
              // Recursive helper branches can expose the same cube at a partial site.
              if (row.size () == domain.size ()) {
                if (maps[b].empty ())
                  maps[b] = row;
                auto [it, added] = incidence.try_emplace (d);
                if (added)
                  result.binders.emplace_back (d->source_formula_id, d->binder_id);
                it->second.insert (b);
              }
            }
            for (const auto& [key, row] : sites[b])
              for (auto [coordinate, code] : row) {
                require (std::ranges::binary_search (result.coordinates, coordinate));
                auto it = maps[b].find (coordinate);
                require (it != maps[b].end () and it->second == code);
              }
          }
          std::set<size_t> connected {0};
          for (size_t n = 0; n < families.size (); ++n)
            for (const auto& [d, buses] : incidence)
              if (std::ranges::any_of (buses, [&] (size_t b) { return connected.contains (b); }))
                connected.insert (buses.begin (), buses.end ());
          require (connected.size () == families.size ());
          std::ranges::sort (result.binders);

          std::set<int64_t> fixed;
          for (size_t b = 0; b < maps.size (); ++b)
            for (auto code : constants[b]) {
              auto it = std::ranges::find_if (maps[b], [=] (const auto& p) { return p.second == code; });
              if (it == maps[b].end ()) {
                auto [pos, added] = maps[b].emplace (static_cast<int64_t> (code), code);
                require (added);
                it = pos;
              }
              fixed.insert (it->first);
            }
          std::erase_if (result.coordinates, [&] (auto i) { return fixed.contains (i); });
          result.moving = result.coordinates.size ();
          require (result.moving >= 2);
          result.coordinates.insert (result.coordinates.end (), fixed.begin (), fixed.end ());

          for (size_t b = 0; b < maps.size (); ++b) {
            auto& bus = result.buses[b];
            std::set<size_t> used;
            for (auto coordinate : result.coordinates) {
              require (maps[b].contains (coordinate));
              const auto code = maps[b].at (coordinate);
              require (used.insert (code).second);
              auto atom = fresh ();
              bus.codes.push_back (code);
              bus.selectors.push_back (atom);
              bus.cubes.push_back (cube (b, code));
              const auto truth = truth_table (bus.cubes.back (), {{b, false}});
              for (size_t raw = 0; raw < truth.size (); ++raw)
                require (truth[raw] == (raw == code));
              substitutions.emplace (atom, bus.cubes.back ());
              selectors.emplace (bus.cubes.back (), atom);
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
            f = distribute_next (f[0]);
          return lowered.emplace (n, f).first->second;
        }

        // Boolean shells see temporal subformulas as atoms with canonical children.
        bool equal (formula a, formula b) {
          if (a == b)
            return true;
          std::map<formula, bdd> memo;
          std::map<std::pair<op, std::vector<int>>, bdd> temporal;
          auto canon = [&] (auto&& self, formula f) -> bdd {
            if (auto it = memo.find (f); it != memo.end ())
              return it->second;
            charge ();
            bdd value;
            if (f.is (op::ap, op::tt, op::ff))
              value = spot::formula_to_bdd (f, dict, this);
            else if (boolean_op (f.kind ())) {
              value = self (self, f[0]);
              switch (f.kind ()) {
              case op::Not: value = !value; break;
              case op::Implies: value = bdd_imp (value, self (self, f[1])); break;
              case op::Equiv: value = bdd_biimp (value, self (self, f[1])); break;
              case op::Xor: value ^= self (self, f[1]); break;
              case op::And:
              case op::Or:
                for (unsigned j = 1; j < f.size (); ++j)
                  value = f.is (op::And) ? value & self (self, f[j]) : value | self (self, f[j]);
                break;
              default: throw declined {};
              }
            } else if (f.is (op::X) and boolean_op (f[0].kind ())) {
              value = self (self, distribute_next (f[0]));
            } else if (f.is (op::G) and f[0].is (op::And)) {
              value = bddtrue;
              for (auto c : f[0])
                value &= self (self, formula::G (c));
            } else if (f.is (op::G) and f[0].is (op::Implies) and f[0][1].is (op::And)) {
              value = bddtrue;
              for (auto c : f[0][1])
                value &= self (self, formula::G (formula::Implies (f[0][0], c)));
            } else if (f.is (op::G) and f[0].is (op::G)) {
              value = self (self, f[0]);
            } else {
              std::vector<int> children;
              for (auto c : f)
                children.push_back (self (self, c).id ());
              auto [it, added] = temporal.try_emplace ({f.kind (), std::move (children)});
              if (added)
                it->second = bdd_ithvar (dict->register_proposition (fresh (), this));
              value = it->second;
            }
            charge (bdd_nodecount (value));
            return memo.emplace (f, value).first->second;
          };
          return canon (canon, a) == canon (canon, b);
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

        std::vector<bool> truth_table (formula f, const std::vector<certificate::slot>& slots) {
          std::vector<int> variables;
          std::map<formula, formula> labels;
          size_t rows = 1;
          for (auto [b, next] : slots) {
            require (rows <= remaining / codes (b));
            rows *= codes (b);
            for (auto bit : result.buses[b].members) {
              auto label = next ? fresh () : bit;
              if (next)
                labels.emplace (formula::X (bit), label);
              variables.push_back (dict->register_proposition (label, this));
            }
          }
          charge (rows);
          std::map<int, size_t> positions;
          for (size_t j = 0; j < variables.size (); ++j)
            positions.emplace (variables[j], j);
          const auto original = spot::formula_to_bdd (substitute (f, labels), dict, this);
          std::vector<bool> truth;
          truth.reserve (rows);
          for (size_t code = 0; code < rows; ++code) {
            bdd cursor = original;
            while (cursor != bddtrue and cursor != bddfalse) {
              charge ();
              cursor = ((code >> positions.at (bdd_var (cursor))) & 1) ? bdd_high (cursor) : bdd_low (cursor);
            }
            truth.push_back (cursor == bddtrue);
          }
          auto rebuild = [&] (auto&& self, size_t bit, size_t code) -> bdd {
            if (bit == variables.size ())
              return truth[code] ? bdd (bddtrue) : bdd (bddfalse);
            return bdd_ite (bdd_ithvar (variables[bit]), self (self, bit + 1, code | (size_t {1} << bit)),
                            self (self, bit + 1, code));
          };
          require (rebuild (rebuild, 0, 0) == original);
          return truth;
        }

        formula add_table (formula f, const std::set<certificate::slot>& slots) {
          if (auto it = extracted.find (f); it != extracted.end ())
            return it->second;
          auto atom = fresh ();
          result.predicates.push_back ({atom, f, {slots.begin (), slots.end ()}, {}});
          auto& p = result.predicates.back ();
          p.truth = truth_table (f, p.slots);
          substitutions.emplace (atom, f);
          return extracted.emplace (f, atom).first->second;
        }

        formula abstract_tables (formula f) {
          if (auto it = extracted.find (f); it != extracted.end ())
            return it->second;
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
            if (std::ranges::none_of (p.slots, [&] (auto slot) {
                  return std::ranges::any_of (swaps, [=] (auto s) { return slot.bus == s.bus; });
                }))
              continue;
            charge (p.truth.size ());
            for (size_t row = 0; row < p.truth.size (); ++row) {
              if ((row & 1023) == 0)
                charge (0);
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

          for (size_t i = 1; i < result.moving; ++i)
            if (not invariant (star (i))) {
              for (size_t b = 0; b < unused.size (); ++b)
                infer_aliases (b, unused[b]);
              break;
            }

          for (size_t i = 1; i < result.moving; ++i) {
            auto swaps = star (i);
            require (invariant (swaps));
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
              bool joined = false;
              for (auto& group : bus.unused_classes) {
                charge ();
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

  bool certify_encoded_family (acacia::tlsf_frontend::captured_specification& spec, size_t work,
                               std::chrono::steady_clock::time_point deadline) {
    auto& objective = spec.objective;
    auto& families = objective.metadata.tlsf_declared_families;
    const auto* origins = spec.ast.value_origins ();
    for (auto& family : families)
      family.logical.reset ();
    try {
      require (origins and origins->complete and objective.metadata.tlsf_effective_target == "Mealy");
      checker check (work, deadline);
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
      check.collect (*origins, families);
      const auto root = tlsf_ast_root (spec.ast.get ());
      require (root);
      result.abstract = check.abstract_tables (check.abstract_selectors (root));
      require (check.equal (exact.f, check.substitute (result.abstract, check.substitutions)));
      check.check_action ();

      const auto certified = std::make_shared<const certificate> (std::move (result));
      for (size_t b = 0; b < families.size (); ++b)
        families[b].logical = logical_family {certified, b};
      return true;
    } catch (const declined&) {
      return false;
    }
  }

}  // namespace symmetry
