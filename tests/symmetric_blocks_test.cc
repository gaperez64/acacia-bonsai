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
        for (bool star_generators : {false, true}) {
          const unsigned num_states = (unsigned) (S + B * n);
          const char* kind = star_generators ? "star" : "all-pairs";
          group G;
          G.full_symmetric = true;
          for (int i = 0; i < n; ++i) G.indices.push_back (i);

          auto add_swap = [&] (int a, int b) {
            std::vector<unsigned> phi (num_states);
            for (unsigned s = 0; s < num_states; ++s) phi[s] = s;
            for (int blk = 0; blk < B; ++blk) {
              const unsigned sa = state_id (S, n, blk, a);
              const unsigned sb = state_id (S, n, blk, b);
              phi[sa] = sb;
              phi[sb] = sa;
            }
            G.gens.push_back (std::move (phi));
            G.gen_pairs.push_back ({a, b});
          };

          if (star_generators) {
            for (int b = 1; b < n; ++b)
              add_swap (0, b);
          } else {
            for (int a = 0; a < n; ++a)
              for (int b = a + 1; b < n; ++b)
                add_swap (a, b);
          }

          auto L = compute_block_layout (G, num_states);

          // n==2 is a documented, intentional decline (stabilizer signatures
          // degenerate: the single (0,1) generator moves both slots, so
          // neither has a distinguishing signature). Verify we correctly
          // decline rather than mislabel.
          if (n == 2) {
            if (L.has_value ()) {
              std::cerr << "FAIL (" << kind << " n=2 B=" << B << " S=" << S
                        << "): expected decline (nullopt), got a layout\n";
              ++failures;
            }
            continue;
          }

          bool ok = L.has_value ();
          if (not ok) {
            std::cerr << "FAIL (" << kind << " n=" << n << " B=" << B
                      << " S=" << S << "): no layout found\n";
            ++failures;
            continue;
          }
          auto& layout = *L;
          if (not generators_match_layout (G, layout)) {
            ok = false;
            std::cerr << "  generators do not match recovered layout\n";
          }
          if (ok and not G.gens.empty ()) {
            group bad = G;
            std::swap (bad.gens[0][0], bad.gens[0][num_states - 1]);
            if (generators_match_layout (bad, layout)) {
              ok = false;
              std::cerr << "  corrupted generator unexpectedly matched layout\n";
            }
          }
          if (layout.num_blocks != (unsigned) B) { ok = false; std::cerr << "  wrong num_blocks\n"; }
          if (layout.num_clients != (unsigned) n) { ok = false; std::cerr << "  wrong num_clients\n"; }
          if (layout.shared_states.size () != (size_t) S) { ok = false; std::cerr << "  wrong shared count\n"; }
          if (layout.slot_to_index.size () != (size_t) n) {
            ok = false;
            std::cerr << "  wrong slot_to_index size\n";
          }
          for (int slot = 0; slot < n and ok; ++slot) {
            if (layout.slot_to_index[slot] != slot) {
              ok = false;
              std::cerr << "  slot " << slot << ": expected AP index " << slot
                        << " got " << layout.slot_to_index[slot] << "\n";
            }
          }

          for (int blk = 0; blk < B and ok; ++blk)
            for (int slot = 0; slot < n and ok; ++slot) {
              const unsigned expected = state_id (S, n, blk, slot);
              const unsigned got = layout.block_slot_state[blk][slot];
              if (got != expected) {
                ok = false;
                std::cerr << "  block " << blk << " slot " << slot << ": expected "
                          << expected << " got " << got << "\n";
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
            std::cerr << "FAIL (" << kind << " n=" << n << " B=" << B
                      << " S=" << S << ")\n";
            ++failures;
          }
        }
      }
    }
  }

  // Partial client symmetry: the automaton has six indexed clients, but
  // client 0 is fixed and the verified group is S_5 on indices {1,...,5}.
  // Its one state in each client block must become ordinary shared state.
  {
    constexpr int total_clients = 6;
    constexpr int active_clients = 5;
    constexpr int B = 4;
    constexpr int S = 3;
    const unsigned num_states = S + B * total_clients;
    group G;
    G.full_symmetric = true;
    for (int index = 1; index <= active_clients; ++index)
      G.indices.push_back (index);
    for (int other = 2; other <= active_clients; ++other) {
      std::vector<unsigned> phi (num_states);
      for (unsigned state = 0; state < num_states; ++state)
        phi[state] = state;
      for (int block = 0; block < B; ++block) {
        const unsigned root_state = state_id (S, total_clients, block, 1);
        const unsigned other_state = state_id (S, total_clients, block, other);
        phi[root_state] = other_state;
        phi[other_state] = root_state;
      }
      G.gens.push_back (std::move (phi));
      G.gen_pairs.push_back ({1, other});
    }

    auto L = compute_block_layout (G, num_states);
    bool ok = L.has_value ();
    if (not ok) {
      std::cerr << "FAIL (partial S_5 plus fixed client): no layout found\n";
    } else {
      ok &= generators_match_layout (G, *L);
      ok &= L->num_clients == active_clients;
      ok &= L->num_blocks == B;
      ok &= L->shared_states.size () == (size_t) (S + B);
      for (int slot = 0; slot < active_clients and ok; ++slot) {
        ok &= L->slot_to_index[slot] == slot + 1;
        for (int block = 0; block < B; ++block)
          ok &= L->block_slot_state[block][slot] ==
                state_id (S, total_clients, block, slot + 1);
      }
      for (int block = 0; block < B and ok; ++block) {
        const unsigned fixed = state_id (S, total_clients, block, 0);
        ok &= L->block_of[fixed] == -1 and L->slot_of[fixed] == -1;
      }
      if (not ok)
        std::cerr << "FAIL (partial S_5 plus fixed client): incorrect layout\n";
    }
    if (not ok)
      ++failures;
  }

  if (failures == 0) {
    std::cout << "ALL symmetric_blocks synthetic tests PASSED\n";
    return 0;
  }
  std::cout << failures << " FAILURES\n";
  return 1;
}
