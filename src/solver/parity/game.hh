#pragma once

#include <cstdint>
#include <limits>
#include <span>
#include <spot/twa/twagraph.hh>
#include <string>
#include <vector>

namespace acacia::parity {
  inline constexpr unsigned no_vertex = std::numeric_limits<unsigned>::max ();

  // Max parity: player zero wins even, player one wins odd. Games are total.
  struct game {
      std::vector<uint8_t> owner;
      std::vector<unsigned> priority;
      std::vector<size_t> out_begin;
      std::vector<unsigned> successors;
      game (std::vector<uint8_t> owners, std::vector<unsigned> priorities,
            const std::vector<std::vector<unsigned>>& edges);
      size_t size () const { return owner.size (); }
      std::span<const unsigned> out (unsigned v) const {
        return {successors.data () + out_begin[v], out_begin[v + 1] - out_begin[v]};
      }
  };

  struct solution {
      std::vector<uint8_t> winner;
      std::vector<unsigned> strategy;
  };

  bool check_strategy (const game& g, const solution& result, std::string* reason = nullptr);

  struct arena_game {
      // Each real state retains its number; each edge gets a single-successor priority vertex.
      std::vector<unsigned> edge_for_vertex;
      game explicit_game;
      spot::twa_graph_ptr arena;
      explicit arena_game (spot::twa_graph_ptr split_arena);
      solution extract () const;
  };
}
