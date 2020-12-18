#pragma once

#undef MAX_CRITICAL_INPUTS
#define MAX_CRITICAL_INPUTS 1

#include <spot/twa/formula2bdd.hh>
#include <spot/twa/twagraph.hh>
#include "bdd_helper.hh"

/// \brief Wrapper class around a UcB to pass as the deterministic safety
/// automaton S^K_N, for N a given UcB.
template <class State, class SetOfStates>
class k_bounded_safety_aut_2step_nosplit_crit {
    enum class direction {
      forward,
      backward
    };

  public:
    k_bounded_safety_aut_2step_nosplit_crit (const spot::twa_graph_ptr& aut, int K,
                          bdd input_support, bdd output_support, int verbose) :
      aut {aut}, K {K},
      input_support {input_support}, output_support {output_support}, verbose {verbose}
    { }

    spot::formula bdd_to_formula (bdd f) const {
      return spot::bdd_to_formula (f, aut->get_dict ());
    }

    bool solve () {
      // Precompute the input and output actions.
      bdd input_letters = bddtrue;
      while (input_letters != bddfalse) {
        bdd one_input_letter = pick_one_letter (input_letters, input_support);
        trans_actions bwd_actions, fwd_actions;
        bdd output_letters = bddtrue;
        while (output_letters != bddfalse) {
          bdd one_output_letter = pick_one_letter (output_letters, output_support);
          const auto& [bwd, fwd] = compute_trans_action (one_input_letter & one_output_letter);
          bwd_actions.insert (std::move (bwd));
          fwd_actions.insert (std::move (fwd));
        }
        input_output_bwd_actions[bwd_actions].insert (one_input_letter);
        input_output_fwd_actions[fwd_actions].insert (one_input_letter);
      }

      if (verbose)
        std::cout << "Computing the set of safe states." << std::endl;

      SetOfStates&& F = safe_states ();
      State init (aut->num_states ());

      for (unsigned p = 0; p < aut->num_states (); ++p)
        init[p] = -1;
      init[aut->get_init_state_number ()] = 0;
      if (verbose)
        std::cout << "Initial state: " << init << std::endl;

      // Compute cpre^*(safe).
      int loopcount = 0;
      do {
        loopcount++;
        if (verbose)
          std::cout << "Loop# " << loopcount << ", F of size " << F.size () << std::endl;
        F.clear_update_flag ();
        auto&& crit = critical_input_output_bwd_actions (F);
        if (verbose)
          std::cout << "Set of critical inputs of size " << crit.size () << std::endl;
        if (crit.empty ())
          break;
        cpre_inplace (F, crit);
        if (verbose)
          std::cout << "Loop# " << loopcount << ", F of size " << F.size () << std::endl;
      } while (F.updated ());

      if (F.contains (init))
        return true; // Alice wins.
      else
        return false;
    }

    SetOfStates safe_states () {
      State f (aut->num_states ());

      // So-called "Optimization 1" in ac+.
      int nb_accepting_states = 0;
      for (unsigned src = 0; src < aut->num_states (); ++src)
        if (aut->state_is_accepting (src))
          nb_accepting_states++;

      auto c = std::vector<int> (aut->num_states ());
      if (aut->state_is_accepting (aut->get_init_state_number ()))
        c[aut->get_init_state_number ()] = 1;

      bool has_changed = true;

      while (has_changed) {
        has_changed = false;

        for (unsigned src = 0; src < aut->num_states (); ++src) {
          int c_src_mod = std::min (nb_accepting_states + 1,
                                    c[src] + (aut->state_is_accepting (src) ? 1 : 0));
          for (const auto& e : aut->out (src))
            if (c[e.dst] < c_src_mod) {
              c[e.dst] = c_src_mod;
              has_changed = true;
            }
        }
      }

      for (unsigned src = 0; src < aut->num_states (); ++src)
        if (c[src] > nb_accepting_states)
          f[src] = K - 1;
        else
          f[src] = 0;

      SetOfStates S;
      S.insert (f);
      S.downward_close ();

      return S;
    }

