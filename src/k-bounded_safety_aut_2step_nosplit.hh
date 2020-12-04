#pragma once

/* #define PRE_HAT_CACHE */
#define PRE_HAT_ACTION_CACHE

#include <spot/twa/formula2bdd.hh>
#include <spot/twa/twagraph.hh>

/// \brief Wrapper class around a UcB to pass as the deterministic safety
/// automaton S^K_N, for N a given UcB.
template <class State, class SetOfStates>
class k_bounded_safety_aut_2step_nosplit {
  public:
    k_bounded_safety_aut_2step_nosplit (const spot::twa_graph_ptr& aut, int K,
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
        omega_actions actions;
        bdd output_letters = bddtrue;
        while (output_letters != bddfalse) {
          bdd one_output_letter = pick_one_letter (output_letters, output_support);
          actions.insert (compute_omega_action (one_input_letter & one_output_letter));
        }
        input_output_actions.insert (std::move (actions));
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
        cpre_inplace (F);
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

    // This computes F = CPre(F), in the following way:
    // UPre(F) = F \cap F2
    // F2 = \cap_{i \in I} F1i
    // F1i = \cup_{o \in O} PreHat (F, i, o)
    void cpre_inplace (SetOfStates& F) {
      SetOfStates F2;

      if (verbose > 1)
        std::cout << "Computing cpre(F) with maxelts (F) = " << std::endl
                  << F.max_elements ();

      bool first_input = true;
      for (const auto& actions : input_output_actions) {
        SetOfStates F1i;
        for (const auto& action_vec : actions) {
          F1i.union_with (F.apply ([this, &action_vec] (const auto& m) {
            return omega (m, action_vec);
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

    // Disallow copies.
    k_bounded_safety_aut_2step_nosplit (k_bounded_safety_aut_2step_nosplit&&) = delete;
    k_bounded_safety_aut_2step_nosplit& operator= (k_bounded_safety_aut_2step_nosplit&&) = delete;

  private:
    const spot::twa_graph_ptr& aut;
    int K;
    bdd input_support, output_support;
    const int verbose;

    using omega_action = std::vector<std::pair<unsigned, bool>>; // All these pairs are unique by construction.
    using omega_action_vec = std::vector<omega_action>;          // Vector indexed by state number
    using omega_actions = std::set<omega_action_vec>;            // Could be set or vector
    using omega_actions_set = std::set<omega_actions>;           // Could be set or vector
    omega_actions_set input_output_actions;

    omega_action_vec compute_omega_action (bdd letter) {
      omega_action_vec ret (aut->num_states ());

      if (verbose > 1)
        std::cout << "Computing Omega(., "
                  << bdd_to_formula (letter) << ")." << std::endl;
      for (unsigned p = 0; p < aut->num_states (); ++p) {
        if (verbose > 1)
          std::cout << p << " -> ";
        for (const auto& e : aut->out (p)) {
          unsigned q = e.dst;
          if ((e.cond & letter) != bddfalse) {
            ret[p].push_back (std::make_pair (q, aut->state_is_accepting (q)));
            if (verbose > 1)
              std::cout << "[" << q << (aut->state_is_accepting (q) ? "-1] " : "] ");
          }
        }
        if (verbose > 1)
          std::cout << std::endl;
      }
      return ret;
    }

    State omega (const State& m, const omega_action_vec& action_vec) {
      State f (aut->num_states ());

      for (unsigned p = 0; p < aut->num_states (); ++p) {
        f[p] = K - 1;
        for (const auto& [q, q_final] : action_vec[p]) {
          f[p] = (int) std::min ((int) f[p], std::max (-1, (int) m[q] - (q_final ? 1 : 0)));
          // If we reached the minimum possible value, stop going through states.
          if (f[p] == -1)
            break;
        }
      }
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
