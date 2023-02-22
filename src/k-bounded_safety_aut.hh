#pragma once

#undef MAX_CRITICAL_INPUTS
#define MAX_CRITICAL_INPUTS 1

#include <algorithm>
#include <map>
#include <functional>
#include <random>
#include <list>
#include <chrono>

#include <spot/twa/formula2bdd.hh>
#include <spot/twa/twagraph.hh>

#include "utils/bdd_helper.hh"
#include "utils/lambda_ptr.hh"
#include "utils/ref_ptr_cmp.hh"
#include "utils/vector_mm.hh"
#include <utils/verbose.hh>
#include "utils/typeinfo.hh"

#include "vectors.hh"

#include "ios_precomputers.hh"
#include "input_pickers.hh"
#include "actioners.hh"

//#define debug(A...) do { std::cout << A << std::endl; } while (0)
#define debug(A...)
#define debug_(A...) do { std::cout << A << std::endl; } while (0)
//#define debug_(A...)
//#define ASSERT(A...) assert (A)
#define ASSERT(A...)
/// \brief Wrapper class around a UcB to pass as the deterministic safety
/// automaton S^K_N, for N a given UcB.
template <class SetOfStates,
          class IOsPrecomputationMaker,
          class ActionerMaker,
          class InputPickerMaker>
class k_bounded_safety_aut_detail {
    using State = typename SetOfStates::value_type;

  public:
    k_bounded_safety_aut_detail (spot::twa_graph_ptr aut, int Kfrom, int Kto, int Kinc,
                                 bdd input_support, bdd output_support,
                                 const IOsPrecomputationMaker& ios_precomputer_maker,
                                 const ActionerMaker& actioner_maker,
                                 const InputPickerMaker& input_picker_maker) :
      aut {aut}, Kfrom {Kfrom}, Kto {Kto}, Kinc {Kinc},
      input_support {input_support}, output_support {output_support},
      gen {0},
      ios_precomputer_maker {ios_precomputer_maker},
      actioner_maker {actioner_maker},
      input_picker_maker {input_picker_maker}
    { }

    spot::formula bdd_to_formula (bdd f) const {
      return spot::bdd_to_formula (f, aut->get_dict ());
    }

    bool solve () {
      int K = Kfrom;

      // Precompute the input and output actions.
      verb_do (1, vout << "IOS Precomputer..." << std::endl);
      auto inputs_to_ios = (ios_precomputer_maker.make (aut, input_support, output_support)) ();
      // ^ ios_precomputers::detail::standard_container<shared_ptr<spot::twa_graph>, vector<pair<int, int>>>
      verb_do (1, vout << "Make actions..." << std::endl);
      auto actioner = actioner_maker.make (aut, inputs_to_ios, K);
      verb_do (1, vout << "Fetching IO actions" << std::endl);
      auto input_output_fwd_actions = actioner.actions (); // list<pair<bdd, list<action_vec>>>
      verb_do (1, io_stats (input_output_fwd_actions));

      auto safe_vector = utils::vector_mm<char> (aut->num_states (), K - 1);

      for (size_t i = vectors::bool_threshold; i < aut->num_states (); ++i)
        safe_vector[i] = 0;
      SetOfStates F = SetOfStates (State (safe_vector));

      int loopcount = 0;

      utils::vector_mm<char> init (aut->num_states ());
      init.assign (aut->num_states (), -1);
      init[aut->get_init_state_number ()] = 0;

      auto input_picker = input_picker_maker.make (input_output_fwd_actions, actioner);

      do {
        loopcount++;
        verb_do (1, vout << "Loop# " << loopcount << ", F of size " << F.size () << std::endl);

        auto&& input = input_picker (F);
        if (not input.has_value ()) // No more inputs, and we just tested that init was present
        {
            if (true) // TODO command line argument
            {
                synthesis(F, actioner, K);
            }
            return true;
        }
        cpre_inplace (F, *input, actioner);
        if (not F.contains (State (init))) {
          if (K >= Kto)
            return false;
          verb_do (1, vout << "Incrementing K from " << K << " to " << K + Kinc << std::endl);
          K += Kinc;
          actioner.setK (K);
          verb_do (1, {vout << "Adding Kinc to every vector..."; vout.flush (); });
          F = F.apply ([&] (const State& s) {
            auto vec = utils::vector_mm<char> (s.size (), 0);
            for (size_t i = 0; i < vectors::bool_threshold; ++i)
              vec[i] = s[i] + Kinc;
            // Other entries are set to 0 by initialization, since they are bool.
            return State (vec);
          });
          verb_do (1, vout << "Done" << std::endl);
          continue;
        }

        verb_do (1, vout << "Loop# " << loopcount << ", F of size " << F.size () << std::endl);
      } while (1);

      std::abort ();
      return false;
    }