    auto critical_input_output_bwd_actions (const SetOfStates& F) {
      // Def: f is one-step-losing if there is an input i such that for all output o
      //           succ (f, <i,o>) \not\in F
      //      the input i is the *witness* of one-step-loss.
      // Def: A set C of inputs is critical for F if:
      //        \exists f \in F, i \in C, i witnesses one-step-loss of F.
      // Algo: We go through all f in F, find an input i witnessing one-step-loss, add it to C.

      using trans_actions_pair_ref = std::reference_wrapper<const trans_actions_map::value_type>;
      std::list<trans_actions_pair_ref> C, Cbar;
      std::set<bdd_t> critical_inputs;

      Cbar.insert (Cbar.begin (),
                   input_output_fwd_actions.begin (),
                   input_output_fwd_actions.end ());

      for (const auto& f : F) {
        bool is_witness = false;
        if (verbose > 2)
          std::cout << "Searching for witness of one-step-loss for " << f << std::endl;

        // Search witness in C
        for (const auto& elt : C) {
          const auto& [output_actions, inputs] = elt.get ();
          is_witness = true;
          for (const auto& action : output_actions)
            if (F.contains (trans (f, action, direction::forward))) {
              is_witness = false;
              break;
            }
          if (is_witness) {
            if (verbose > 2) {
              std::cout << "Already have inputs ";
              std::cout << bdd_to_formula (*inputs.begin ()) << " ";
              std::cout << "as witnesses for " << f << std::endl;
            }
            break;
          }
        }
        if (is_witness) // Witness already in C
          continue;

        // Search witness in the rest of the actions
        for (auto it = Cbar.begin (); it != Cbar.end (); ++it) {
          const auto& [output_actions, inputs] = it->get ();
          is_witness = true;
          for (const auto& action : output_actions)
            if (F.contains (trans (f, action, direction::forward))) {
              is_witness = false;
              break;
            }
          if (is_witness) {
            if (verbose > 2) {
              std::cout << "Adding critical input ";
              std::cout << bdd_to_formula (*inputs.begin ()) << " ";
              std::cout << "as witnesses for " << f << std::endl;
            }
            C.push_back (*it);
            critical_inputs.insert (*inputs.begin ());
            Cbar.erase (it);
            break;
          }
        }
        if (critical_inputs.size () >= MAX_CRITICAL_INPUTS)
          break;
      }

      trans_actions_ref_list ret;
      for (const auto& elt : input_output_bwd_actions) {
        const auto& [output_actions, inputs] = elt;
        if (not empty_intersection (inputs, critical_inputs))
          ret.push_back (std::cref (output_actions));
      }

      if (verbose > 1) {
        std::cout << "Critical inputs: ";
        for (auto bdd : critical_inputs)
          std::cout << "[" << bdd_to_formula (bdd) << "] ";
        std::cout << std::endl;
      }

      return ret;
    }

    // Disallow copies.
    k_bounded_safety_aut_2step_nosplit_crit (k_bounded_safety_aut_2step_nosplit_crit&&) = delete;
    k_bounded_safety_aut_2step_nosplit_crit& operator= (k_bounded_safety_aut_2step_nosplit_crit&&) = delete;

  private:
    const spot::twa_graph_ptr& aut;
    int K;
    bdd input_support, output_support;
    const int verbose;

    using trans_action = std::vector<std::pair<unsigned, bool>>; // All these pairs are unique by construction.
    using trans_action_vec = std::vector<trans_action>;          // Vector indexed by state number

    using trans_actions = std::set<trans_action_vec>;            // Could be set or vector

    using trans_actions_map = std::map<trans_actions, std::set<bdd_t>>; // Maps a set of actions to a set of inputs.

    using trans_actions_ref = std::reference_wrapper<const trans_actions>;
    using trans_actions_ref_list = std::list<trans_actions_ref>;

