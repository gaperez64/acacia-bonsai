
#include <ranges>
#include <utility>

#include "python_interface.hh"
#include "solver/create_automaton.hh"
#include "solver/k_bounded_safety_aut.hh"
#include "solver/solver_invoker.hh"
#include "utils/push_aps.hh"
#include "utils/verbose.hh"

#include <sstream>

#include <spot/misc/optionmap.hh>
#include <spot/tl/formula.hh>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/hoa.hh>
#include <spot/twaalgos/translate.hh>


utils::voutstream utils::vout;
unsigned int      utils::verbose = 0;

// TODO We need to figure out some clean way to define these, instead of copying them everywhere
size_t posets::vectors::bool_threshold = 0;
size_t posets::vectors::bitset_threshold = 0;


// Parse the LTL formula. Mirrors parse_ltl_string() from solver_invoker.hh
// (which lives in an anonymous namespace and cannot be called from here).
static spot::formula parse_ltl (const std::string& input) {
  auto pf = spot::parse_infix_psl (input, spot::default_environment::instance (), false, false);

  if ((not pf.f) or (not pf.errors.empty ())) {
    pf.format_errors (std::cerr);
    throw std::runtime_error ("Error parsing LTL formula");
  }

  return pf.f;
}


// Wraps each input AP in the formula with X(...), implementing the
// UNREAL_X_FORMULA Moore-semantics transformation.
static void prep_unreal_formula (spot::formula& formula,
                                 const std::vector<std::string>& input_aps) {
  auto rec = [&input_aps] (auto&& self, spot::formula m) -> spot::formula {
    if (m.is (spot::op::ap) and
        (std::ranges::find (input_aps, m.ap_name ()) != input_aps.end ()))
      return spot::formula::X (m);
    return m.map ([&] (spot::formula t) { return self (self, t); });
  };
  formula = formula.map ([&] (spot::formula t) { return rec (rec, t); });
}


// Build the TWA for the given formula using an already-created dict.
static spot::twa_graph_ptr build_twa (spot::formula& formula,
                                      spot::bdd_dict_ptr dict) {
  spot::option_map extra_options;
  extra_options.set ("simul", 0);
  extra_options.set ("ba-simul", 0);
  extra_options.set ("det-simul", 0);
  extra_options.set ("tls-impl", 1);
  extra_options.set ("wdba-minimize", 2);

  spot::translator trans (dict, &extra_options);
  return create_automaton (formula, trans);
}


Game* create_twa (const std::string& formula_str,
                  const std::vector<std::string>& input_aps,
                  const std::vector<std::string>& output_aps,
                  bool unreal_x_formula) {
  auto formula = parse_ltl (formula_str);

  if (unreal_x_formula)
    prep_unreal_formula (formula, input_aps);

  auto dict = spot::make_bdd_dict ();

  // Build the automaton first so all APs that appear in the formula are
  // already registered before we register the input/output sets.
  auto twa = build_twa (formula, dict);

  // Register input/output APs owned by the Game* we're about to create.
  // The Game destructor calls dict->unregister_all_my_variables(this) to
  // undo these registrations, keeping assert_emptiness() happy.
  // We use a temporary owner key (unique_ptr) during construction and then
  // transfer ownership once the Game pointer is stable.
  auto game = new Game{.dict = dict, .twa = twa};
  bdd all_inputs  = bddtrue;
  bdd all_outputs = bddtrue;
  for (const std::string& ap : input_aps) {
    const unsigned v = dict->register_proposition (spot::formula::ap (ap), game);
    all_inputs &= bdd_ithvar (v);
  }
  for (const std::string& ap : output_aps) {
    const unsigned v = dict->register_proposition (spot::formula::ap (ap), game);
    all_outputs &= bdd_ithvar (v);
  }
  game->inputs  = all_inputs;
  game->outputs = all_outputs;
  return game;
}


void prep_unreal_automaton (Game& game) {
  game.twa = utils::push_aps (game.twa, game.outputs, game.inputs);
}


void preprocess_aut_standard (Game& game, int k_max) {
  aut_preprocessors::standard::make (game.twa, game.inputs,
    game.outputs, k_max) ();
}

void preprocess_aut_surely_losing (Game& game, int k_max) {
  aut_preprocessors::surely_losing::make (game.twa, game.inputs,
      game.outputs, k_max) ();
}

void set_bool_thresh_no_bool_states (Game& game, int k_max) {
  posets::vectors::bool_threshold = boolean_states::no_boolean_states::make (game.twa, k_max)();
}

void set_bool_thresh_forward_saturation (Game& game, int k_max) {
  posets::vectors::bool_threshold = boolean_states::forward_saturation::make (game.twa, k_max)();
}

std::string get_aut_hoa (const Game& game) {
  std::ostringstream os;
  spot::print_hoa (os, game.twa);
  return os.str ();
}

// Python-specific solver. The generic solve_game() in solve_game.hh picks a
// specialised downset type at run time via static_switch_t; we can't
// expose a runtime-selected template through SWIG. Instead, we commit to the
// single fixed instantiation used in this file (winreg_type), mirroring one
// of the branches in solve_game.hh (VECTOR_AND_BITSET_DOWNSET_IMPL<VECTOR_IMPL>).
static std::optional<std::pair<VECTOR_ELT_T, winreg_type>>
solve_game_python (spot::twa_graph_ptr aut,
                   VECTOR_ELT_T k_max, VECTOR_ELT_T k_min, VECTOR_ELT_T k_inc,
                   const bdd& all_inputs, const bdd& all_outputs) {
  // The downset doesn't use bitsets, so no state goes into bitsets.
  posets::vectors::bitset_threshold = aut->num_states ();

  using SpecializedDownset = winreg_type;
  using IOsPrecomputationMaker = IOS_PRECOMPUTER;
  using ActionerMaker = ACTIONER<typename SpecializedDownset::value_type>;
  using InputPickerMaker = INPUT_PICKER;

  auto skn = k_bounded_safety_aut_detail<SpecializedDownset, IOsPrecomputationMaker,
                                         ActionerMaker, InputPickerMaker> (
      aut, k_min, k_max, k_inc, all_inputs, all_outputs,
      IOsPrecomputationMaker (),
      ActionerMaker (),
      InputPickerMaker ());
  return skn.solve ();
}

GameResult* solve_acacia_safety_game (Game& game, int k_max, int k_min, int k_inc) {
  auto res = solve_game_python (game.twa,
                                (VECTOR_ELT_T) k_max,
                                (VECTOR_ELT_T) k_min,
                                (VECTOR_ELT_T) k_inc,
                                game.inputs, game.outputs);
  if (res.has_value ()) {
    auto& [k, region] = *res;
    return new GameResult (k, std::move (region));
  }
  return new GameResult ();
}

vector_wrapper* get_initial_state (Game& game) {
  auto aut = game.twa;
  // Same encoding as used inside post_real / k_bounded_safety_aut.solve():
  // -1 everywhere, 0 for the initial automaton state.
  posets::utils::vector_mm<VECTOR_ELT_T> v (aut->num_states (), -1);
  v[aut->get_init_state_number ()] = 0;
  return new vector_wrapper (vector_type (v));
}
