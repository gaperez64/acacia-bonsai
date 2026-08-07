#include "solver/degenerate_io.hh"

#include <iostream>
#include <string_view>
#include <vector>

#include <spot/tl/parse.hh>
#include <spot/twaalgos/postproc.hh>

namespace {
  using acacia::degenerate_io::verdict;

  spot::formula parse (const char* text) {
    auto parsed = spot::parse_infix_psl (text);
    if (not parsed.f or not parsed.errors.empty ()) {
      parsed.format_errors (std::cerr);
      return spot::formula::ff ();
    }
    return parsed.f;
  }

  bool expect (std::string_view label, const char* formula,
               std::vector<std::string> inputs,
               std::vector<std::string> outputs, verdict wanted) {
    const auto got = acacia::degenerate_io::try_direct (
        parse (formula), inputs, outputs, spot::postprocessor::Small);
    if (got == wanted)
      return true;
    std::cerr << label << ": unexpected verdict\n";
    return false;
  }
}  // namespace

int main () {
  bool ok = true;
  ok &= expect ("empty outputs realizable", "G(i | !i)", {"i"}, {},
                verdict::realizable);
  ok &= expect ("empty outputs unrealizable", "G(i)", {"i"}, {},
                verdict::unrealizable);
  ok &= expect ("empty inputs realizable", "F(o)", {}, {"o"},
                verdict::realizable);
  ok &= expect ("empty inputs unrealizable", "G(o) & G(!o)", {}, {"o"},
                verdict::unrealizable);
  ok &= expect ("both alphabets empty realizable", "1", {}, {},
                verdict::realizable);
  ok &= expect ("both alphabets empty unrealizable", "0", {}, {},
                verdict::unrealizable);
  ok &= expect ("normal game falls through", "G(i -> o)", {"i"}, {"o"},
                verdict::unknown);
  return ok ? 0 : 1;
}
