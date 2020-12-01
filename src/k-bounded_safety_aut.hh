#pragma once

/* #define PRE_HAT_CACHE */
#define PRE_HAT_ACTION_CACHE

#include <spot/twa/formula2bdd.hh>
#include <spot/twa/twagraph.hh>

/// \brief Wrapper class around bdd to provide an operator< with bool value.
/// The original type returns a bdd.  This is needed to implement a map<> with
/// this as (part of a) key, since map<>s are key-sorted.
class bdd_t : public bdd {
  public:
    bdd_t (bdd b) : bdd { b } {}

    bool operator< (const bdd_t& other) const {
      return id () < other.id ();
    }
};


/// \brief Wrapper class around a UcB to pass as the deterministic safety
/// automaton S^K_N, for N a given UcB.
template <class State, class SetOfStates>
class k_bounded_safety_aut {
  public:
    k_bounded_safety_aut (const spot::twa_graph_ptr& aut, int K,
                          bdd input_support, bdd output_support, int verbose) :
      aut {aut}, K {K},
      input_support {input_support}, output_support {output_support}, verbose {verbose}
    { }

    spot::formula bdd_to_formula (bdd f) const {
      return spot::bdd_to_formula (f, aut->get_dict ());
    }

    bool solve () {
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

    SetOfStates unsafe_states () {
      SetOfStates U;
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
      bdd input_letters = bddtrue;

      while (input_letters != bddfalse) {
        bdd one_input_letter = pick_one_letter (input_letters, input_support);
        bdd output_letters = bddtrue;

        // NOTE: We're forcing iterative union/intersection; do we want to keep
        // all ph/F1i's and give them to the SetOfStates all at once?

        SetOfStates F1i;
        do {
          bdd&& one_output_letter = pick_one_letter (output_letters, output_support);
          SetOfStates&& ph = pre_hat (F, one_input_letter, one_output_letter);
          F1i.union_with (ph);
        } while (output_letters != bddfalse);

        if (verbose > 1)
          std::cout << "maxelts (F1_{"
                    << spot::bdd_to_formula (one_input_letter, aut->get_dict ())
                    << "}) = "
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
    k_bounded_safety_aut (k_bounded_safety_aut&&) = delete;
    k_bounded_safety_aut& operator= (k_bounded_safety_aut&&) = delete;

  private:
    const spot::twa_graph_ptr& aut;
    int K;
    bdd input_support, output_support;
    const int verbose;

#ifdef PRE_HAT_ACTION_CACHE
    using pre_hat_action = std::vector<std::pair<unsigned, bool>>;
    std::map<std::pair<unsigned, bdd_t>, pre_hat_action>
    pre_hat_action_cache;
#endif

#ifdef PRE_HAT_CACHE
    std::map<std::pair<State, bdd_t>, State> pre_hat_cache;
#endif

#ifdef PRE_HAT_CACHE
    State&
#else
    State
#endif
    pre_hat_one_state (const State& m, const bdd_t& io) {
#ifdef PRE_HAT_CACHE
      auto pair_m_io = std::make_pair (m, io);
      try {
        return pre_hat_cache.at (pair_m_io);
      } catch (...) {
        State &f = pre_hat_cache.emplace (pair_m_io, aut->num_states ()).first->second;
#else
        State f (aut->num_states ());
#endif
        for (unsigned p = 0; p < aut->num_states (); ++p) {
          // If there are no transitions from p labeled io, then we propagate
          // the value of the nonexisting sink, which is set to be K - 1.  Note
          // that the value of the sink cannot decrease.
          f[p] = K - 1;
#ifdef PRE_HAT_ACTION_CACHE
          auto& actions = ([this, p, &io] () -> auto& {
            auto pair_p_io = std::make_pair (p, io);
            try {
              return pre_hat_action_cache.at (pair_p_io);
            } catch (...) {
              auto& actions = pre_hat_action_cache[pair_p_io];
              for (const auto& e : aut->out (p)) {
                unsigned q = e.dst;
                if ((e.cond & io) != bddfalse)
                  actions.push_back (std::make_pair (q, aut->state_is_accepting (q)));
              }
              return actions;
            }}) ();

          for (const auto& [q, q_final] : actions) {
            f[p] = (int) std::min ((int) f[p], std::max (-1, (int) m[q] - (q_final ? 1 : 0)));
            // If we reached the minimum possible value, stop going through states.
            if (f[p] == -1)
              break;
          }
#else
          for (const auto& e : aut->out (p)) {
            unsigned q = e.dst;
            if ((e.cond & io) != bddfalse)
              f[p] = (int) std::min ((int) f[p],
                                     std::max (-1,
                                               m[q] - (aut->state_is_accepting (q) ? 1 : 0)));
          }
#endif
        }
        if (verbose > 2)
          std::cout << "pre_hat(" << m << "," << bdd_to_formula (io) << ") = "
                    << f << std::endl;
        return f;
#ifdef PRE_HAT_CACHE
      }
#endif
    }

    // This computes DownwardClose{Pre_hat (m, i, o) | m \in F}, where Pre_hat (m, i, o) is
    // the State f that maps p to
    //    f(p) =  min_(p, <i,o>, q \in Delta) m(q) - CharFunction(B)(q).
    SetOfStates pre_hat (const SetOfStates& F, bdd input_letter, bdd output_letter) {
      SetOfStates pre = F;
      bdd io = input_letter & output_letter;

      pre.apply_inplace ([this, &io] (const auto& m) { return pre_hat_one_state (m, io); });

      // It may happen that pre is not downward closed anymore.
      // See (G(in)->F(out)) * (G(!in)->F(!out)) with set_of_vectors implementation.
      pre.downward_close ();
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


    bdd pick_one_letter (bdd& letter_set, const bdd& support) const {
      bdd one_letter = bdd_satoneset (letter_set,
                                      support,
                                      bddtrue);
      letter_set -= one_letter;
      return one_letter;
    }

};
