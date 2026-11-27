#include "solver/controller_check.hh"
#include "solver/encoded_count.hh"
#include "solver/parity/game.hh"
#include "solver/symmetric_count.hh"
#include "tlsf_frontend.hh"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/translate.hh>
#include <stdexcept>

namespace {
  using namespace acacia;
  using symcount::encoded_audit;
  using symcount::encoded_alphabet;
  using words = encoded_audit::words;
  using view = encoded_audit::view;
  constexpr auto absent = encoded_alphabet::absent;

  bdd minterm (const std::vector<int>& vars, unsigned value) {
    bdd m = bddtrue;
    for (unsigned i = 0; i < vars.size (); ++i)
      m &= (value >> i & 1) ? bdd_ithvar (vars[i]) : bdd_nithvar (vars[i]);
    return m;
  }

  struct logical_letter {
      unsigned raw = absent, globals = 0, events = 0;
  };

  void audit (const encoded_audit& a) {
    const auto& alphabet = a.alphabet;
    const auto raw = spot::parse_infix_psl (alphabet.cert.objective);
    if (not raw.f or not raw.errors.empty ())
      throw std::runtime_error ("invalid encoded objective");
    unsigned history_mask = 0;
    auto event_vars = a.event_vars;
    std::vector<std::pair<const symmetry::encoded_certificate::predicate*, int>> future;
    const auto& ap_vars = a.global->get_dict ()->var_map;
    for (const auto& p : alphabet.cert.predicates) {
      if (std::ranges::none_of (p.slots, [] (auto slot) { return slot.next; }))
        continue;
      for (const auto& slot : p.slots)
        if (not slot.next)
          history_mask |= alphabet.mask (slot.bus);
      const auto var = ap_vars.find (p.atom);
      if (var == ap_vars.end () or std::erase (event_vars, var->second) != 1)
        throw std::runtime_error ("certified future AP is missing from the event slots");
      future.emplace_back (&p, var->second);
    }
    if (history_mask != alphabet.history_mask)
      throw std::runtime_error ("history mask disagrees with the certificate");

    // Assemble the logical letter from the raw trace and step Spot directly.
    const auto direct_step = [&] (unsigned state, logical_letter previous, logical_letter current)
      -> std::pair<unsigned, unsigned> {
      if (not future.empty () and previous.raw == absent)
        return {a.global->get_init_state_number (), 0};
      const auto& letter = future.empty () ? current : previous;
      bdd label = minterm (a.global_vars, letter.globals) & minterm (event_vars, letter.events);
      for (const auto& [p, var] : future)
        label &= alphabet.table (*p, previous.raw, current.raw) ? bdd_ithvar (var) : bdd_nithvar (var);
      for (const auto& edge : a.global->out (state))
        if ((label & edge.cond) != bddfalse)
          return {edge.dst, edge.acc.max_set () - 1};
      throw std::runtime_error ("no direct logical-letter transition");
    };

    const unsigned input_mask = alphabet.owner_mask (true);
    const unsigned output_mask = alphabet.owner_mask (false);
    const unsigned output_step = input_mask + 1;
    const auto header = a.initial.size ();
    const auto lookup = [&] (const words& state, unsigned* in = nullptr) {
      words prefix (state.begin (), state.begin () + header);
      words local (state.begin () + header, state.end ());
      auto p = alphabet.canonicalize (local, prefix[4], in);
      const auto v = a.find_vertex (prefix, local);
      if (v == absent)
        throw std::runtime_error ("raw state missing from the quotient");
      if (not in)
        return std::pair {v, std::move (p)};
      if (a.witnesses[v].size () != a.succ[v].size ())
        throw std::runtime_error ("missing retained input witnesses");
      const auto at = std::ranges::find (a.witnesses[v], *in);
      if (at == a.witnesses[v].end ())
        throw std::runtime_error ("raw input missing from the quotient");
      return std::pair {a.succ[v][at - a.witnesses[v].begin ()], std::move (p)};
    };
    const auto untransport = [&] (unsigned word, encoded_alphabet::transport p, unsigned mask) {
      const auto forward = p.coord;
      for (unsigned i = 0; i < forward.size (); ++i)
        p.coord[forward[i]] = i;
      return alphabet.apply (word, mask, p, true);
    };
    const auto chosen = [&] (unsigned v, const auto& p, unsigned mask) {
      const auto edge = a.solution.strategy[v];
      if (edge == parity::no_vertex)
        throw std::runtime_error ("missing winning letter");
      const auto w = a.checked.explicit_game.out (edge).front ();
      const auto at = std::ranges::find (a.succ[v], w);
      if (at == a.succ[v].end ())
        throw std::runtime_error ("missing winning witness");
      return std::pair {untransport (a.witnesses[v][at - a.succ[v].begin ()], p, mask), w};
    };
    struct trace {
        words state;
        logical_letter previous;
    };
    const auto step = [&] (const trace& source, unsigned word, bool exhaustive) {
      const auto state = view (source.state).first (header);
      const auto local = view (source.state).subspan (header);
      auto [next, moved, g, ev] = a.finish_round (state, local, word);
      logical_letter current;
      if (exhaustive) {
        current = {word, g, ev};
        if (std::pair {next[0], next[1]} != direct_step (state[0], source.previous, current)
            or (not future.empty () and (next[2] != g or next[3] != ev)))
          throw std::runtime_error ("buffered state or priority disagrees with the raw trace");
      }
      next.insert (next.end (), moved.begin (), moved.end ());
      return trace {std::move (next), current};
    };
    words initial (a.initial.begin (), a.initial.end ());
    initial.insert (initial.end (), a.initial_local.begin (), a.initial_local.end ());
    if (initial[0] != a.global->get_init_state_number () or initial[1] != 0 or initial[4] != absent)
      throw std::runtime_error ("wrong initial state or priority");
    const auto safe_word = [&] (unsigned word) {
      for (size_t b = 0; b < alphabet.buses.size (); ++b)
        if (not alphabet.cert.buses[b].is_input) {
          const auto val = alphabet.buses[b].values[alphabet.code (word, b)];
          if (not a.safe_weight (b, unsigned (val.mode == 0)))
            return false;
        }
      return true;
    };
    bool fallback = true;
    for (unsigned out = 0; out <= output_mask and fallback; out += output_step) {
      a.poll ();
      fallback = not safe_word (out);
    }
    using edge_row = std::set<std::pair<unsigned, unsigned>>;
    const auto checked_row = [&] (unsigned v) {
      const auto& game = a.checked.explicit_game;
      edge_row row;
      for (auto edge : game.out (v))
        row.emplace (game.out (edge).front (), game.priority[edge]);
      return row;
    };
    const auto replay = [&] (bool exhaustive) {
      spot::twa_graph_ptr strategy;
      std::vector<int> vars;
      const unsigned sink_count = not exhaustive and not a.system_wins and a.output_safety;
      if (not exhaustive) {
        strategy = spot::make_twa_graph (a.global->get_dict ());
        strategy->set_acceptance (0, spot::acc_cond::acc_code::t ());
        for (const auto& name : alphabet.raw_names)
          vars.push_back (strategy->register_ap (name));
        // After a safety violation the counterstrategy can choose zero forever.
        if (sink_count) {
          strategy->new_state ();
          strategy->new_edge (0, 0, minterm ({vars.begin (), vars.begin () + alphabet.input_width}, 0));
        }
      }
      std::map<words, unsigned> index;
      std::vector<trace> queue;
      const auto visit = [&] (trace target) {
        if (exhaustive) {
          const auto history = future.empty () or target.previous.raw == absent
                               ? absent : target.previous.raw & history_mask;
          if (target.state[4] != history)
            throw std::runtime_error ("packed history disagrees with the raw trace");
        }
        auto [at, fresh] = index.try_emplace (target.state, queue.size ());
        if (fresh) {
          if (queue.size () >= a.max_vertices / (1 + a.initial_local.size () / 16))
            throw std::runtime_error ("concrete state limit");
          queue.push_back (std::move (target));
          if (strategy)
            strategy->new_state ();
        }
        return at->second + sink_count;
      };
      const auto root = visit ({initial, {}});
      if (strategy)
        strategy->set_init_state (root);
      std::vector<unsigned> paired (exhaustive ? (output_mask >> alphabet.input_width) + 1 : 0,
                                    absent);
      size_t concrete_edges = 0;
      for (size_t i = 0; i < queue.size (); ++i) {
        const auto source = queue[i];
        const auto [v, p] = lookup (source.state);
        const auto [first_in, chosen_u] = exhaustive or a.system_wins
                                           ? std::pair {0u, absent} : chosen (v, p, input_mask);
        const auto last_in = chosen_u == absent ? input_mask : first_in;
        std::set<unsigned> inputs_seen;
        const auto checked_inputs = exhaustive ? checked_row (v) : edge_row {};
        for (unsigned in = first_in; in <= last_in; ++in) {
          a.poll ();
          unsigned canonical_in = in;
          const auto [u, q] = lookup (source.state, &canonical_in);
          if (chosen_u != absent and u != chosen_u)
            throw std::runtime_error ("lifted input misses its paired successor");
          const auto [first_out, chosen_w] = exhaustive or not a.system_wins
                                               ? std::pair {0u, absent} : chosen (u, q, output_mask);
          const auto last_out = chosen_w == absent ? output_mask : first_out;
          const auto default_out = exhaustive and fallback ? untransport (0, q, output_mask) : 0;
          const auto retained = [&] (unsigned out) {
            return fallback ? out == default_out : safe_word (in | out);
          };
          const auto checked_outputs = exhaustive ? checked_row (u) : edge_row {};
          if (exhaustive) {
            if (inputs_seen.insert (u).second and not checked_inputs.contains ({u, 0}))
              throw std::runtime_error ("neutral input edge missing from the quotient");
            if (a.witnesses[u].size () != a.succ[u].size ())
              throw std::runtime_error ("missing retained output witnesses");
            for (size_t k = 0; k < a.succ[u].size (); ++k) {
              const auto out = untransport (a.witnesses[u][k], q, output_mask);
              if ((out & ~output_mask) or not retained (out))
                throw std::runtime_error ("retained output is outside the allowed row");
              auto& target = paired[out >> alphabet.input_width];
              if (target != absent)
                throw std::runtime_error ("duplicate retained output witness");
              target = a.succ[u][k];
            }
          }
          std::set<unsigned> outputs_seen;
          for (unsigned out = first_out; out <= last_out; out += output_step) {
            a.poll ();
            if (++concrete_edges > a.max_edges)
              throw std::runtime_error ("concrete edge limit");
            const auto word = in | out;
            if (exhaustive and not retained (out))
              continue;
            if (sink_count and not safe_word (word)) {
              strategy->new_edge (i + sink_count, 0, minterm (vars, word));
              continue;
            }
            auto next = step (source, word, exhaustive);
            const auto w = lookup (next.state).first;
            if (exhaustive) {
              auto& target = paired[out >> alphabet.input_width];
              if (target != absent and w != target)
                throw std::runtime_error ("retained output misses its paired successor");
              target = absent;
              if (not checked_outputs.contains ({w, next.state[1]}))
                throw std::runtime_error ("raw output or priority missing from the quotient");
              outputs_seen.insert (w);
            } else if ((chosen_w != absent and w != chosen_w) or a.solution.winner[w] != a.system_wins)
              throw std::runtime_error ("lifted output misses its paired successor or winning region");
            const auto dst = visit (std::move (next));
            if (strategy)
              strategy->new_edge (i + sink_count, dst, minterm (vars, word));
          }
          if (exhaustive and outputs_seen.size () != a.succ[u].size ())
            throw std::runtime_error ("quotient output has no raw witness");
        }
        if (exhaustive and inputs_seen.size () != a.succ[v].size ())
          throw std::runtime_error ("quotient input has no raw witness");
      }
      return strategy;
    };
    replay (true);
    const auto strategy = replay (false);
    spot::translator translator (a.global->get_dict ());
    const auto bad = translator.run (a.system_wins ? spot::formula::Not (raw.f) : raw.f);
    if (not synthesis::controller_satisfies (bad, strategy))
      throw std::runtime_error ("lifted strategy fails the converted objective");
  }
}  // namespace

