#include "game.hh"

#include <algorithm>
#include <spot/twaalgos/game.hh>
#include <spot/twaalgos/parity.hh>
#include <stdexcept>

namespace acacia::parity {
  namespace {
    std::vector<unsigned> compact_priorities (const game& g) {
      auto unique = g.priority;
      std::ranges::sort (unique);
      unique.erase (std::unique (unique.begin (), unique.end ()), unique.end ());
      std::vector<unsigned> mapped (unique.size ());
      unsigned current = unique.front () & 1;
      for (unsigned i = 0; i < unique.size (); ++i) {
        if ((current & 1) != (unique[i] & 1))
          ++current;
        mapped[i] = current;
      }
      if (current >= spot::acc_cond::mark_t::max_accsets ())
        throw std::runtime_error ("priorities exceed Spot's acceptance width");
      std::vector<unsigned> result;
      for (unsigned priority : g.priority)
        result.push_back (
            mapped[std::lower_bound (unique.begin (), unique.end (), priority) - unique.begin ()]);
      return result;
    }
  }
  game::game (std::vector<uint8_t> owners, std::vector<unsigned> priorities,
              const std::vector<std::vector<unsigned>>& edges)
    : owner (std::move (owners)),
      priority (std::move (priorities)) {
    if (owner.empty () or priority.size () != size () or edges.size () != size () or
        size () >= no_vertex)
      throw std::runtime_error ("invalid parity game dimensions");
    out_begin.push_back (0);
    in_begin.resize (size () + 1);
    for (unsigned v = 0; v < size (); ++v) {
      if (owner[v] > 1 or edges[v].empty ())
        throw std::runtime_error ("parity game must have two owners and no dead ends");
      for (unsigned to : edges[v]) {
        if (to >= size ())
          throw std::runtime_error ("parity edge destination is out of range");
        successors.push_back (to);
        ++in_begin[to + 1];
      }
      out_begin.push_back (successors.size ());
    }
    for (unsigned v = 0; v < size (); ++v)
      in_begin[v + 1] += in_begin[v];
    auto next = in_begin;
    predecessors.resize (successors.size ());
    for (unsigned v = 0; v < size (); ++v)
      for (unsigned to : out (v))
        predecessors[next[to]++] = v;
  }

  spot::twa_graph_ptr to_spot (const game& g, spot::bdd_dict_ptr dict) {
    const auto priority = compact_priorities (g);
    const auto maximum = *std::max_element (priority.begin (), priority.end ());
    if (maximum >= spot::acc_cond::mark_t::max_accsets ())
      throw std::runtime_error ("priorities exceed Spot's acceptance width");
    auto aut = spot::make_twa_graph (std::move (dict));
    aut->new_states (g.size ());
    aut->set_init_state (0);
    aut->set_acceptance (maximum + 1, spot::acc_cond::acc_code::parity (true, true, maximum + 1));
    spot::region_t owners;
    for (unsigned v = 0; v < g.size (); ++v) {
      owners.push_back (g.owner[v]);
      for (unsigned to : g.out (v))
        aut->new_edge (v, to, bddtrue, {priority[v]});
    }
    spot::set_state_players (aut, std::move (owners));
    return aut;
  }

  solution solve_spot (const game& g, const spot::twa_graph_ptr& same_game) {
    spot::solve_parity_game (same_game, true);
    solution result;
    const auto& winners = spot::get_state_winners (same_game);
    const auto& strategies = spot::get_strategy (same_game);
    result.winner.assign (winners.begin (), winners.end ());
    result.strategy.assign (g.size (), no_vertex);
    for (unsigned v = 0; v < g.size (); ++v)
      if (g.owner[v] == result.winner[v])
        result.strategy[v] = same_game->edge_storage (strategies[v]).dst;
    return result;
  }

