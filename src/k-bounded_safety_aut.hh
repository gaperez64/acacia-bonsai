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

    bool solve (bool verbose = false) {
      if (verbose)
        std::cout << "Computing the set of unsafe states." << std::endl;

      SetOfStates&& F = unsafe_states ();
      State init (aut->num_states ());

      for (unsigned p = 0; p < aut->num_states (); ++p)
        init[p] = -1;
      init[aut->get_init_state_number ()] = 0;
      bool realizable = true;
      int loopcount = 0;
      do {
        loopcount++;
        if (verbose) {
          std::cout << "Loop# " << loopcount << ", F of size " << F.size () << std::endl;
          std::cout << F;
        }
        if (F.contains (init)) {
          // Alice loses.
          realizable = false;
          break;
        }
        F.clear_update_flag ();
        upre_inplace (F, verbose);
      } while (F.updated ());

      return realizable;
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

    // This computes F = UPre(F), in the following way:
    // UPre(F) = F U F2
    // F2 = \cup_{i \in I} F1i
    // F1i = \cap_{o \in O} PreHat (F, i, o)
    void upre_inplace (SetOfStates& F, bool verbose = false) {
      bdd input_letters = bddtrue;
      SetOfStates F2 (aut->num_states ());

      if (verbose)
        std::cout << "Computing upre(F) with F:" << std::endl << F;

      while (input_letters != bddfalse) {
        bdd one_input_letter = pick_one_letter (input_letters, input_support);
        bdd output_letters = bddtrue;
        SetOfStates F1i = pre_hat (F, one_input_letter,
                                   pick_one_letter (output_letters, output_support),
                                   verbose);
        while (output_letters != bddfalse && not F1i.empty ()) {
          bdd&& one_output_letter = pick_one_letter (output_letters, output_support);
          SetOfStates&& ph = pre_hat (F, one_input_letter, one_output_letter, verbose);
          F1i.intersect_with (ph);
        }
        if (verbose)
          std::cout << "F1_" << spot::bdd_to_formula (one_input_letter, aut->get_dict ()) << " = "
                    << std::endl << F1i;
        // NOTE: We're forcing iterative union; do we want to keep all F1i's and
        // give them to the SetOfStates all at once?
        F2.union_with (F1i);
      }

      if (verbose)
        std::cout << "F2 = " << std::endl << F2;

      F.union_with (F2);
    }

    // Disallow copies.
    k_bounded_safety_aut (k_bounded_safety_aut&&) = delete;
    k_bounded_safety_aut& operator= (k_bounded_safety_aut&&) = delete;

  private:
    const spot::twa_graph_ptr& aut;
    size_t K;
    bdd input_support, output_support;

    // This computes {Pre_hat (f, i, o) | m \in F}, where Pre_hat (f, i, o) is
    // the State that maps p to
    //      max_(p, <i,o>, q \in Delta) f(q) - CharFunction(B)(q).
    SetOfStates pre_hat (const SetOfStates& F, bdd input_letter, bdd output_letter, bool verbose = false) {
      SetOfStates pre = F;
      bdd io = input_letter & output_letter;

      pre.apply([this, &io, verbose](auto& f) {
        State ret (aut->num_states ());

        for (unsigned p = 0; p < aut->num_states (); ++p) {
          ret[p] = -1;
          for (const auto& e : aut->out (p)) {
            unsigned q = e.dst;
            if (f[q] < 0)
              continue;
            if ((e.cond & io) != bddfalse) {
              if (f[q] == 0)
                ret[p] = std::max (ret[p], 0);
              else
                ret[p] = std::max (ret[p], f[q] - (aut->state_is_accepting (q) ? 1 : 0));
            }
          }
        }
        if (verbose)
          std::cout << "pre_hat(" << f << "," << spot::bdd_to_formula (io, aut->get_dict ()) << ") = "
                    << ret << std::endl;
        return ret;
      });

      return pre;
    }

    bdd pick_one_letter (bdd& letter_set, const bdd& support) {
      bdd one_letter = bdd_satoneset (letter_set,
                                      support,
                                      bddtrue);
      letter_set -= one_letter;
      return one_letter;
    }

};