    // Disallow copies.
    k_bounded_safety_aut_detail (k_bounded_safety_aut_detail&&) = delete;
    k_bounded_safety_aut_detail& operator= (k_bounded_safety_aut_detail&&) = delete;

  private:
    spot::twa_graph_ptr aut;
    const int Kfrom, Kto, Kinc;
    bdd input_support, output_support;
    std::mt19937 gen;
    const IOsPrecomputationMaker& ios_precomputer_maker;
    const ActionerMaker& actioner_maker;
    const InputPickerMaker& input_picker_maker;

    // This computes F = CPre(F), in the following way:
    // UPre(F) = F \cap F2
    // F2 = \cap_{i \in I} F1i
    // F1i = \cup_{o \in O} PreHat (F, i, o)
    template <typename Action, typename Actioner>
    void cpre_inplace (SetOfStates& F, const Action& io_action, Actioner& actioner) {

      verb_do (2, vout << "Computing cpre(F) with F = " << std::endl << F);

      const auto& [input, actions] = io_action.get ();
      utils::vector_mm<char> v (aut->num_states (), -1);
      auto vv = typename SetOfStates::value_type (v);
      SetOfStates F1i (std::move (vv));
      bool first_turn = true;
      for (const auto& action_vec : actions) {
          // action_vec : vector<vector<pair<unsigned int, bool>>>
          // -> map each state q to a list of (p, is_q_accepting) tuples
        verb_do (3, vout << "one_output_letter:" << std::endl);

        SetOfStates&& F1io = F.apply ([this, &action_vec, &actioner] (const auto& m) {
          auto&& ret = actioner.apply (m, action_vec, actioners::direction::backward);
          verb_do (3, vout << "  " << m << " -> " << ret << std::endl);
          return std::move (ret);
        });

        if (first_turn) {
          F1i = std::move (F1io);
          first_turn = false;
        }
        else
          F1i.union_with (std::move (F1io));
      }

      F.intersect_with (std::move (F1i));
      verb_do (2, vout << "F = " << std::endl << F);
    }


    // get index of the first dominating element that dominates the vector v
    template<class Vector>
    int get_dominated_index(const SetOfStates& antichain, const Vector& v) const
    {
        int i = 0;
        auto it = antichain.begin();
        while (it != antichain.end())
        {
            if (SetOfStates((*it).copy()).contains(v))
            {
                break;
            }

            i++;
            ++it;
        }
        assert((size_t)i < antichain.size()); // fails if no i is found: will be equal to size
        return i;
    }

    bdd binary_encode(unsigned int s, const std::vector<bdd>& src) // ~ bdd_buildcube(s, src.size(), src.data())
    {
        // turn the value into a BDD e.g. with 4 states so 2 variables:
        // state 0: !x1 & !x2
        // state 1:  x1 & !x2
        // state 2: !x1 &  x2
        // state 3:  x1 &  x2

        bdd res = bddtrue;
        for(const bdd& var: src)
        {
            // use least significant bit for first variable, next bit for second variable, and so on
            bool negate = (s & 1) == 0;
            s >>= 1;
            res &= negate ? (!var) : var;
        }
        assert(s == 0);
        return res;
    }

    bdd encode_state(const SetOfStates& m, const SetOfStates& antichain, const std::vector<bdd>& state_vars)
    {
        assert(m.size() == 1); // m is a single state in a set
        auto& state = *m.begin();
        int index = get_dominated_index(antichain, state); // get_index(antichain, state);
        return binary_encode(index, state_vars);
    }

    // this can't be the best way to do this..
    std::vector<bdd> cube_to_vector(const bdd& cube)
    {
        std::vector<bdd> res;
        for(int i = 0; i < bdd_varnum(); i++)
        {
            bdd var = bdd_ithvar(i);
            if (cube == (cube & var))
            {
                res.push_back(var);
            }
        }
        return res;
    }

