#pragma once

#include <spot/twa/formula2bdd.hh>
#include <spot/twa/twagraph.hh>


/// \brief Wrapper class around a UcB to pass as the deterministic safety
/// automaton S^K_N, for N a given UcB.
template <class State, class SetOfStates>
class k_bounded_safety_aut {
  public:
    k_bounded_safety_aut (const spot::twa_graph_ptr& aut, size_t K, bdd input_support, bdd output_support) :
      aut {aut}, K {K}, input_support {input_support}, output_support {output_support} {
    }

    bool solve (int verbose = 0) {
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
        cpre_inplace (F, verbose);
        std::cout << "Loop# " << loopcount << ", F of size " << F.size () << std::endl;
      } while (F.updated ());

      if (F.contains (init))
        return true; // Alice wins.
      else
        return false;
    }

    SetOfStates unsafe_states () {
      SetOfStates U (aut->num_states ());
      State f (aut->num_states ());
      for (unsigned src = 0; src < aut->num_states (); ++src)
        f[src] = -1;

      for (unsigned src = 0; src < aut->num_states (); ++src) {
        f[src] = K;
        U.insert (f);
        f[src] = -1;
      }

      U.upward_close (K);
      return U;
    }

    SetOfStates safe_states () {
      SetOfStates S (aut->num_states ());
      State f (aut->num_states ());

      for (unsigned src = 0; src < aut->num_states (); ++src)
        f[src] = K - 1;
      S.insert (f);

      S.downward_close (K);
      return S;
    }

    // This computes F = CPre(F), in the following way:
    // UPre(F) = F \cap F2
    // F2 = \cap_{i \in I} F1i
    // F1i = \cup_{o \in O} PreHat (F, i, o)
    void cpre_inplace (SetOfStates& F, int verbose = 0) {
      bdd input_letters = bddtrue;
      SetOfStates F2 (aut->num_states ());

      if (verbose)
        std::cout << "Computing cpre(F) with maxelts (F) = " << std::endl
                  << F.max_elements (K);

      bool first_input = true;

      while (input_letters != bddfalse) {
        bdd one_input_letter = pick_one_letter (input_letters, input_support);
        bdd output_letters = bddtrue;

        // NOTE: We're forcing iterative union/intersection; do we want to keep
        // all ph/F1i's and give them to the SetOfStates all at once?

        SetOfStates F1i (aut->num_states ());
        do {
          bdd&& one_output_letter = pick_one_letter (output_letters, output_support);
          SetOfStates&& ph = pre_hat (F, one_input_letter, one_output_letter, verbose);
          F1i.union_with (ph);
        } while (output_letters != bddfalse);

        if (verbose)
          std::cout << "maxelts (F1_{"
                    << spot::bdd_to_formula (one_input_letter, aut->get_dict ())
                    << "}) = "
                    << std::endl << F1i.max_elements (K);
        if (first_input) {
          F2 = std::move (F1i);
          first_input = false;
        } else
          F2.intersect_with (F1i);
        if (F2.empty ())
          break;
      }

      if (verbose)
        std::cout << "maxelts (F2) = " << std::endl << F2.max_elements (K);

      F.intersect_with (F2);
      if (verbose)
        std::cout << "maxelts (F) = " << std::endl << F.max_elements (K);
    }

    // Disallow copies.
    k_bounded_safety_aut (k_bounded_safety_aut&&) = delete;
    k_bounded_safety_aut& operator= (k_bounded_safety_aut&&) = delete;

  private:
    const spot::twa_graph_ptr& aut;
    size_t K;
    bdd input_support, output_support;

    // This computes DownwardClose{Pre_hat (m, i, o) | m \in F}, where Pre_hat (m, i, o) is
    // the State f that maps p to
    //    f(p) =  min_(p, <i,o>, q \in Delta) m(q) - CharFunction(B)(q).
    SetOfStates pre_hat (const SetOfStates& F, bdd input_letter, bdd output_letter, int verbose = 0) {
      SetOfStates pre = F;
      bdd io = input_letter & output_letter;

      pre.apply([this, &io, verbose](auto& m) {
        State f (aut->num_states ());

        for (unsigned p = 0; p < aut->num_states (); ++p) {
          f[p] = K;
          for (const auto& e : aut->out (p)) {
            unsigned q = e.dst;
            if ((e.cond & io) != bddfalse) {
              if (m[q] <= 0)
                f[p] = std::min (f[p], m[q]);
              else
                f[p] = std::min (f[p], m[q] - (aut->state_is_accepting (q) ? 1 : 0));
            }
            // If we reached the minimum possible value, stop going through states.
            if (f[p] == -1)
              break;
          }
        }
        if (verbose > 1)
          std::cout << "pre_hat(" << m << "," << spot::bdd_to_formula (io, aut->get_dict ()) << ") = "
                    << f << std::endl;
        return f;
      });

      // NOTE: Can we avoid this?
      pre.downward_close (K);
      return pre;
    }

    // T = { (f, a, g) | g(q) = max_(p,a,q)\in\Delta f(p) + CharFun(B)(q).
    State T (const State& f, bdd a) {
      State g (aut->num_states ());

      for (unsigned q = 0; q < aut->num_states (); ++q)
        g[q] = -1;

      for (unsigned p = 0; p < aut->num_states (); ++p)
        for (const auto& e : aut->out (p))
          g[e.dst] = std::max (g[e.dst], f[p] + (aut->state_is_accepting (e.dst) ? 1 : 0));

      return g;
    }


    bdd pick_one_letter (bdd& letter_set, const bdd& support) {
      bdd one_letter = bdd_satoneset (letter_set,
                                      support,
                                      bddtrue);
      letter_set -= one_letter;
      return one_letter;
    }

};
