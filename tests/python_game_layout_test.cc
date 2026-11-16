#include "python/python_interface.hh"

#include <iostream>
#include <memory>
#include <spot/twa/twagraph.hh>
#include <string_view>

namespace {
  void require (bool condition, const char* message) {
    if (not condition)
      throw std::runtime_error (message);
  }

  std::unique_ptr<Game> loop_game () {
    // Exercise the real factory/registration owner, but avoid translator
    // heuristics in the mathematical oracle's two nonaccepting self-loops.
    std::unique_ptr<Game> game (create_twa ("0", {"i"}, {"o"}));
    auto aut = spot::make_twa_graph (game->dict);
    aut->new_states (2);
    aut->set_init_state (0);
    aut->set_buchi ();
    aut->prop_state_acc (true);
    aut->new_edge (0, 0, bddtrue);
    aut->new_edge (1, 1, bddtrue);
    game->twa = std::move (aut);
    return game;
  }

  bool check_owner (const Game& game) {
    // i/o are deliberately absent from the edges: the automaton cannot
    // accidentally keep the Game's IO-only registrations alive for it.
    for (const auto& ap : {"i", "o"})
      if (game.dict->has_registered_proposition (spot::formula::ap (ap), &game) < 0) {
        std::cerr << "I08 owner check failed: missing Game registration for " << ap << '\n';
        return false;
      }
    require (game.inputs == bdd_ithvar (game.dict->varnum (spot::formula::ap ("i"))),
             "input cube lost its AP meaning");
    require (game.outputs == bdd_ithvar (game.dict->varnum (spot::formula::ap ("o"))),
             "output cube lost its AP meaning");
    return true;
  }

  bool check_region (Game& game, int k, bool boolean) {
    std::unique_ptr<GameResult> result (solve_acacia_safety_game (game, k, k, 1));
    require (result->is_real (), "nonaccepting loops must have a winning region");
    const auto* region = result->get_winning_region ();
    // Independent oracle: neither loop increments or changes a coordinate.
    // Exactly the ranks in [-1,K-1]^2 (or [-1,0]^2 for Boolean states) are safe.
    for (int x = -1; x <= k; ++x)
      for (int y = -1; y <= k; ++y) {
        std::unique_ptr<vector_wrapper> rank (make_vector (game, {x, y}));
        const bool expected = x <= (boolean ? 0 : k - 1) && y <= (boolean ? 0 : k - 1);
        if (region->contains (*rank) != expected) {
          std::cerr << "I08 rank oracle failed: K=" << k << " rank=[" << x << ',' << y
                    << "] expected=" << expected << " active threshold="
                    << posets::vectors::bool_threshold << '\n';
          return false;
        }
      }
    return true;
  }
}

int main (int argc, char** argv) {
  const std::string_view mutation = argc == 2 ? argv[1] : "";
  try {
    auto a = loop_game ();
    std::weak_ptr<spot::bdd_dict> a_dict = a->dict;
    if (mutation == "--drop-bdd-owner")
      a->dict->unregister_all_my_variables (a.get ());
    if (not check_owner (*a))
      return 17;

    for (int k : {2, 3}) {
      set_bool_thresh_no_bool_states (*a, k);
      require (posets::vectors::bool_threshold == 2, "A must have two counting states");
      if (not check_region (*a, k, false))
        return 17;
      {
        auto b = loop_game ();
        std::weak_ptr<spot::bdd_dict> b_dict = b->dict;
        set_bool_thresh_forward_saturation (*b, k);
        require (posets::vectors::bool_threshold == 0, "B must have two Boolean states");
        if (not check_owner (*b) || not check_region (*b, k, true))
          return 17;
        // Mutation: leave B's equally sized, but semantically wrong, layout
        // active for A. This demonstrates the existing multi-object hazard.
        if (mutation != "--foreign-layout")
          set_bool_thresh_no_bool_states (*a, k);
        if (not check_region (*a, k, false))
          return 17;
        b.reset ();
        require (b_dict.expired (), "B leaked its dictionary/automaton owner");
      }
      require (check_owner (*a), "destroying B invalidated A's IO variables");
    }

    // An accepting self-loop can exhaust K without proving LTL UNREAL.
    std::unique_ptr<Game> losing (create_twa ("1", {}, {}));
    set_bool_thresh_no_bool_states (*losing, 2);
    std::unique_ptr<GameResult> unknown (solve_acacia_safety_game (*losing, 2, 1, 1));
    require (not unknown->is_real () && unknown->get_winning_region () == nullptr,
             "bounded exhaustion must remain absence of a winning certificate");
    a.reset ();
    require (a_dict.expired (), "A leaked its dictionary/automaton owner");
    std::cout << "I08: 123 rank memberships, two layouts, IO owner lifetimes, bounded exhaustion OK\n";
    return 0;
  }
  catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
}