    template<typename Antichain, typename Actioner>
    void synthesis(Antichain& F, Actioner& actioner, int K)
    {
        utils::vout << "Final F:\n" << F;
        utils::vout << "= antichain of size " << F.size();

        // create APs to encode the mapping of the automaton states to integers
        // number of variables to encode the state
        unsigned int mapping_bits = ceil(log2(F.size()));
        assert(F.size() <= (1ull << mapping_bits));
        utils::vout << " -> " << mapping_bits << " bit(s)\n\n";

        // create atomic propositions
        std::vector<bdd> state_vars, state_vars_prime;
        bdd state_vars_cube = bddtrue, state_vars_prime_cube = bddtrue;
        for(unsigned int i = 0; i < mapping_bits; i++)
        {
            unsigned int v = aut->register_ap(spot::formula::ap("Y"+std::to_string(i)));
            state_vars.push_back(bdd_ithvar(v)); // store v instead of the bdd object itself?
            state_vars_cube &= bdd_ithvar(v);

            v = aut->register_ap(spot::formula::ap("Z"+std::to_string(i)));
            state_vars_prime.push_back(bdd_ithvar(v));
            state_vars_prime_cube &= bdd_ithvar(v);
        }

        bdd encoding = bddfalse;

        auto input_output_fwd_actions = actioner.actions();

        int k = 0;
        // for every dominating element m
        for(auto& m: F)
        {
            utils::vout << "Elem " << k++ << "\n";
            SetOfStates m_in_set = SetOfStates(m.copy());
            bdd state_encoding = encode_state(m_in_set, F, state_vars);
            bdd IOXp_encoding = bddfalse; // encoding of I, O, and X'

            // for every input i
            for(auto& tuple: input_output_fwd_actions)
            {
                // .first = input (BDD)
                // .second = list<action_vec>
                //  -> for this input, a list (one per compatible IO) of actions
                //  where an action maps each state q to a list of (p, is_q_accepting) tuples
                //  + the action includes the IO
                //  these actions are used in act_cpre
                utils::vout << "Input: " << bdd_to_formula(tuple.first) << "\n";

                // add all compatible IOs that keep us in the antichain (+ encoding of destination state)
                IOXp_encoding |= act_cpre(m_in_set, tuple.second, actioner, F, state_vars_prime);
                utils::vout << "\n";
            }
            // add encoding for current state and add to total BDD
            encoding |= state_encoding & IOXp_encoding;
            utils::vout << "\n\n";
        }

        utils::vout << "Resulting BDD:\n" << bdd_to_formula(encoding) << "\n\n";


        // g_o1(I, L) g_o2 ... maken: g_o1 geeft gegeven een input en Latch (= huidige state: welk dominating element)
        // of o1 true moet zijn of niet
        // f_L1(I, L) f_L2 ... geeft wat de nieuwe state is, één functie per variable (bv 4 dom elements -> 2 vars, X en X')
        std::vector<bdd> output_vector = cube_to_vector(output_support);

        // assumes deterministic policy?
        for(const bdd& o: output_vector)
        {
            bdd g_o = bdd_exist(encoding & o, output_support & state_vars_prime_cube);
            utils::vout << "g_" << bdd_to_formula(o) << ": " << bdd_to_formula(g_o) << "\n";
        }

        for(const bdd& m: state_vars_prime)
        {
            bdd f_l = bdd_exist(encoding & m, output_support & state_vars_prime_cube);
            utils::vout << "f_" << bdd_to_formula(m) << ": " << bdd_to_formula(f_l) << "\n";
        }

        // initial state
        auto init_vector = utils::vector_mm<char> (aut->num_states(), -1);
        //for (size_t i = vectors::bool_threshold; i < aut->num_states (); ++i)
        //    init_vector[i] = 0;
        init_vector[aut->get_init_state_number()] = 0;
        utils::vout << "Initial vector: " << State(init_vector) << "\n";
        int init_index = get_dominated_index(F, State(init_vector));
        utils::vout << "Initial state: " << bdd_to_formula(binary_encode(init_index, state_vars)) << " (index " << init_index << ")\n";

        utils::vout << "\n\n\n";

        //
        // https://github.com/gaperez64/AbsSynthe/blob/native-dev-par/source/algos.cpp#L129
        bdd care_set = bddfalse; // encoding for states we care about?
        for(unsigned int s = 0; s < F.size(); s++)
        {
            care_set |= binary_encode(s, state_vars); // | binary_encode(s, state_vars_prime);
        }

        bdd strategy = encoding;

        std::vector<bdd> inputs = output_vector;
        //std::vector<unsigned> c_input_lits;
        std::vector<bdd> c_input_funs;

        for(const bdd& c: inputs)
        {
            bdd others_cube = bddtrue;
            unsigned others_count = 0;

            for(const bdd& j: inputs)
            {
                if (c == j) continue;
                others_cube &= j;
                others_count++;
            }

            bdd c_arena;
            if (others_count > 0)
            {
                c_arena = bdd_exist(strategy, others_cube);
            }
            else
            {
                c_arena = strategy;
            }

            bdd can_be_true = bdd_restrict(c_arena, c); // positive cofactor
            bdd can_be_false = bdd_restrict(c_arena, !c); // negative cofactor

            bdd must_be_true = (!can_be_false) & can_be_true;
            bdd must_be_false = (!can_be_true) & can_be_false;

            bdd local_care_set = care_set & (must_be_true | must_be_false);

            bdd res = bdd_restrict(must_be_true, local_care_set); // opt1
            //bdd opt2 = bdd_restrict(!must_be_false, local_care_set);

            //strategy = bdd_compose(strategy, res, c);
            strategy &= (!c | res) & (!res | c);
            c_input_funs.push_back(res);

            utils::vout << "c = " << bdd_to_formula(c) << ": " << bdd_to_formula(res) << ", strat = " << bdd_to_formula(strategy) << "\n";
        }
    }

