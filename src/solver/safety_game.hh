#pragma once

#include "configuration.hh"
#include "posets/downsets/vector_backed.hh"
#include "posets/vectors.hh"

#include <spot/twa/twagraph.hh>

// downset type that does not depend on the exact automaton
using GenericDownset =
    posets::downsets::vector_backed<posets::vectors::vector_backed<VECTOR_ELT_T>>;

// Safety game: contains the Büchi automaton and the number of nonboolean states
// may also contain a downset which is either the safe region if solved == true, or some
// overestimation if solved == false if this contains no safe region (safe == nullptr), then the
// game was solved and found to be losing for the controller finally it also includes the invariant
// that was used to solve the game
struct safety_game {
    spot::twa_graph_ptr aut;
    size_t bool_threshold = 0;
    std::shared_ptr<GenericDownset> safe;
    bool solved = false;
    bdd invariant = bddtrue;

  public:
    safety_game (spot::twa_graph_ptr aut, unsigned k_min, size_t bool_threshold);
    std::pair<unsigned long, unsigned long> set_globals ();
};
