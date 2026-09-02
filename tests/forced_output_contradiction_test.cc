#include "solver/forced_output_contradiction.hh"

#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <spot/tl/parse.hh>

namespace {
  using acacia::forced_output_contradiction::response_kind;

  spot::formula parse (std::string_view label, const char* text) {
    auto parsed = spot::parse_infix_psl (text);
    if (not parsed.f or not parsed.errors.empty ()) {
      std::cerr << label << ": parse failure\n";
      parsed.format_errors (std::cerr);
      return {};
    }
    return parsed.f;
  }

  bool expect (std::string_view label, const char* formula,
               std::vector<std::string> inputs,
               std::vector<std::string> outputs, bool wanted) {
    spot::formula parsed = parse (label, formula);
    if (not parsed)
      return false;
    const auto got = acacia::forced_output_contradiction::try_direct (
        parsed, inputs, outputs);
    if (got.unrealizable == wanted
        and (wanted ? got.proof.has_value ()
                    : (not got.proof and not got.decline_reason.empty ())))
      return true;
    std::cerr << label << ": expected "
              << (wanted ? "unrealizable" : "decline") << ", got "
              << (got.unrealizable ? "unrealizable" : "decline")
              << " (" << got.decline_reason << ")\n";
    return false;
  }

  bool expect_response (std::string_view label, const char* formula,
                        response_kind wanted_kind, unsigned wanted_delay) {
    spot::formula parsed = parse (label, formula);
    if (not parsed)
      return false;
    const auto got = acacia::forced_output_contradiction::try_direct (
        parsed, {"r0", "r1"}, {"g0", "g1"});
    if (got.unrealizable and got.proof
        and got.proof->kind == wanted_kind
        and got.proof->delay == wanted_delay)
      return true;
    std::cerr << label << ": response witness mismatch\n";
    return false;
  }

  bool split_aps (std::string_view label, const std::string& line,
                  std::vector<std::string>& aps) {
    if (line.empty ()) {
      std::cerr << label << ": empty AP line\n";
      return false;
    }
    size_t begin = 0;
    while (begin <= line.size ()) {
      const size_t comma = line.find (',', begin);
      const std::string ap = line.substr (begin, comma - begin);
      if (ap.empty ()) {
        std::cerr << label << ": empty AP name\n";
        return false;
      }
      aps.push_back (ap);
      if (comma == std::string::npos)
        break;
      begin = comma + 1;
    }
    return true;
  }

  bool expect_fixture (std::string_view label,
                       const std::string& fixture_directory,
                       const char* filename, bool wanted) {
    const std::string path = fixture_directory + '/' + filename;
    std::ifstream fixture (path);
    if (not fixture) {
      std::cerr << label << ": unable to open fixture " << path << '\n';
      return false;
    }

    std::string formula;
    std::string inputs_line;
    std::string outputs_line;
    std::string extra_line;
    if (not std::getline (fixture, formula)
        or not std::getline (fixture, inputs_line)
        or not std::getline (fixture, outputs_line)
        or std::getline (fixture, extra_line) or formula.empty ()) {
      std::cerr << label << ": malformed fixture " << path
                << " (expected exactly three nonempty lines)\n";
      return false;
    }

    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    if (not split_aps (label, inputs_line, inputs)
        or not split_aps (label, outputs_line, outputs))
      return false;
    return expect (label, formula.c_str (), std::move (inputs),
                   std::move (outputs), wanted);
  }
}  // namespace

int main (int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: forced-output-contradiction-test FIXTURE_DIRECTORY\n";
    return 2;
  }

  // Deliberately create no bdd_dict: this matches the solver call-site state.
  bool ok = true;
  ok &= expect_fixture (
      "full arbiter unreal1",
      argv[1], "full_arbiter_unreal1_pb_2_2_pe_.formula", true);
  ok &= expect_fixture (
      "full arbiter unreal2",
      argv[1], "full_arbiter_unreal2_pb_3_pe_.formula", true);
  ok &= expect_fixture (
      "round robin outer implication",
      argv[1], "round_robin_arbiter_unreal1_pb_2_2_pe_.formula", false);
  ok &= expect_fixture (
      "load balancer outer implication",
      argv[1], "load_balancer_unreal1_pb_2_2_pe_.formula", false);
  ok &= expect_response (
      "fixed delay positive",
      "G !(g0 & g1) & G ((r0 & X r1) -> X X X X (g0 & g1))",
      response_kind::fixed_delay, 4);
  ok &= expect_response (
      "eventual positive",
      "G !(g0 & g1) & G ((r0 & X r1) -> F (g0 & g1))",
      response_kind::eventual, 0);
  ok &= expect ("trigger mentions output",
                "G !(g0 & g1) & G ((g0 & X r1) -> X (g0 & g1))",
                {"r0", "r1"}, {"g0", "g1"}, false);
  ok &= expect ("beta mentions input",
                "G !(g0 & g1) & G ((r0 & X r1) -> X (g0 & r1))",
                {"r0", "r1"}, {"g0", "g1"}, false);
  ok &= expect ("invariant not global",
                "!(g0 & g1) & G ((r0 & X r1) -> X (g0 & g1))",
                {"r0", "r1"}, {"g0", "g1"}, false);
  ok &= expect ("trigger contains F",
                "G !(g0 & g1) & G (F r0 -> X (g0 & g1))",
                {"r0", "r1"}, {"g0", "g1"}, false);
  ok &= expect ("trigger contains U",
                "G !(g0 & g1) & G ((r0 U r1) -> X (g0 & g1))",
                {"r0", "r1"}, {"g0", "g1"}, false);
  ok &= expect ("compatible output",
                "G (!g0 | !g1) & G (r0 -> X g0)",
                {"r0", "r1"}, {"g0", "g1"}, false);
  ok &= expect ("unsatisfiable trigger",
                "G !(g0 & g1) & G ((r0 & !r0) -> X (g0 & g1))",
                {"r0", "r1"}, {"g0", "g1"}, false);
  ok &= expect (
      "candidate below implication",
      "r0 -> (G !(g0 & g1) & G (r0 -> X (g0 & g1)))",
      {"r0", "r1"}, {"g0", "g1"}, false);
  return ok ? 0 : 1;
}
