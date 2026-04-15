#pragma once

// TODO: this uses the global default configuration.hh, but there might be a conflict somewhere, resulting in duplicate template instatiation
#include "solver/solver_invoker.hh"
#include <bddx.h>
#include <optional>
#include <spot/tl/formula.hh>
#include <spot/twa/bdddict.hh>
#include <spot/twa/fwd.hh>

/**
 * The type of vectors in a winning region.
 */
using vector_type = posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>;

/**
 * The type of winning region.
 */
using winreg_type = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<vector_type>;


struct io_spec {
  std::vector<std::string> input_aps;
  std::vector<std::string> output_aps;
};

io_spec get_io_spec(const std::vector<std::string>& input_aps, const std::vector<std::string>& output_aps);


struct bdd_io_spec {
    bdd inputs;
    bdd outputs;
    spot::bdd_dict_ptr dict;
};

bdd_io_spec create_bdds(const io_spec& output_aps);


/**
 * Bundles a (universal co-Büchi) automaton together with the BDDs for its
 * input/output APs and the dictionary that produced them. This lives across
 * the calls used by the Python API (preprocessing, bool threshold, solving)
 * so we don't have to recompute the BDDs at every step.
 */
struct Game {
    spot::twa_graph_ptr twa;
    bdd_io_spec ios;
};


// Parses an LTL formula into a spot::formula, mirroring what
// parse_ltl_string() in solver_invoker.hh does. Kept here so the Python API
// can expose each step of run_ltl() independently.
spot::formula parse_ltl(const std::string& formula);


// needed for UNREAL_X_FORMULA
void prep_unreal_formula(spot::formula& formula, std::vector<std::string>& output_aps);


/**
 * Creates a Game from the LTL formula and the input/output AP lists. This
 * mirrors the "create BDDs + create TWA" portion of run_ltl() and bundles
 * them together so the later pipeline steps only need a Game handle.
 */
Game* create_twa(spot::formula& formula,
                 const std::vector<std::string>& input_aps,
                 const std::vector<std::string>& output_aps);

// needed for UNREAL_X_AUTOMATON
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