    template <typename Actions, typename Actioner>
    bdd act_cpre(const SetOfStates& m, const Actions& actions, Actioner& actioner, const SetOfStates& antichain, std::vector<bdd> state_vars_prime)
    {
        assert(m.size() == 1); // m is a single state in a set

        bool dominated = false;
        bdd transitions = bddfalse;

        // action_vec maps each state q to a list of (p, is_q_accepting) tuples (vector<vector<tuple<unsigned int, bool>>>)
        for(const auto& action_vec: actions)
        {
            // calculate fwd(m, action), see if this is dominated by some element in the antichain
            SetOfStates&& fwd = m.apply ([this, &action_vec, &actioner] (const auto& _m) {
                auto&& ret = actioner.apply (_m, action_vec, actioners::direction::forward);
                verb_do (3, vout << "  " << _m << " -> " << ret << std::endl);
                return std::move (ret);
            });

            assert(fwd.size() == 1);

            if (antichain.contains(*fwd.begin()))
            {
                dominated = true;
                utils::vout << "dominated with IO = " << bdd_to_formula(action_vec.IO) << ": " << fwd;
                transitions |= action_vec.IO & encode_state(fwd, antichain, state_vars_prime);
                // return transitions; <- for deterministic policy using first IO that is found
            }
        }

        assert(dominated);
        return transitions;
    }


    template <typename IToActions>
    void io_stats (const IToActions& inputs_to_actions) {
      size_t all_io = 0;
      for (const auto& [inputs, ios] : inputs_to_actions) {
        verb_do (1, vout << "INPUT: " << bdd_to_formula (inputs)
                 /*   */ <<  " #ACTIONS: " << ios.size () << std::endl);
        all_io += ios.size ();
      }
      auto ins = input_support;
      size_t all_inputs_size = 1;
      while (ins != bddtrue) {
        all_inputs_size *= 2;
        ins = bdd_high (ins);
      }

      auto outs = output_support;
      size_t all_outputs_size = 1;
      while (outs != bddtrue) {
        all_outputs_size *= 2;
        outs = bdd_high (outs);
      }

      utils::vout << "INPUT GAIN: " << inputs_to_actions.size () << "/" << all_inputs_size
                  << " = " << (inputs_to_actions.size () * 100 / all_inputs_size) << "%\n"
                  << "IO GAIN: " << all_io << "/" << all_inputs_size * all_outputs_size
                  << " = " << (all_io * 100 / (all_inputs_size * all_outputs_size)) << "%"
                  << std::endl;
    }
};

template <class SetOfStates,
          class IOsPrecomputationMaker,
          class ActionerMaker,
          class InputPickerMaker>
static auto k_bounded_safety_aut_maker (const spot::twa_graph_ptr& aut, int Kfrom, int Kto, int Kinc,
                                        bdd input_support, bdd output_support,
                                        const IOsPrecomputationMaker& ios_precomputer_maker,
                                        const ActionerMaker& actioner_maker,
                                        const InputPickerMaker& input_picker_maker) {
  return k_bounded_safety_aut_detail<SetOfStates, IOsPrecomputationMaker, ActionerMaker, InputPickerMaker>
    (aut, Kfrom, Kto, Kinc, input_support, output_support, ios_precomputer_maker, actioner_maker, input_picker_maker);
}

template <class SetOfStates>
static auto k_bounded_safety_aut (const spot::twa_graph_ptr& aut, int Kfrom, int Kto, int Kinc,
                                  bdd input_support, bdd output_support) {
  return k_bounded_safety_aut_maker<SetOfStates> (aut, Kfrom, Kto, Kinc,
                                                  input_support, output_support,
                                                  IOS_PRECOMPUTER (),
                                                  ACTIONER (),
                                                  INPUT_PICKER ()
    );
}