  bool check_strategy (const game& g, const solution& result, std::string* reason) {
    auto fail = [&] (const char* why) {
      if (reason)
        *reason = why;
      return false;
    };
    if (result.winner.size () != g.size () or result.strategy.size () != g.size ())
      return fail ("incomplete solution");
    for (unsigned v = 0; v < g.size (); ++v) {
      if (result.winner[v] > 1)
        return fail ("unsolved vertex");
      if (g.owner[v] == result.winner[v]) {
        const auto to = result.strategy[v];
        if (std::ranges::find (g.out (v), to) == g.out (v).end () or
            result.winner[to] != result.winner[v])
          return fail ("strategy leaves its winning region");
      }
      else {
        for (unsigned to : g.out (v))
          if (result.winner[to] != result.winner[v])
            return fail ("opponent can leave the winning region");
      }
    }
    // For each region, every infinite play respecting its owner's strategy must
    // satisfy that owner's parity. Emptiness detects bad cycles, including ones
    // hidden in a sub-SCC of a larger SCC with the correct maximum priority.
    const auto priority = compact_priorities (g);
    const auto maximum = *std::max_element (priority.begin (), priority.end ());
    if (maximum >= spot::acc_cond::mark_t::max_accsets ())
      return fail ("strategy priority exceeds Spot's acceptance width");
    for (unsigned player = 0; player < 2; ++player) {
      auto paths = spot::make_twa_graph (spot::make_bdd_dict ());
      paths->new_states (g.size () + 1);
      paths->set_init_state (g.size ());
      paths->set_acceptance (maximum + 1,
                             spot::acc_cond::acc_code::parity (true, player == 0, maximum + 1));
      for (unsigned v = 0; v < g.size (); ++v) {
        if (result.winner[v] != player)
          continue;
        paths->new_edge (g.size (), v, bddtrue);
        for (unsigned to : g.out (v))
          if (g.owner[v] != player or result.strategy[v] == to)
            paths->new_edge (v, to, bddtrue, {priority[v]});
      }
      if (not paths->is_empty ())
        return fail ("strategy admits a losing parity cycle");
    }
    return true;
  }

  namespace {
    game expand (const spot::twa_graph_ptr& arena, std::vector<unsigned>& edge_for_vertex) {
      const auto& players = spot::get_state_players (arena);
      std::vector<uint8_t> owners (players.begin (), players.end ());
      std::vector<unsigned> priorities (owners.size (), 0);
      std::vector<std::vector<unsigned>> edges (owners.size ());
      edge_for_vertex.assign (owners.size (), no_vertex);
      for (unsigned v = 0; v < arena->num_states (); ++v) {
        for (const auto& e : arena->out (v)) {
          if (e.cond == bddfalse)
            continue;
          if (e.acc.count () != 1)
            throw std::runtime_error ("split arena must be colored");
          const unsigned intermediate = owners.size ();
          edges[v].push_back (intermediate);
          owners.push_back (0);
          priorities.push_back (e.acc.max_set () - 1);
          edges.push_back ({e.dst});
          edge_for_vertex.push_back (arena->edge_number (e));
        }
      }
      return {std::move (owners), std::move (priorities), edges};
    }
  }

  arena_game::arena_game (spot::twa_graph_ptr split_arena)
    : explicit_game ([&] {
        spot::change_parity_here (split_arena, spot::parity_kind_max, spot::parity_style_odd);
        spot::colorize_parity_here (split_arena, true);
        return expand (split_arena, edge_for_vertex);
      }()),
      arena (std::move (split_arena)) {}

  solution arena_game::extract () const {
    solution result;
    const auto& winners = spot::get_state_winners (arena);
    const auto& strategies = spot::get_strategy (arena);
    result.winner.assign (explicit_game.size (), 2);
    result.strategy.assign (explicit_game.size (), no_vertex);
    for (unsigned v = 0; v < arena->num_states (); ++v) {
      result.winner[v] = winners[v];
      for (unsigned to : explicit_game.out (v))
        if (explicit_game.owner[v] == winners[v] and edge_for_vertex[to] == strategies[v])
          result.strategy[v] = to;
    }
    for (unsigned v = arena->num_states (); v < explicit_game.size (); ++v) {
      const auto to = explicit_game.out (v).front ();
      result.winner[v] = winners[to];
      if (explicit_game.owner[v] == result.winner[v])
        result.strategy[v] = to;
    }
    return result;
  }

  void arena_game::install (const solution& result) {
    spot::region_t winners;
    spot::strategy_t strategy (arena->num_states (), 0);
    for (unsigned v = 0; v < arena->num_states (); ++v) {
      winners.push_back (result.winner[v]);
      if (explicit_game.owner[v] == result.winner[v])
        strategy[v] = edge_for_vertex.at (result.strategy[v]);
    }
    spot::set_state_winners (arena, std::move (winners));
    spot::set_strategy (arena, std::move (strategy));
  }
}
