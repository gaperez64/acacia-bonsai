#include "solve_game.hh"

#include "actioners.hh"
#include "configuration.hh"
#include "input_pickers.hh"
#include "ios_precomputers.hh"
#include "posets/vectors.hh"
#include "posets/vectors/traits.hh"
#include "safety_game.hh"
#include "types.hh"
#include "utils/static_switch.hh"

#include <spot/misc/timer.hh>

bool solve_game (safety_game& game, unsigned kmax, unsigned kmin, unsigned kinc, bdd all_inputs,
                 bdd all_outputs) {
  // moved here from epilogue()
  if (game.aut == nullptr)
    return true;

  spot::stopwatch sw;
  sw.start ();

  auto [nbitsetbools, actual_nonbools] = game.set_globals ();

#define UNREACHABLE [] (int x) { assert (false); }

  constexpr auto STATIC_ARRAY_CAP_MAX =
      posets::vectors::traits<posets::vectors::ARRAY_IMPL, VECTOR_ELT_T>::capacity_for (
          STATIC_ARRAY_MAX);

  if (actual_nonbools <= STATIC_ARRAY_CAP_MAX) {  // Array & Bitsets
    static_switch_t<STATIC_ARRAY_CAP_MAX> {}(
        [&] (auto vnonbools) {
          static_switch_t<STATIC_MAX_BITSETS> {}(
              [&] (auto vbitsets) {
                using SpecializedDownset =
                    posets::downsets::ARRAY_AND_BITSET_DOWNSET_IMPL<posets::vectors::x_and_bitset<
                        posets::vectors::ARRAY_IMPL<VECTOR_ELT_T, std::max (vnonbools.value, 1UL)>,
                        vbitsets.value>>;
                auto skn = k_bounded_safety_aut_maker<SpecializedDownset> (game.aut, kmin, kmax, kinc, all_inputs, all_outputs, IOS_PRECOMPUTER (), ACTIONER<typename SpecializedDownset::value_type> (),
                                                  INPUT_PICKER ());
                assert (game.safe);
                auto current_safe = cast_downset<SpecializedDownset> (*game.safe);
                auto safe = skn.solve (current_safe);
                if (safe.has_value ()) {
                  game.safe = std::make_shared<GenericDownset> (
                      cast_downset<GenericDownset> (safe.value ()));
                }
                else
                  game.safe = nullptr;
              },
              UNREACHABLE, posets::vectors::nbools_to_nbitsets (nbitsetbools));
        },
        UNREACHABLE, actual_nonbools);
  }
  else {  // Vectors & Bitsets
    static_switch_t<STATIC_MAX_BITSETS> {}(
        [&] (auto vbitsets) {
          using SpecializedDownset =
              posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<posets::vectors::x_and_bitset<
                  posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>, vbitsets.value>>;

          auto skn = k_bounded_safety_aut_maker<SpecializedDownset> (game.aut, kmin, kmax, kinc, all_inputs, all_outputs, IOS_PRECOMPUTER (), ACTIONER<typename SpecializedDownset::value_type> (),
                                                  INPUT_PICKER ());
          assert (game.safe);
          auto current_safe = cast_downset<SpecializedDownset> (*game.safe);
          auto safe = skn.solve (current_safe);
          if (safe.has_value ()) {
            game.safe =
                std::make_shared<GenericDownset> (cast_downset<GenericDownset> (safe.value ()));
          }
          else
            game.safe = nullptr;
        },
        UNREACHABLE, posets::vectors::nbools_to_nbitsets (nbitsetbools));
  }

  game.solved = true;

  double solve_time = sw.stop ();
  verb_do (1, vout << "Safety game solved in " << solve_time << " seconds\n");

  return game.safe != nullptr;
}
