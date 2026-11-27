#include "solver/encoded_count.hh"
#include "solver/parity/game.hh"
#include "solver/symmetric_count.hh"
#include "tlsf_frontend.hh"

#include <algorithm>
#include <iostream>
#include <set>
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
    struct trace {
        words state;
        logical_letter previous;
    };
    const auto step = [&] (const trace& source, unsigned word) {
      const auto state = view (source.state).first (header);
      const auto local = view (source.state).subspan (header);
      auto [next, moved, g, ev] = a.finish_round (state, local, word);
      const logical_letter current {word, g, ev};
      if (std::pair {next[0], next[1]} != direct_step (state[0], source.previous, current)
          or (not future.empty () and (next[2] != g or next[3] != ev)))
        throw std::runtime_error ("buffered state or priority disagrees with the raw trace");
      next.insert (next.end (), moved.begin (), moved.end ());
      return trace {std::move (next), current};
    };
    words initial (a.initial.begin (), a.initial.end ());
    initial.insert (initial.end (), a.initial_local.begin (), a.initial_local.end ());
    if (initial[0] != a.global->get_init_state_number () or initial[1] != 0 or initial[4] != absent)
      throw std::runtime_error ("wrong initial state or priority");
    bool fallback = true;
    for (unsigned out = 0; out <= output_mask and fallback; out += output_step) {
      a.poll ();
      fallback = not a.safe_word (out);
    }
    using edge_row = std::set<std::pair<unsigned, unsigned>>;
    const auto checked_row = [&] (unsigned v) {
      const auto& game = a.checked.explicit_game;
      edge_row row;
      for (auto edge : game.out (v))
        row.emplace (game.out (edge).front (), game.priority[edge]);
      return row;
    };
    std::set<words> seen;
    std::vector<trace> queue;
    const auto visit = [&] (trace target) {
      const auto history = future.empty () or target.previous.raw == absent
                           ? absent : target.previous.raw & history_mask;
      if (target.state[4] != history)
        throw std::runtime_error ("packed history disagrees with the raw trace");
      if (seen.insert (target.state).second) {
        if (queue.size () >= a.max_vertices / (1 + a.initial_local.size () / 16))
          throw std::runtime_error ("concrete state limit");
        queue.push_back (std::move (target));
      }
    };
    visit ({initial, {}});
    std::vector<unsigned> paired ((output_mask >> alphabet.input_width) + 1, absent);
    size_t concrete_edges = 0;
    for (size_t i = 0; i < queue.size (); ++i) {
      const auto source = queue[i];
      const auto v = a.lookup (source.state).first;
      std::set<unsigned> inputs_seen;
      const auto checked_inputs = checked_row (v);
      for (unsigned in = 0; in <= input_mask; ++in) {
        a.poll ();
        unsigned canonical_in = in;
        const auto [u, q] = a.lookup (source.state, &canonical_in);
        const auto default_out = fallback ? a.untransport (0, q, output_mask) : 0;
        const auto retained = [&] (unsigned out) {
          return fallback ? out == default_out : a.safe_word (in | out);
        };
        const auto checked_outputs = checked_row (u);
        if (inputs_seen.insert (u).second and not checked_inputs.contains ({u, 0}))
          throw std::runtime_error ("neutral input edge missing from the quotient");
        if (a.witnesses[u].size () != a.succ[u].size ())
          throw std::runtime_error ("missing retained output witnesses");
        for (size_t k = 0; k < a.succ[u].size (); ++k) {
          const auto out = a.untransport (a.witnesses[u][k], q, output_mask);
          if ((out & ~output_mask) or not retained (out))
            throw std::runtime_error ("retained output is outside the allowed row");
          auto& target = paired[out >> alphabet.input_width];
          if (target != absent)
            throw std::runtime_error ("duplicate retained output witness");
          target = a.succ[u][k];
        }
        std::set<unsigned> outputs_seen;
        for (unsigned out = 0; out <= output_mask; out += output_step) {
          a.poll ();
          if (++concrete_edges > a.max_edges)
            throw std::runtime_error ("concrete edge limit");
          if (not retained (out))
            continue;
          auto next = step (source, in | out);
          const auto w = a.lookup (next.state).first;
          auto& target = paired[out >> alphabet.input_width];
          if (target != absent and w != target)
            throw std::runtime_error ("retained output misses its paired successor");
          target = absent;
          if (not checked_outputs.contains ({w, next.state[1]}))
            throw std::runtime_error ("raw output or priority missing from the quotient");
          outputs_seen.insert (w);
          visit (std::move (next));
        }
        if (outputs_seen.size () != a.succ[u].size ())
          throw std::runtime_error ("quotient output has no raw witness");
      }
      if (inputs_seen.size () != a.succ[v].size ())
        throw std::runtime_error ("quotient input has no raw witness");
    }
  }
}  // namespace

int main (int argc, char** argv) {
  using namespace acacia;
  if (argc != 2)
    return 3;
  auto spec = tlsf_frontend::load_with_value_origins (argv[1]);
  const auto cert = symmetry::check_encoded_family (
      spec, 1u << 22, std::chrono::steady_clock::now () + std::chrono::seconds (15));
  if (not cert) {
    std::cerr << "missing encoded certificate\n";
    return 3;
  }
  const auto solve = [&] (const symcount::limits& bound) {
    return symcount::solve_encoded (spec.formula, spec.inputs, spec.outputs, *cert, bound, true, audit);
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
  const auto mismatched = symcount::solve_encoded ("true", spec.inputs, spec.outputs, *cert);
  auto reordered = spec.inputs;
  std::reverse (reordered.begin (), reordered.end ());
  const auto wrong_order = symcount::solve_encoded (spec.formula, reordered, spec.outputs, *cert);
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
