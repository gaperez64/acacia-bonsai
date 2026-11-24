#include "solver/aiger_abc.hh"

#include <cstdio>
#include <fstream>
#include <iterator>

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