int main (int argc, char** argv) {
  using namespace acacia;
  if (argc != 2)
    return 3;
  auto spec = tlsf_frontend::load_with_value_origins (argv[1]);
  if (not symmetry::certify_encoded_family (
          spec, 1u << 22, std::chrono::steady_clock::now () + std::chrono::seconds (15))) {
    std::cerr << "missing encoded certificate\n";
    return 3;
  }
  const auto& cert = *spec.metadata.tlsf_declared_families.front ().logical->certificate;
  const auto solve = [&] (const symcount::limits& bound) {
    return symcount::solve_encoded (spec.formula, spec.inputs, spec.outputs, cert, bound, audit);
  };
  using symcount::verdict;
  for (const auto& [bound, reason] : {
           std::pair {symcount::limits {.max_vertices = 1}, "input split limit"},
           std::pair {symcount::limits {.max_edges = 1}, "raw alphabet limit"},
           std::pair {symcount::limits {.deadline_mono_ns = 1}, "deadline"}}) {
    const auto r = solve (bound);
    if (r.outcome != verdict::declined or r.reason != reason) {
      std::cerr << "expected " << reason << ", got " << r.reason << '\n';
      return 3;
    }
  }
  const auto mismatched = symcount::solve_encoded ("true", spec.inputs, spec.outputs, cert);
  auto reordered = spec.inputs;
  std::reverse (reordered.begin (), reordered.end ());
  const auto wrong_order = symcount::solve_encoded (spec.formula, reordered, spec.outputs, cert);
  if (mismatched.outcome != verdict::declined or wrong_order.outcome != verdict::declined) {
    std::cerr << "accepted a different converted objective or partition\n";
    return 3;
  }
  const auto r = solve ({});
  if (not r.lifted or r.outcome == verdict::declined) {
    std::cerr << "audit declined: " << r.reason << '\n';
    return 2;
  }
  std::cout << "checked raw transitions and lifted strategy through the converted objective\n";
  return r.outcome == verdict::realizable ? 0 : 1;
}
