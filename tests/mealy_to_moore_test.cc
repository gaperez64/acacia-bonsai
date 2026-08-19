#include "solver/mealy_to_moore.hh"

#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

  struct evaluation {
      std::vector<bool> outputs;
      std::vector<bool> next_latches;
  };

  bool literal_value (unsigned literal, const std::vector<bool>& values) {
    if (literal < 2)
      return literal == spot::aig::aig_true ();
    const bool value = values[literal / 2];
    return (literal & 1U) ? not value : value;
  }

  evaluation evaluate (const spot::const_aig_ptr& circuit, const std::vector<bool>& inputs,
                       const std::vector<bool>& latches) {
    std::vector<bool> values (circuit->max_var () / 2 + 1, false);
    for (unsigned i = 0; i < circuit->num_inputs (); ++i)
      values[circuit->input_var (i) / 2] = inputs[i];
    for (unsigned i = 0; i < circuit->num_latches (); ++i)
      values[circuit->latch_var (i) / 2] = latches[i];
    for (unsigned i = 0; i < circuit->num_gates (); ++i) {
      const auto [left, right] = circuit->gates ()[i];
      values[circuit->gate_var (i) / 2] =
          literal_value (left, values) and literal_value (right, values);
    }

    evaluation result;
    result.outputs.reserve (circuit->num_outputs ());
    for (unsigned literal : circuit->outputs ())
      result.outputs.push_back (literal_value (literal, values));
    result.next_latches.reserve (circuit->num_latches ());
    for (unsigned literal : circuit->next_latches ())
      result.next_latches.push_back (literal_value (literal, values));
    return result;
  }

  bool expect (const std::string& name, bool condition) {
    if (condition)
      return true;
    std::cerr << "failed: " << name << '\n';
    return false;
  }

}  // namespace

int main () {
  auto mealy = std::make_shared<spot::aig> (std::vector<std::string> {"request"},
                                            std::vector<std::string> {"grant", "idle"}, 1);
  const unsigned request = mealy->input_var (0);
  const unsigned remembered = mealy->latch_var (0);
  const unsigned grant = mealy->aig_and (request, remembered);
  mealy->set_next_latch (0, request);
  mealy->set_output (0, grant);
  mealy->set_output (1, grant ^ 1U);

  const auto moore = acacia::synthesis::mealy_to_moore (mealy);
  bool ok = true;
  ok &= expect ("signal names preserved", moore->input_names () == mealy->input_names () and
                                              moore->output_names () == mealy->output_names ());
  ok &= expect ("one output latch per output",
                moore->num_latches () == mealy->num_latches () + mealy->num_outputs ());

  std::vector<bool> mealy_state (mealy->num_latches (), false);
  std::vector<bool> moore_state (moore->num_latches (), false);
  std::vector<bool> previous_outputs;
  const std::vector<bool> inputs = {false, true, true, false, true};
  for (size_t step = 0; step < inputs.size (); ++step) {
    const std::vector<bool> letter = {inputs[step]};
    const evaluation source = evaluate (mealy, letter, mealy_state);
    const evaluation delayed = evaluate (moore, letter, moore_state);
    if (step == 0)
      ok &= expect ("initial outputs use all-false valuation",
                    delayed.outputs == std::vector<bool> ({false, true}));
    else
      ok &= expect ("outputs delayed exactly one step", delayed.outputs == previous_outputs);
    previous_outputs = source.outputs;
    mealy_state = source.next_latches;
    moore_state = delayed.next_latches;
  }

  // Every emitted output must be driven only by a newly allocated latch (or
  // its negation), never by a current input or a combinational gate.
  for (unsigned literal : moore->outputs ()) {
    const unsigned positive = literal & ~1U;
    ok &= expect ("output is latch-driven",
                  positive >= moore->latch_var (mealy->num_latches ()) and
                      positive <= moore->latch_var (moore->num_latches () - 1));
  }

  return ok ? 0 : 1;
}
