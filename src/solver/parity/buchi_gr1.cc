#include "solver/parity/buchi_gr1.hh"

#include "phase_records.hh"
#include "solver/spot_nba_fastpath.hh"
#include <unordered_map>

#include <algorithm>
#include <bit>
#include <map>
#include <spot/twaalgos/isdet.hh>
#include <stdexcept>

namespace acacia::parity_route {
  namespace {
    std::string input_name (size_t i) { return "input_" + std::to_string (i); }
    std::string output_name (size_t i) { return "controllable_" + std::to_string (i); }

    // Copy the returned strategy through the public AIG accessors. Positional
    // game names keep even spec inputs named controllable_* uncontrollable.
    // Normalize reset-1 latches by complementing their stored representation.
    spot::aig_ptr controller (const Aig* source, const std::vector<std::string>& inputs,
                              const std::vector<std::string>& outputs) {
      const auto latches = aig_num_latches (source);
      auto result = std::make_shared<spot::aig> (inputs, outputs, latches);
      std::vector<unsigned> literals (1 + aig_num_inputs (source) + latches +
                                      aig_num_ands (source));
      const auto remap = [&] (unsigned literal) {
        return literals.at (literal / 2) ^ (literal & 1u);
      };
      if (aig_num_inputs (source) != inputs.size () || aig_num_outputs (source) != outputs.size ())
        throw std::runtime_error ("Boolean strategy partition changed");
      for (unsigned i = 0; i < inputs.size (); ++i) {
        unsigned literal;
        const char* name = aig_input_name (source, i, &literal);
        if (!name || name != input_name (i))
          throw std::runtime_error ("Boolean strategy input changed");
        literals.at (literal / 2) = result->input_var (i);
      }
      for (unsigned i = 0; i < latches; ++i) {
        unsigned literal, reset;
        aig_latch_at (source, i, &literal, nullptr, &reset);
        if (reset > 1)
          throw std::runtime_error ("Boolean strategy has a nonconstant reset");
        literals.at (literal / 2) = result->latch_var (i) ^ reset;
      }
      for (unsigned i = 0; i < aig_num_ands (source); ++i) {
        unsigned literal, left, right;
        aig_and_at (source, i, &literal, &left, &right);
        literals.at (literal / 2) = result->aig_and (remap (left), remap (right));
      }
      for (unsigned i = 0; i < latches; ++i) {
        unsigned next, reset;
        aig_latch_at (source, i, nullptr, &next, &reset);
        result->set_next_latch (i, remap (next) ^ reset);
      }
      for (unsigned i = 0; i < outputs.size (); ++i) {
        const auto literal = aig_output_lit (source, output_name (i).c_str ());
        if (literal == UINT32_MAX)
          throw std::runtime_error ("Boolean strategy output missing");
        result->set_output (i, remap (literal));
      }
      return result;
    }
  }

