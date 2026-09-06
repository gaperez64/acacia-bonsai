#pragma once

/// Independent replay of a forward losing proof.
///
/// `certificate_verifier.hh` re-checks every forward WIN before it is returned.
/// Nothing re-checked a forward LOSE, and that asymmetry is not academic: most
/// of the instances the forward backend uniquely answers on SYNTCOMP 2026 are
/// UNREALIZABLE, so they rest entirely on losing propagation being right.
///
/// This checker recomputes.  It never consults a node's status flag, only the
/// recorded proof and the game itself, so a solver that marked a node losing
/// for a reason it cannot justify is caught rather than believed.  The
/// `ctrl_all_losing` case deliberately re-derives *every* action successor of
/// the controller node: that is the case a lazy forward solver can get wrong by
/// abandoning alternative choices too early, and shortcutting it here would
/// remove the only check on exactly that mistake.

#include "solver/forward_reachable_safety.hh"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

namespace acacia::solver_detail {

  /// Recompute every recorded losing proof and confirm the initial node loses.
  ///
  /// Returns false on the first record that cannot be justified; never throws,
  /// so a caller may treat a false as "do not trust this LOSE" and fall back.
  template <typename State, typename InputOutputFwdActions, typename Actioner>
  bool replay_losing_proofs (
      const std::vector<losing_proof>& proofs, const std::vector<State>& env_ranks,
      const std::vector<std::pair<std::size_t, std::size_t>>& ctrl_parents,
      const State& safe, const InputOutputFwdActions& input_output_fwd_actions,
      Actioner& actioner, std::size_t initial_env) {
    // Which env / ctrl nodes have an established proof, and at which record.
    std::vector<std::size_t> env_proof (env_ranks.size (), 0);
    std::vector<std::size_t> ctrl_proof (ctrl_parents.size (), 0);

    for (const auto& proof : proofs) {
      // Dependencies must already be established.  Combined with the ordering
      // assertion made when the log is appended, this is what keeps the proof
      // graph acyclic: nothing may rest on a fact proved later.
      static const bool trace = std::getenv ("ACACIA_REPLAY_TRACE") != nullptr;
      auto reject = [&] (const char* why) {
        if (trace)
          std::cerr << "replay reject: proof id=" << proof.id << " reason="
                    << static_cast<int> (proof.reason) << " node=" << proof.node
                    << " why=" << why << "\n";
        return false;
      };
      for (const std::size_t dependency : proof.dependencies)
        if (dependency == 0 or dependency >= proof.id)
          return reject ("bad dependency order");

      switch (proof.reason) {
        case losing_reason::env_unsafe: {
          if (proof.node >= env_ranks.size ())
            return false;
          if (env_ranks[proof.node].partial_order (safe).leq ())
            return reject ("safe but claimed unsafe");
          env_proof[proof.node] = proof.id;
          break;
        }

        case losing_reason::env_subsumed: {
          if (proof.node >= env_ranks.size () or proof.witness >= env_ranks.size ())
            return false;
          // The witness must itself be a proved-losing node, proved earlier.
          const std::size_t witness_proof = env_proof[proof.witness];
          if (witness_proof == 0 or witness_proof >= proof.id)
            return false;
          if (not env_ranks[proof.witness].partial_order (env_ranks[proof.node]).leq ())
            return reject ("witness not <= node");
          env_proof[proof.node] = proof.id;
          break;
        }

        case losing_reason::env_losing_input: {
          if (proof.node >= env_ranks.size () or proof.witness >= ctrl_parents.size ())
            return false;
          const std::size_t controller_proof = ctrl_proof[proof.witness];
          if (controller_proof == 0 or controller_proof >= proof.id)
            return false;
          if (ctrl_parents[proof.witness].first != proof.node)
            return reject ("controller parent mismatch");
          env_proof[proof.node] = proof.id;
          break;
        }

        case losing_reason::ctrl_all_losing: {
          if (proof.node >= ctrl_parents.size ())
            return false;
          const auto [parent, input_index] = ctrl_parents[proof.node];
          if (parent >= env_ranks.size ())
            return false;

          // Re-derive every successor of this controller node and require each
          // to be covered by an earlier losing environment proof.  A successor
          // that is safe and unproved means the controller had an option it did
          // not take, and the claim is false.
          std::size_t seen = 0;
          for (const auto& input_and_actions : input_output_fwd_actions) {
            if (seen++ != input_index)
              continue;
            for (const auto& action : input_and_actions.second) {
              const auto image =
                  actioner.apply (env_ranks[parent], action, actioners::direction::forward);
              // An unsafe successor is losing by definition, and the solver
              // skips those without interning them, so they have no node id and
              // no proof record.  Recompute the safety test here rather than
              // demanding a proof that cannot exist -- still recomputing, never
              // trusting the solver.
              if (not image.partial_order (safe).leq ())
                continue;
              bool covered = false;
              for (std::size_t id = 0; id < env_ranks.size (); ++id) {
                const std::size_t established = env_proof[id];
                if (established == 0 or established >= proof.id)
                  continue;
                if (env_ranks[id].partial_order (image).leq ()) {
                  covered = true;
                  break;
                }
              }
              if (not covered)
                return reject ("successor not covered by an earlier losing proof");
            }
            break;
          }
          if (seen <= input_index)
            return reject ("input class missing");
          ctrl_proof[proof.node] = proof.id;
          break;
        }
      }
    }

    return initial_env < env_proof.size () and env_proof[initial_env] != 0;
  }

}  // namespace acacia::solver_detail
