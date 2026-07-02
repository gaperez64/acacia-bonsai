// Correctness test for src/solver/symmetric_conversion.hh: round-trip
// realize()/to_count_vector() against a SYNTHETIC block_layout (matching the
// structure symmetric_blocks_test.cc validates), and cross-checks that
// different candidate split keys always realize a raw vector whose
// to_count_vector() recovers the ORIGINAL count-vector exactly (realize must
// never lose or corrupt information -- only WHICH client gets which type
// varies with the key, never the resulting multiset).

#include "solver/symmetric_conversion.hh"

#include <cassert>
#include <iostream>
#include <random>

using namespace symmetry;
using namespace symmetric_downset;

namespace {

  unsigned state_id (int S, int n, int block, int slot) {
    return (unsigned) (S + block * n + slot);
  }

  block_layout make_synthetic_layout (int n, int B, int S) {
    block_layout L;
    L.num_states = (unsigned) (S + B * n);
    L.num_clients = (unsigned) n;
    L.num_blocks = (unsigned) B;
    L.block_of.assign (L.num_states, -1);
    L.slot_of.assign (L.num_states, -1);
    L.block_slot_state.assign (B, std::vector<unsigned> (n));
    for (int blk = 0; blk < B; ++blk)
      for (int slot = 0; slot < n; ++slot) {
        const unsigned s = state_id (S, n, blk, slot);
        L.block_slot_state[blk][slot] = s;
        L.block_of[s] = blk;
        L.slot_of[s] = slot;
      }
    for (int i = 0; i < S; ++i) L.shared_states.push_back ((unsigned) i);
    return L;
  }

}  // namespace

int main () {
  int failures = 0;
  std::mt19937 rng (13);
  std::uniform_int_distribution<int> val_dist (-1, 4);

  for (int n : {3, 5, 7}) {
    for (int B : {1, 3, 4}) {
      for (int S : {0, 2}) {
        auto L = make_synthetic_layout (n, B, S);

        for (int trial = 0; trial < 20; ++trial) {
          // Build a random raw vector, convert to count form.
          posets::utils::vector_mm<VECTOR_ELT_T> raw (L.num_states);
          for (unsigned s = 0; s < L.num_states; ++s) raw[s] = (VECTOR_ELT_T) val_dist (rng);
          count_vector c = to_count_vector (L, raw);

          if ((long) c.counts.size () > 0) {
            long total = 0;
            for (auto& [t, cnt] : c.counts) total += cnt;
            if (total != n) {
              std::cerr << "FAIL n=" << n << " B=" << B << " S=" << S
                        << ": count total " << total << " != n=" << n << "\n";
              ++failures;
            }
          }

          // For each candidate split key, realize() then re-convert: must
          // recover the EXACT SAME count-vector (realize never loses/adds
          // information, only permutes which client gets which type).
          for (auto& key : candidate_split_keys ()) {
            auto realized = realize (L, c, key);
            auto c2 = to_count_vector (L, realized);
            if (c2.counts != c.counts or c2.shared != c.shared) {
              std::cerr << "FAIL n=" << n << " B=" << B << " S=" << S
                        << ": round-trip mismatch for a candidate split key\n";
              ++failures;
            }
          }
        }
      }
    }
  }

  if (failures == 0) {
    std::cout << "ALL symmetric_conversion tests PASSED\n";
    return 0;
  }
  std::cout << failures << " FAILURES\n";
  return 1;
}
