#pragma once

// Only lightweight headers here — solver_invoker.hh / solve_game.hh /
// create_automaton.hh all define non-inline functions, so they must only be
// included in python_interface.cc (one translation unit). The SWIG wrapper
// also includes this header, so pulling in solver_invoker.hh here would
// produce duplicate-symbol linker errors.
#include "configuration.hh"       // VECTOR_ELT_T, VECTOR_IMPL, VECTOR_AND_BITSET_DOWNSET_IMPL macros
#include <posets/downsets.hh>     // posets::downsets::*
#include <posets/vectors.hh>      // posets::vectors::*
#include <bddx.h>
#include <optional>
#include <spot/twa/fwd.hh>
#include <spot/twa/bdddict.hh>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * The type of vectors in a winning region.
 */
using vector_type = posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>;

/**
 * The type of winning region.
 */
using winreg_type = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<vector_type>;


/**
 * Bundles a (universal co-Büchi) automaton together with the BDDs for its
 * input/output APs and the dictionary that produced them. This lives across
 * the calls used by the Python API (preprocessing, bool threshold, solving)
 * so we don't have to recompute the BDDs at every step.
 *
 * Member declaration order matters: C++ destroys members in reverse order, so
 * twa (declared last) is destroyed first, allowing it to unregister its BDD
 * variables before dict's destructor calls assert_emptiness().
 */
struct Game {
    bdd inputs;
    bdd outputs;
    // Names of input/output APs in the order they were registered. Kept here
    // so that Python callers can build BDD cubes, enumerate IO assignments,
    // and translate between BDD variable indices and AP names.
    std::vector<std::string> input_aps;
    std::vector<std::string> output_aps;
    spot::bdd_dict_ptr dict;
    spot::twa_graph_ptr twa;   // destroyed first (last declared)

    ~Game() {
        // Unregister the input/output BDD variables that were registered with
        // this Game* as owner in create_twa(). Must happen before twa and dict
        // are destroyed.
        if (dict)
            dict->unregister_all_my_variables(this);
    }
};


/**
 * Creates a Game from the LTL formula string and the input/output AP lists.
 *
 * If unreal_x_formula is true, each input AP in the formula is wrapped in an
 * X(...) modality before the automaton is built (the UNREAL_X_FORMULA
 * transformation for checking unrealizability via Moore semantics).
 *
 * Ownership of the returned Game is transferred to the caller.
 */
Game* create_twa(const std::string& formula,
                 const std::vector<std::string>& input_aps,
                 const std::vector<std::string>& output_aps,
                 bool unreal_x_formula = false);


/**
 * Applies the UNREAL_X_AUTOMATON transformation to game.twa: pushes the
 * output APs into the next transition (Moore-semantics automaton).
 */
void prep_unreal_automaton(Game& game);


void preprocess_aut_standard(Game& game, int k_max);


void preprocess_aut_surely_losing(Game& game, int k_max);


void set_bool_thresh_no_bool_states(Game& game, int k_max);


void set_bool_thresh_forward_saturation(Game& game, int k_max);


class vector_wrapper {
  private:
    vector_type vec;
  public:
    // Owning constructor: used for freshly built vectors (e.g. the initial
    // state) and when copying a vector out of a winning region during
    // iteration.
    explicit vector_wrapper(vector_type v) : vec{std::move(v)} {}

    [[nodiscard]]
    std::size_t len() const { return vec.end() - vec.begin(); }

    [[nodiscard]]
    const vector_type& get_vec() const {
      return vec;
    }
};


/**
 * Exposes a single vector in a winning region as an iterator.
 */
class vector_iterator {
  private:
    decltype(static_cast<const vector_type*> (nullptr)->begin()) cur;
    decltype(static_cast<const vector_type*> (nullptr)->end()) end;

  public:
    explicit vector_iterator (const vector_wrapper& v)
        : cur{v.get_vec ().begin()}, end{v.get_vec ().end()} {}

    [[nodiscard]]
    bool has_next() const {
      return cur != end;
    }

    VECTOR_ELT_T next() {
      if (cur == end) {
        throw std::out_of_range("iterator exhausted");
      }
      return *cur++;
    }

    [[nodiscard]]
    size_t len() const {
      return end - cur;
    }
};

/**
 * Exposes a winning region as an iterator.
 */
class winreg_iterator {
  private:
    decltype(static_cast<const winreg_type*> (nullptr)->begin()) cur;
    decltype(static_cast<const winreg_type*> (nullptr)->end()) end;

