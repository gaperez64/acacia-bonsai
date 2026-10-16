#pragma once

#include <cstdint>
#include <functional>
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
      std::vector<size_t> out_begin, in_begin;
      std::vector<unsigned> successors, predecessors;
      game (std::vector<uint8_t> owners, std::vector<unsigned> priorities,
            const std::vector<std::vector<unsigned>>& edges);
      size_t size () const { return owner.size (); }
      std::span<const unsigned> out (unsigned v) const {
        return {successors.data () + out_begin[v], out_begin[v + 1] - out_begin[v]};
      }
      std::span<const unsigned> in (unsigned v) const {
        return {predecessors.data () + in_begin[v], in_begin[v + 1] - in_begin[v]};
      }
  };

  struct statistics {
      unsigned k = 0, t = 0, pairs = 0;
      uint64_t lift_attempts = 0;
      bool fallback = false;
      size_t fallback_vertices = 0;
      double fallback_seconds = 0;
      std::string fallback_reason;
  };
  struct solution {
      std::vector<uint8_t> winner;
      std::vector<unsigned> strategy;
      statistics stats;
  };
  struct limits {
      // Global operation budget, zero means unbounded. Never inferred from a deadline.
      uint64_t lift_budget = 0;
      unsigned initial_k = 1, initial_t = 1;
      std::function<void (const statistics&, double)> fallback_observer;
      std::function<void (const statistics&)> parameter_observer;
  };

  spot::twa_graph_ptr to_spot (const game& g, spot::bdd_dict_ptr dict = spot::make_bdd_dict ());
  solution solve_spot (const game& g, const spot::twa_graph_ptr& same_game);
  solution solve_strpm (const game& g, limits bounds = {});
  bool check_strategy (const game& g, const solution& result, std::string* reason = nullptr);

  struct arena_game {
      // Each real state retains its number; each edge gets a single-successor priority vertex.
      std::vector<unsigned> edge_for_vertex;
      game explicit_game;
      spot::twa_graph_ptr arena;
      explicit arena_game (spot::twa_graph_ptr split_arena);
      void install (const solution& result);
      solution extract () const;
  };
}
