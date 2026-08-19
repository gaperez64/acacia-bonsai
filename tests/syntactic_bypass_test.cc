#include "solver/syntactic_bypass.hh"

#include <iostream>
#include <string_view>
#include <vector>

#include <spot/tl/parse.hh>

namespace {
  using acacia::syntactic_bypass::verdict;

  spot::formula parse (const char* text) {
    auto parsed = spot::parse_infix_psl (text);
    if (not parsed.f or not parsed.errors.empty ()) {
      parsed.format_errors (std::cerr);
      return spot::formula::ff ();
    }
    return parsed.f;
  }

  bool expect (std::string_view label, const char* formula,
               std::vector<std::string> outputs, verdict wanted) {
    auto got = acacia::syntactic_bypass::try_direct (parse (formula), outputs).value;
    if (got == wanted)
      return true;
    std::cerr << label << ": expected " << acacia::syntactic_bypass::name (wanted)
              << ", got " << acacia::syntactic_bypass::name (got) << '\n';
    return false;
  }

}  // namespace

int main () {
  bool ok = true;
  ok &= expect ("G(bool) realizable", "G(i -> o)", {"o"}, verdict::realizable);
  ok &= expect ("G(bool) unrealizable", "G(i)", {"o"}, verdict::unrealizable);
  ok &= expect ("GF dual", "GF(o) <-> GF(i)", {"o"}, verdict::realizable);
  ok &= expect ("FG/obligation dual", "FG(o) <-> F(i)", {"o"}, verdict::realizable);
  ok &= expect ("unsupported fallback", "F(o)", {"o"}, verdict::unknown);
  ok &= acacia::syntactic_bypass::matches_worker (verdict::realizable, false);
  ok &= acacia::syntactic_bypass::matches_worker (verdict::unrealizable, true);
  ok &= not acacia::syntactic_bypass::matches_worker (verdict::realizable, true);
  ok &= not acacia::syntactic_bypass::matches_worker (verdict::unrealizable, false);
  return ok ? 0 : 1;
}