  public:
    explicit winreg_iterator (const winreg_type& s)
        : cur{s.begin()}, end{s.end()} {}

    [[nodiscard]]
    bool has_next() const {
      return cur != end;
    }

    // Returns an owning wrapper around a *copy* of the current vector.
    // Copying decouples the Python object's lifetime from the downset's.
    vector_wrapper* next() {
      vector_wrapper* value = new vector_wrapper((*cur).copy());
      ++cur;
      return value;
    }

    [[nodiscard]]
    size_t len() const {
      return end - cur;
    }
};


/**
 * The (realizable-side) winning region of a safety game. Wraps the downset
 * computed by the solver together with the witness bound k.
 */
class WinningRegion {
  private:
    VECTOR_ELT_T k;
    winreg_type region;

  public:
    WinningRegion(VECTOR_ELT_T k, winreg_type&& r)
        : k{k}, region{std::move(r)} {}

    [[nodiscard]]
    bool contains(const vector_wrapper& v) const {
      return region.contains(v.get_vec());
    }

    [[nodiscard]]
    size_t len() const { return region.size(); }

    [[nodiscard]]
    VECTOR_ELT_T get_k() const { return k; }

    [[nodiscard]]
    const winreg_type& get_region() const { return region; }
};


/**
 * Result of solving the safety game. Either realizable (with a winning
 * region) or unrealizable.
 */
class GameResult {
  private:
    std::optional<WinningRegion> region;

  public:
    // Unrealizable result.
    GameResult() = default;
    // Realizable result with a winning region.
    GameResult(VECTOR_ELT_T k, winreg_type&& r)
        : region{std::in_place, k, std::move(r)} {}

    [[nodiscard]]
    bool is_real() const { return region.has_value(); }

    // Returns a pointer to the internal WinningRegion for realizable games,
    // or nullptr otherwise. The pointer is owned by this GameResult.
    WinningRegion* get_winning_region() {
      if (region.has_value())
        return &region.value();
      return nullptr;
    }
};


/**
 * Returns the automaton in HOA format as a string.
 * Useful for passing the automaton back to the spot Python bindings:
 *   aut = spot.automaton(acacia_python.get_aut_hoa(game))
 */
std::string get_aut_hoa(const Game& game);


/**
 * Returns the ordered list of input AP names that were registered when the
 * game was created. Useful on the Python side for iterating IO assignments
 * and translating AP names into BDD variable indices.
 */
std::vector<std::string> get_input_aps(const Game& game);


/**
 * Returns the ordered list of output AP names (same contract as
 * get_input_aps).
 */
std::vector<std::string> get_output_aps(const Game& game);


/**
 * Number of states in the underlying UCB automaton.
 */
unsigned num_states(const Game& game);


/**
 * Initial state number of the underlying UCB automaton (use together with
 * state_is_accepting).
 */
unsigned initial_state_number(const Game& game);


/**
 * Whether state s of the UCB is accepting (i.e. rejecting in the dual Büchi
 * view that drives the counter increment in the downset successor).
 */
bool state_is_accepting(const Game& game, unsigned s);


/**
 * Computes the forward successor of a state vector under a full IO
 * assignment, using the same update rule as the solver's standard actioner:
 *   new[p] = max over edges p -> q compatible with the assignment of
 *     min(k_cap, v[q] + (1 if q accepting else 0))
 * (with -1 propagated as "absent"). The assignment is given as disjoint
 * lists of true and false AP names; every AP appearing on an edge condition
 * should be decided by (true_aps ∪ false_aps), otherwise the compatibility
 * test is approximate (a partial cube is allowed — any edge whose condition
 * is satisfied by the cube restriction is considered active).
 *
 * Returns a newly owned vector_wrapper.
 */
vector_wrapper* successor(Game& game,
                          const vector_wrapper& v,
                          const std::vector<std::string>& true_aps,
                          const std::vector<std::string>& false_aps,
                          int k_cap);


/**
 * Builds a state vector from a Python-side list of ints. Size must match the
 * number of states of game.twa. Caller takes ownership.
 */
vector_wrapper* make_vector(const Game& game,
                            const std::vector<int>& entries);


/**
 * Solves the safety game wrapped in game. Returns an owned GameResult.
 */
GameResult* solve_acacia_safety_game(Game& game, int k_max, int k_min, int k_inc);


/**
 * Returns the initial state of the underlying universal co-Büchi automaton
 * represented as a vector: all entries are -1 except the entry for the
 * initial state, which is 0. Ownership of the returned wrapper is
 * transferred to the caller.
 */
vector_wrapper* get_initial_state(Game& game);