    trans_actions_map input_output_bwd_actions, input_output_fwd_actions;

    // This computes F = CPre(F), in the following way:
    // UPre(F) = F \cap F2
    // F2 = \cap_{i \in I} F1i
    // F1i = \cup_{o \in O} PreHat (F, i, o)
    void cpre_inplace (SetOfStates& F, const trans_actions_ref_list& io_actions) {
      SetOfStates F2;

      if (verbose > 1)
         std::cout << "Computing cpre(F) with maxelts (F) = " << std::endl
                  << F.max_elements ();

      bool first_input = true;
      for (const auto& actions : io_actions) {
        SetOfStates F1i;
        for (const auto& action_vec : actions.get ()) {
          if (verbose > 2)
            std::cout << "one_output_letter:" << std::endl;
          F1i.union_with (F.apply ([this, &action_vec] (const auto& m) {
            auto&& ret = trans (m, action_vec, direction::backward);
            if (verbose > 2)
              std::cout << "  " << m << " -> " << ret << std::endl;
            return ret;
          }));
        }

        if (verbose > 1)
          std::cout << "maxelts (F1_{one_input_letter}) ="
                    << std::endl << F1i.max_elements ();
        if (first_input) {
          F2 = std::move (F1i);
          first_input = false;
        } else
          F2.intersect_with (F1i);
        if (F2.empty ())
          break;
      }

      if (verbose > 1)
        std::cout << "maxelts (F2) = " << std::endl << F2.max_elements ();

      F.intersect_with (F2);
      if (verbose > 1)
        std::cout << "maxelts (F) = " << std::endl << F.max_elements ();
    }

    auto compute_trans_action (bdd letter) {
      trans_action_vec ret_bwd (aut->num_states ()),
        ret_fwd (aut->num_states ());

      if (verbose > 1)
        std::cout << "Computing T(., "
                  << bdd_to_formula (letter) << ")"
                  << std::endl;

      for (unsigned p = 0; p < aut->num_states (); ++p) {
        for (const auto& e : aut->out (p)) {
          unsigned q = e.dst;
          if ((e.cond & letter) != bddfalse) {
            ret_bwd[p].push_back (std::make_pair (q, aut->state_is_accepting (q)));
            ret_fwd[q].push_back (std::make_pair (p, aut->state_is_accepting (q)));
          }
        }
      }
      return std::make_pair (ret_bwd, ret_fwd);
    }

    State trans (const State& m, const trans_action_vec& action_vec, direction dir) {
      State f (aut->num_states ());

      for (unsigned p = 0; p < aut->num_states (); ++p) {
        if (dir == direction::forward)
          f[p] = -1;
        else
          f[p] = K - 1;

        for (const auto& [q, q_final] : action_vec[p]) {
          if (dir == direction::forward) {
            int mq = m[q];
            if (mq != -1)
              f[p] = (int) std::max ((int) f[p], std::min (K, (int) mq + (q_final ? 1 : 0)));
          } else
            f[p] = (int) std::min ((int) f[p], std::max (-1, (int) m[q] - (q_final ? 1 : 0)));

          // If we reached the extreme values, stop going through states.
          if ((dir == direction::forward  && f[p] == K) ||
              (dir == direction::backward && f[p] == -1))
            break;
        }
      }

      // Monotonic?
      /*
      if (dir == direction::forward) {
        if (not f.partial_order (m).geq ()) {
          std::cout << "succ(" << m << ") = " << f << " (not leq)"  << std::endl;
        }
      }
      else
        if (not m.partial_order (f).geq ())
          std::cout << "prev(" << m << ") = " << f << " (not geq)"  << std::endl;
       */

      return f;
    }

    bdd pick_one_letter (bdd& letter_set, const bdd& support) const {
      bdd one_letter = bdd_satoneset (letter_set,
                                      support,
                                      bddtrue);
      letter_set -= one_letter;
      return one_letter;
    }

};
