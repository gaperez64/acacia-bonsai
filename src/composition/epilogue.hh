#pragma once
#include "configuration.hh"
#include "create_safety_game.hh"
#include "error_msg.hh"
#include "solve_game.hh"
#include "ios_precomputers/standard.hh"

int epilogue (safety_game& r, unsigned Kmax, unsigned Kmin, unsigned Kinc, bdd all_inputs, bdd all_outputs, std::vector<int> init_state, bdd invariant) {
    // TODO: this should either return true (real) or false (unknown). This will require some changes.

    // check stored_result
    // TODO: this branch should be impossible?
    // if (!stored_result) {
    //     error(EXIT_CODE_ERROR, "Error: result should already exist!");
    // }
    //
    // safety_game& r = *stored_result;

    // if the final result was not solved, or it was solved with the wrong invariant (if the IOs precomputer uses it in the first place)
    // then a final solve is needed before calling synthesis
    bool not_fully_solved = ((r.invariant != invariant) && IOS_PRECOMPUTER::supports_invariant);

    // there is a special case of having had found all states to be bounded
    // this should only happen when checking UNREAL, and it means we can
    // return true
    if (r.aut == nullptr) {
        // assert (synth_fname.empty ());
        return true;
    }

    if ((!r.solved) || not_fully_solved) {
        if (!r.solved) verb_do (1, vout << "Not fully solved -> extra solve\n");
        if (not_fully_solved) verb_do (1, vout << "Solved but not with the right invariant -> extra solve\n");
        solve_game (r, Kmax, Kmin, Kinc, all_inputs, all_outputs, init_state, invariant);
    }
    // if there is no safe region: return 0 (not winning)
    return r.safe != nullptr;
}
