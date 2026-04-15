
#include <utility>

#include "python_interface.hh"
#include "solver/create_automaton.hh"
#include "solver/k_bounded_safety_aut.hh"
#include "solver/solver_invoker.hh"
#include "utils/verbose.hh"

#include <spot/misc/optionmap.hh>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/translate.hh>


utils::voutstream utils::vout;
unsigned int      utils::verbose = 0;

// TODO We need to figure out some clean way to define these, instead of copying them everywhere
size_t posets::vectors::bool_threshold = 0;
size_t posets::vectors::bitset_threshold = 0;


// Parse the LTL formula. Mirrors parse_ltl_string() from solver_invoker.hh
// (which lives in an anonymous namespace and cannot be called from here).
spot::formula parse_ltl (const std::string& input) {
  auto pf = spot::parse_infix_psl (input, spot::default_environment::instance (), false, false);

  if ((not pf.f) or (not pf.errors.empty ())) {
    pf.format_errors (std::cerr);
    throw std::runtime_error ("Error parsing LTL formula");
  }

  return pf.f;
}

io_spec get_io_spec(const std::vector<std::string>& input_aps, const std::vector<std::string>& output_aps) {
  return io_spec{.input_aps = input_aps, .output_aps = output_aps};
}

bdd_io_spec create_bdds (const io_spec& io_spec) {
  // Set up the dictionary now: BuDDy's initialization
  spot::bdd_dict_ptr dict = spot::make_bdd_dict ();

  bdd all_inputs = bddtrue;
  bdd all_outputs = bddtrue;
  for (std::string ap : io_spec.input_aps) {
    // TODO: make signed int, that is what reg_prop returns
    const unsigned v = dict->register_proposition (spot::formula::ap (ap), nullptr);
    all_inputs &= bdd_ithvar (v);
  }
  for (std::string ap : io_spec.output_aps) {
    const unsigned v = dict->register_proposition (spot::formula::ap (ap), nullptr);
    all_outputs &= bdd_ithvar (v);
  }

  return bdd_io_spec{.inputs = all_inputs, .outputs = all_outputs, .dict = dict};
}

// Internal helper: build the TWA for the given formula using the BDD spec.
// Same body as the old create_twa(formula, bdd_io_spec), shared by the
// public create_twa(formula, inputs, outputs) entry point.
static spot::twa_graph_ptr build_twa (spot::formula& formula, bdd_io_spec& io_spec) {
  spot::option_map extra_options;
  extra_options.set ("simul", 0);
  extra_options.set ("ba-simul", 0);
  extra_options.set ("det-simul", 0);
  extra_options.set ("tls-impl", 1);
  extra_options.set ("wdba-minimize", 2);

  spot::translator trans (io_spec.dict, &extra_options);

  auto aut = create_automaton (formula, trans);

  return aut;
}

Game* create_twa (spot::formula& formula,
                  const std::vector<std::string>& input_aps,
                  const std::vector<std::string>& output_aps) {
  auto ios = get_io_spec (input_aps, output_aps);
  auto bdds = create_bdds (ios);
  auto twa = build_twa (formula, bdds);
  return new Game{.twa = twa, .ios = std::move (bdds)};
}

void prep_unreal_automaton (Game& game) {
  // TODO: this no longer works
  push_outputs (game.twa, game.ios.inputs, game.ios.outputs);
}

void preprocess_aut_standard (Game& game, int k_max) {
  aut_preprocessors::standard::make (game.twa, game.ios.inputs,
    game.ios.outputs, k_max) ();
}

void preprocess_aut_surely_losing (Game& game, int k_max) {
  aut_preprocessors::surely_losing::make (game.twa, game.ios.inputs,
      game.ios.outputs, k_max) ();
}

void set_bool_thresh_no_bool_states (Game& game, int k_max) {
  posets::vectors::bool_threshold = boolean_states::no_boolean_states::make (game.twa, k_max)();
}

void set_bool_thresh_forward_saturation (Game& game, int k_max) {
  posets::vectors::bool_threshold = boolean_states::forward_saturation::make (game.twa, k_max)();
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
                                game.ios.inputs, game.ios.outputs);
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
