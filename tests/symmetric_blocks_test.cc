// Correctness test for src/solver/symmetric_blocks.hh: build a SYNTHETIC
// group emulating an n-client, B-block automaton (plus S shared states) with
// the natural "swap slot a,b across every block simultaneously" generators,
// and verify compute_block_layout recovers a valid, consistent slot
// structure -- exactly the structural assumption symmetry::detect's
// generators satisfy by construction (every generator originates from a
// single AP-level client-index transposition applied uniformly across the
// whole automaton).

#include "solver/symmetric_blocks.hh"

#include <cassert>
#include <iostream>

using namespace symmetry;

namespace {

  unsigned state_id (int S, int n, int block, int slot) {
    return (unsigned) (S + block * n + slot);
  }

}  // namespace

int main () {
  int failures = 0;
  for (int n : {2, 3, 5, 7}) {
    for (int B : {1, 3, 4}) {
      for (int S : {0, 2}) {
        const unsigned num_states = (unsigned) (S + B * n);
        group G;
        G.full_symmetric = true;
        for (int i = 0; i < n; ++i) G.indices.push_back (i);

        // Generators: for every pair (a,b), the permutation swapping slot a
        // and slot b in EVERY block simultaneously, fixing shared states and
        // all other slots -- exactly the shape symmetry::detect produces.
        for (int a = 0; a < n; ++a) {
          for (int b = a + 1; b < n; ++b) {
            std::vector<unsigned> phi (num_states);
            for (unsigned s = 0; s < num_states; ++s) phi[s] = s;
            for (int blk = 0; blk < B; ++blk) {
              const unsigned sa = state_id (S, n, blk, a);
              const unsigned sb = state_id (S, n, blk, b);
              phi[sa] = sb;
              phi[sb] = sa;
            }
            G.gens.push_back (std::move (phi));
          }
        }

        auto L = compute_block_layout (G, num_states);

        // n==2 is a documented, intentional decline (stabilizer signatures
        // degenerate: the single (0,1) generator moves both slots, so
        // neither has a distinguishing signature). Verify we correctly
        // decline rather than mislabel.
        if (n == 2) {
          if (L.has_value ()) {
            std::cerr << "FAIL (n=2 B=" << B << " S=" << S
                      << "): expected decline (nullopt), got a layout\n";
            ++failures;
          }
          continue;
        }

        bool ok = L.has_value ();
        if (not ok) {
          std::cerr << "FAIL (n=" << n << " B=" << B << " S=" << S << "): no layout found\n";
          ++failures;
          continue;
        }
        auto& layout = *L;
        if (layout.num_blocks != (unsigned) B) { ok = false; std::cerr << "  wrong num_blocks\n"; }
        if (layout.num_clients != (unsigned) n) { ok = false; std::cerr << "  wrong num_clients\n"; }
        if (layout.shared_states.size () != (size_t) S) { ok = false; std::cerr << "  wrong shared count\n"; }

        for (int blk = 0; blk < B and ok; ++blk)
          for (int slot = 0; slot < n and ok; ++slot) {
            const unsigned expected = state_id (S, n, blk, slot);
            const unsigned got = layout.block_slot_state[blk][slot];
            if (got != expected) {
              ok = false;
              std::cerr << "  block " << blk << " slot " << slot << ": expected " << expected
                        << " got " << got << "\n";
            }
          }
        for (unsigned s = 0; s < num_states and ok; ++s) {
          if (s < (unsigned) S) {
            if (layout.block_of[s] != -1 or layout.slot_of[s] != -1) {
              ok = false;
              std::cerr << "  shared state mislabeled\n";
            }
          } else {
            const int b = layout.block_of[s], sl = layout.slot_of[s];
            if (b < 0 or sl < 0 or layout.block_slot_state[b][sl] != s) {
              ok = false;
              std::cerr << "  inconsistent block_of/slot_of\n";
            }
          }
        }

        if (not ok) {
          std::cerr << "FAIL (n=" << n << " B=" << B << " S=" << S << ")\n";
          ++failures;
        }
      }
    }
  }
  if (failures == 0) {
    std::cout << "ALL symmetric_blocks synthetic tests PASSED\n";
    return 0;
  }
  std::cout << failures << " FAILURES\n";
  return 1;
}