  gr1_game encode_buchi_gr1 (const spot::twa_graph_ptr& forbidden,
                             const std::vector<std::string>& inputs,
                             const std::vector<std::string>& outputs, bool moore) {
    if (!forbidden || !forbidden->acc ().is_buchi () || !spot::is_deterministic (forbidden))
      throw std::runtime_error ("Boolean game encoding requires a deterministic Buchi automaton");
    const auto aut = spot_fastpath::complete_copy_with_rejecting_sink (forbidden);
    gr1_game game (aig_new (), aig_free);
    if (!game)
      throw std::bad_alloc ();
    auto* g = game.get ();
    std::map<int, unsigned> signals;
    const auto signal = [&] (const std::string& name, unsigned literal) {
      const auto variable = aut->register_ap (spot::formula::ap (name));
      if (!signals.emplace (variable, literal).second)
        throw std::runtime_error ("Boolean game signal partition overlaps");
    };
    for (unsigned i = 0; i < inputs.size (); ++i)
      signal (inputs[i], aig_input (g, input_name (i).c_str ()));
    std::vector<unsigned> choices;
    for (unsigned i = 0; i < outputs.size (); ++i)
      choices.push_back (aig_input (g, output_name (i).c_str ()));

    // For native Moore, round 0 only commits the initial output. Round t+1
    // consumes (input[t+1], pending_output[t]) and commits the next output.
    // Thus every output precedes its corresponding input. A Moore strategy
    // extends to this Mealy game by ignoring the dummy input[0]; conversely,
    // fixing that dummy input to zero yields a Moore strategy. Ignoring one
    // initial round preserves GF/FG. mealy_to_moore(..., true) both delays the
    // committed outputs and advances the controller through that fixed dummy
    // round, including its game, pending-output and goal-counter latches.
    const unsigned started = moore ? aig_latch (g, AIG_TRUE, 0) : AIG_TRUE;
    for (unsigned i = 0; i < outputs.size (); ++i)
      signal (outputs[i], moore ? aig_latch (g, choices[i], 0) : choices[i]);
    for (const auto& ap : aut->ap ())
      if (!signals.contains (aut->get_dict ()->var_map.at (ap)))
        throw std::runtime_error ("Boolean game automaton AP missing from partition");

    const unsigned bits = std::bit_width (aut->num_states () - 1);
    std::vector<unsigned> state (bits), next (bits, AIG_FALSE);
    for (auto& latch : state)
      latch = aig_latch (g, AIG_FALSE, 0);
    // XOR with the initial state gives it the all-zero code, without changing
    // any edge destination. Unused binary codes are unreachable from reset.
    const unsigned initial = aut->get_init_state_number ();
    std::unordered_map<int, unsigned> labels {{0, AIG_FALSE}, {1, AIG_TRUE}};
    const auto label = [&] (auto&& self, bdd value) -> unsigned {
      if (const auto found = labels.find (value.id ()); found != labels.end ())
        return found->second;
      const auto variable = signals.at (bdd_var (value));
      const auto high = self (self, bdd_high (value));
      const auto low = self (self, bdd_low (value));
      const auto literal =
          aig_or (g, aig_and (g, variable, high), aig_and (g, aig_not (variable), low));
      labels.emplace (value.id (), literal);
      return literal;
    };
    unsigned accepting = AIG_FALSE;
    for (unsigned q = 0; q < aut->num_states (); ++q) {
      unsigned here = started;
      for (unsigned bit = 0; bit < bits; ++bit)
        here = aig_and (g, here, state[bit] ^ unsigned (!((q ^ initial) & (1u << bit))));
      for (const auto& edge : aut->out (q)) {
        const auto enabled = aig_and (g, here, label (label, edge.cond));
        for (unsigned bit = 0; bit < bits; ++bit)
          if ((edge.dst ^ initial) & (1u << bit))
            next[bit] = aig_or (g, next[bit], enabled);
        if (edge.acc.has (0))
          accepting = aig_or (g, accepting, enabled);
      }
    }
    for (unsigned bit = 0; bit < bits; ++bit)
      if (!aig_set_latch_next (g, state[bit], next[bit]))
        throw std::runtime_error ("Boolean game state latch missing");
    // Sample transition acceptance exactly, including state-based edge marks.
    // A reset-false prefix and this one-step delay cannot change GF accepting.
    const auto acc = aig_latch (g, accepting, 0);
    aig_add_fairness (g, acc, "accepting");
    const unsigned never = AIG_FALSE;
    aig_add_justice (g, &never, 1, "false");
    aig_set_output (g, "bad", AIG_FALSE);
    // GF accepting -> GF false is exactly FG !accepting. The validator accepts
    // a singleton false literal (an empty justice record would mean true).
    char message[256];
    if (!tlsf_gr1_validate_game (g, message, sizeof message))
      throw std::runtime_error (message);
    return game;
  }

  buchi_solution solve_buchi_gr1 (const spot::twa_graph_ptr& forbidden,
                                  const std::vector<std::string>& inputs,
                                  const std::vector<std::string>& outputs, bool moore,
                                  bool synthesis) {
    legacy_phase encode ("gr1_encode");
    auto game = encode_buchi_gr1 (forbidden, inputs, outputs, moore);
    encode.finish ();
    legacy_phase solve ("gr1_solve");
    Gr1SolveOptions options {};
    options.oxidd = oxidd_solve_options_default ();
    OxiddFailure failure {};
    options.oxidd.failure = &failure;
    Gr1CertificateStats stats {};
    options.stats = &stats;
    int unreal = 0;
    auto strategy =
        std::shared_ptr<Aig> (solve_gr1_oxidd (game.release (), &unreal, &options), aig_free);
    // The API exposes node samples, not a high-water mark; never label them peaks.
    legacy_count ("bdd_nodes_at_export", stats.nodes_at_export);
    legacy_count ("bdd_nodes_before_teardown", stats.nodes_before_teardown);
    buchi_solution result;
    if (failure.kind != OXIDD_FAILURE_NONE || (!strategy && !unreal)) {
      solve.finish ("gr1_solver_error", true);
      return result;
    }
    solve.finish ();
    result.real = !unreal;
    if (strategy && synthesis)
      result.circuit = [strategy, inputs, outputs] {
        return controller (strategy.get (), inputs, outputs);
      };
    return result;
  }
}
