#include "parity_scalar_oracle.hh"
#include "solver/parity/lanes.hh"

#include <iostream>
#include <random>
#include <stdexcept>

namespace detail = acacia::parity::detail;
std::pair<std::vector<uint8_t>, std::vector<int>> decode (const detail::node_measure& m) {
  std::vector<uint8_t> bits;
  std::vector<int> levels;
  if (m.nlanes and m.levels[0] == detail::TOP_SENTINEL)
    return {{0}, {-1}};
  for (unsigned lane = 0; lane < m.nlanes; ++lane)
    for (unsigned bit = 0; bit < std::popcount (m.masks[lane]); ++bit) {
      bits.push_back ((m.bits[lane] >> bit) & 1);
      levels.push_back (m.levels[lane]);
    }
  return {bits, levels};
}
int main () {
  try {
    std::mt19937 random (1515);
    unsigned successors = 0;
    for (int height = 2; height <= 9; ++height)
      for (int k = 2; k <= std::min (height, 9); ++k)
        for (int t = 1; t <= 7; ++t) {
          detail::lifting simd;
          simd.k = k;
          simd.t = t;
          simd.h = height;
          detail::node_measure initial;
          initial.nlanes = k - 1;
          initial.masks[0] = (1 << (t + 1)) - 1;
          for (int lane = 1; lane < k - 1; ++lane) {
            initial.masks[lane] = 1;
            initial.levels[lane] = lane;
          }
          for (unsigned walk = 0; walk < 10; ++walk) {
            auto current = initial;
            for (unsigned step = 0; step < 200; ++step) {
              auto [bits, levels] = decode (current);
              const int pindex = random () % (height - 1);
              parity_test::scalar_lifting scalar {k, t, height, bits, levels};
              simd.to_tmp (current);
              scalar.prog_tmp (pindex, height);
              simd.prog_tmp (pindex, height);
              detail::node_measure next;
              simd.from_tmp (next);
              auto [next_bits, next_levels] = decode (next);
              if (not scalar.tmp_d.empty () and scalar.tmp_d.front () == -1)
                scalar.tmp_b = {0}, scalar.tmp_d = {-1};
              if (next_bits != scalar.tmp_b or next_levels != scalar.tmp_d)
                throw std::runtime_error ("SIMD successor differs from scalar reference");
              simd.to_best (current);
              scalar.tmp_b = next_bits;
              scalar.tmp_d = next_levels;
              if (simd.compare (pindex) != scalar.compare (pindex, bits, levels))
                throw std::runtime_error ("SIMD comparison differs from scalar reference");
              ++successors;
              if (next_levels.front () == -1)
                break;
              current = next;
            }
          }
        }
    std::cout << successors << " level successors match the scalar reference\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
}
