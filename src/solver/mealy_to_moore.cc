#include "solver/mealy_to_moore.hh"

#include <cassert>
#include <memory>
#include <vector>

namespace acacia::synthesis {

  namespace {

    unsigned remap_literal (unsigned literal, const std::vector<unsigned>& literal_map) {
      if (literal < 2)
        return literal;
      const unsigned mapped = literal_map[literal / 2];
      return (literal & 1U) ? (mapped ^ 1U) : mapped;
    }

    bool evaluate_literal (unsigned literal, const std::vector<bool>& values) {
      if (literal == spot::aig::aig_false ())
        return false;
      if (literal == spot::aig::aig_true ())
        return true;
      const bool value = values[literal / 2];
      return (literal & 1U) ? not value : value;
    }

  }  // namespace

  spot::aig_ptr mealy_to_moore (const spot::const_aig_ptr& source) {
    assert (source != nullptr);

    const unsigned input_count = source->num_inputs ();
    const unsigned latch_count = source->num_latches ();
    const unsigned output_count = source->num_outputs ();
    auto result = std::make_shared<spot::aig> (source->input_names (), source->output_names (),
                                               latch_count + output_count);

    // Spot numbers AIG literals consecutively and max_var() is the largest
    // positive literal.  The map stores the destination literal corresponding
    // to each positive source variable.
    std::vector<unsigned> literal_map (source->max_var () / 2 + 1, 0);
    std::vector<bool> reset_values (source->max_var () / 2 + 1, false);

    for (unsigned i = 0; i < input_count; ++i) {
      const unsigned source_literal = source->input_var (i);
      literal_map[source_literal / 2] = result->input_var (i);
      // The established mealy2moore convention chooses all-false inputs when
      // computing the initial output valuation.
      reset_values[source_literal / 2] = false;
    }

    for (unsigned i = 0; i < latch_count; ++i) {
      const unsigned source_literal = source->latch_var (i);
      literal_map[source_literal / 2] = result->latch_var (i);
      // Spot AIG latches are initialized to false.
      reset_values[source_literal / 2] = false;
    }

    const auto& gates = source->gates ();
    for (unsigned i = 0; i < gates.size (); ++i) {
      const auto [left, right] = gates[i];
      const unsigned source_literal = source->gate_var (i);
      literal_map[source_literal / 2] =
          result->aig_and (remap_literal (left, literal_map), remap_literal (right, literal_map));
      reset_values[source_literal / 2] =
          evaluate_literal (left, reset_values) and evaluate_literal (right, reset_values);
    }

    const auto& next_latches = source->next_latches ();
    for (unsigned i = 0; i < latch_count; ++i)
      result->set_next_latch (i, remap_literal (next_latches[i], literal_map));

    const auto& outputs = source->outputs ();
    for (unsigned i = 0; i < output_count; ++i) {
      const unsigned next_output = remap_literal (outputs[i], literal_map);
      const bool reset = evaluate_literal (outputs[i], reset_values);
      const unsigned output_latch = result->latch_var (latch_count + i);

      // Spot only emits zero-reset latches.  A logically true reset is encoded
      // by storing the complemented output and exposing the negated latch.
      result->set_next_latch (latch_count + i, reset ? (next_output ^ 1U) : next_output);
      result->set_output (i, reset ? (output_latch ^ 1U) : output_latch);
    }

    return result;
  }

}  // namespace acacia::synthesis
