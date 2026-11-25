#include "solver/aiger_abc.hh"

#include <cstdio>
#include <fstream>
#include <iterator>

using namespace acacia::synthesis;
static_assert (aiger_abc_deadline (0, 10000000000) == 15000000000);
static_assert (aiger_abc_deadline (40000000000, 10000000000) == 15000000000);
static_assert (aiger_abc_deadline (14000000000, 10000000000) == 11000000000);
static_assert (aiger_abc_deadline (10000000000, 10000000000) == 10000000000);
static_assert (aiger_abc_deadline (1, 10000000000) == 10000000000);
static_assert (aiger_abc_payload_limit (0) == 0);
static_assert (aiger_abc_payload_limit (256 * 1024 * 1024) == 16 * 1024 * 1024);
static_assert (aiger_abc_payload_limit (512 * 1024 * 1024) == 32 * 1024 * 1024);
static_assert (aiger_abc_payload_limit (UINT64_MAX) == 32 * 1024 * 1024);

int main (int argc, char** argv) {
  if (argc != 3)
    return 1;
  std::ifstream input (argv[1]);
  const std::string original {std::istreambuf_iterator<char> (input), {}};
  if (original.empty ())
    return 1;
  for (const acacia::synthesis::abc_options options :
       {acacia::synthesis::abc_options {true, 0, 1},              // Insufficient worker memory.
        acacia::synthesis::abc_options {true, 1, UINT64_MAX}}) {  // Expired deadline.
    auto text = original;
    acacia::synthesis::shrink_aiger (
        text, options, [] { std::fputs ("unexpected ABC attempt without a budget\n", stderr); });
    if (text != original)
      return 1;
  }
  std::ofstream output (argv[2]);
  output << original;
  output.close ();
  return output ? 0 : 1;
}
