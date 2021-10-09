      // Step 1: POW(trans)
      int nand = 0;
      std::list<bdd> crossings;
      crossings.push_front (bddtrue); // Start with "true": Take all transitions.
      for (size_t p = 0; p < aut->num_states (); ++p)
        for (const auto& e : aut->out (p)) {
          auto it = crossings.begin ();

          while (it != crossings.end ()) {
            auto cros_and_cond = *it & e.cond;
            if (cros_and_cond != bddfalse) {
              auto cros_and_notcond = *it & !e.cond;
              if (cros_and_notcond != bddfalse) { // SPLIT
                crossings.push_front (cros_and_cond);
                crossings.push_front (cros_and_notcond);
                it = crossings.erase (it);
              }
              else
                ++it; // TALKING POINT?
            }
            else
              ++it;
          }
        }
      std::cout << "NANDS AFTER POW: " << nand << " CROS : " << crossings.size () << std::endl;
#if 0
      std::map<bdd_t, std::set<bdd_t>> inputs_to_ios;
      inputs_to_ios[bddtrue] = std::set<bdd_t> ();
      for (auto crossing : crossings) {
        auto inputs = bdd_exist (crossing, output_support);

        decltype (inputs_to_ios) new_inputs_to_ios;
        for (auto [i, ios] : inputs_to_ios) {
          auto i_and_inputs = i & inputs;
          if (i_and_inputs != bddfalse) {
            new_inputs_to_ios[i_and_inputs] = ios;
            new_inputs_to_ios[i_and_inputs].insert (crossing);
          }

          auto i_and_notinputs = i & !inputs;
          if (i_and_notinputs != bddfalse)
            new_inputs_to_ios[i_and_notinputs] = ios;
        }
        inputs_to_ios = std::move (new_inputs_to_ios);
      }
#endif

#if 0
      /* Problem: Given S a set of BDDs, x a BDD
         Compute: { y \in S | x & y != bddfalse }

         S = { x1, x2, x3 }  -> F1, F2, F3 S = F1 & x1 | F2 & x2 | F3 & x3

         S & y = F1 & x1 & y
          proj -> F1
         S[F1=0]
         12 input prop -> 4096 possib.
         4096 crossings -> 4096^2
       */

      // Step 2: Same thing with inputs. ATTEMPT 2: IF INPUT EXISTS, DON'T GO THROUGH INPUTS
       std::map<bdd_t, std::list<bdd>> inputs_to_ios;
      inputs_to_ios[bddtrue] = std::list<bdd> ();
      int nsplit = 0 ;
      for (auto crossing : crossings) {
        auto inputs = bdd_exist (crossing, output_support);
        auto it = inputs_to_ios.find (inputs);
        if (it != inputs_to_ios.end ()) {
          it->second.push_front (crossing);
          continue;
        }
        std::list<decltype (inputs_to_ios)::value_type> to_add;
        it = inputs_to_ios.begin ();
        while (it != inputs_to_ios.end ()) {
          auto i_and_inputs = it->first & inputs;
          nand++;
          if (i_and_inputs != bddfalse) {
            if (i_and_inputs != it->first) { // SPLIT
              auto i_and_notinputs = it->first & !inputs;
              nand++;
              nsplit++;
              to_add.push_front (std::pair (i_and_notinputs,
                                            it->second));
              it->second.push_front (crossing);
              to_add.push_front (std::pair (i_and_inputs,
                                            std::move (it->second)));
              auto it_save = it++;
              inputs_to_ios.erase (it_save);
            }
            else {
              it->second.push_front (crossing);
              ++it;
            }
          }
          else {
            to_add.push_front (std::pair (it->first & !inputs,
                                          std::move (it->second)));
            auto it_save = it++;
            inputs_to_ios.erase (it_save);

            //it++; // could add !inputs to i?
          }
        }
        for (auto& e : to_add)
          inputs_to_ios.emplace (e.first, std::move (e.second));
       }
#endif

#if 1
      /////////////////////////////////////
      // Step 2: Have fake vars added
      std::map<bdd_t, std::list<bdd>> inputs_to_ios;
      inputs_to_ios[bddtrue] = std::list<bdd> ();

      int cur_var = 0;
      int var_available = 0;
      std::vector<bdd> fvar_to_input;
      bdd all_fvars = bddtrue;

#define MORE_VARS 1024
      auto get_fresh_var = [&] () {
        if (var_available--)
          return ++cur_var;
        var_available = MORE_VARS - 1;
        auto new_var = bdd_extvarnum (MORE_VARS);
        ASSERT (cur_var == 0 or cur_var == new_var - 1);
        cur_var = new_var;
        for (int i = cur_var; i < cur_var + MORE_VARS; ++i)
          all_fvars &= bdd_ithvar (i);
        fvar_to_input.resize (cur_var + MORE_VARS);
        return cur_var;
      };

      int true_fvar = get_fresh_var ();
      bdd all_inputs_with_fvars = bdd_ithvar (true_fvar);
      fvar_to_input[true_fvar] = bddtrue;

      /*      bdd all_fvars = bddtrue;
      for (size_t i = 0; i < all_inputs_size; ++i) {
        int v = aut->register_ap (spot::formula::ap("IN-" + std::to_string (i)));
        all_fvars &= bdd_ithvar (v);
      }
      bdd fvars_available = all_fvars;
      int cur_var = bdd_var (fvars_available);
      fvars_available = bdd_high (fvars_available);

      std::map<int, bdd> fvar_to_input;

      bdd all_inputs_with_fvars = bdd_ithvar (cur_var);
      // bdd all_fvars = bdd_ithvar (cur_var);
      fvar_to_input[cur_var] = bddtrue;
       */

      for (auto crossing : crossings) {
        auto crossing_inputs = bdd_exist (crossing, output_support);
        auto fvars = bdd_existcomp (crossing_inputs & all_inputs_with_fvars, all_fvars);
        debug_ ("CROSSING: " << bdd_to_formula (crossing));
        debug ("ALL_IN_W_FVARS: " << bdd_to_formula (all_inputs_with_fvars));
        debug ("ALL_FVARS: " << bdd_to_formula (all_fvars));

        auto not_crossing_inputs = !crossing_inputs;
        // fvars is a disjunction of fake vars.
        while (fvars != bddfalse) {
          debug ("fvars: " << bdd_to_formula (fvars));
          auto var = bdd_var (fvars);
          debug ("Asking " << var);
          auto input = fvar_to_input[var];
          auto input_and_not_crossing_inputs = input & not_crossing_inputs;
          if (input_and_not_crossing_inputs != bddfalse) { // Split
            auto input_and_crossing_inputs = input & crossing_inputs;
            ASSERT (input_and_crossing_inputs != input && input_and_crossing_inputs != bddfalse);
            ASSERT (input == bdd_existcomp (input, input_support));
            debug ("splitting " << bdd_to_formula (input)
                    << " into " << bdd_to_formula (input_and_crossing_inputs)
                    << " and " << bdd_to_formula (input_and_not_crossing_inputs));;
            auto it = inputs_to_ios.find (input);
            ASSERT (it != inputs_to_ios.end ());
            inputs_to_ios.emplace (input_and_not_crossing_inputs, it->second);
            it->second.push_back (crossing);
            inputs_to_ios.emplace (input_and_crossing_inputs, std::move (it->second));
            inputs_to_ios.erase (it);

            auto ianc_var = get_fresh_var ();
            //fvars_available = bdd_high (fvars_available);
            // all_inputs_with_fvars = bdd_forall (all_inputs_with_fvars, bdd_ithvar (var)); // improve?
            all_inputs_with_fvars = bdd_restrict (all_inputs_with_fvars, bdd_nithvar (var));
            all_inputs_with_fvars |= ((bdd_ithvar (var) & input_and_crossing_inputs) |
                                      (bdd_ithvar (ianc_var) & input_and_not_crossing_inputs));
            //all_fvars &= bdd_ithvar (ianc_var);
            fvar_to_input[var] = input_and_crossing_inputs;
            debug ("Saving " << var);
            fvar_to_input[ianc_var] = input_and_not_crossing_inputs;
            debug ("Saving " << ianc_var);
            ASSERT (input_and_not_crossing_inputs == bdd_existcomp (input_and_not_crossing_inputs, input_support));
            ASSERT (input_and_crossing_inputs == bdd_existcomp (input_and_crossing_inputs, input_support));
          }
          else
            inputs_to_ios[input].push_back (crossing);
#warning Discussion point
          fvars = bdd_low (fvars);
        }
      }
      aut->get_dict ()->unregister_all_my_variables (this);
#endif


        verb_do (1, {
        size_t all_io = 0;
        std::set<bdd_t> all_io_set;
        for (const auto& [inputs, ios] : inputs_to_ios) {
          if (verbose > 1)
            std::cout << "INPUT: " << bdd_to_formula (inputs) << std::endl;
          all_io += ios.size ();
          all_io_set.insert (ios.begin (), ios.end ());
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

        std::cout << "INPUT GAIN: " << inputs_to_ios.size () << "/" << all_inputs_size
                  << " = " << (inputs_to_ios.size () * 100 / all_inputs_size) << "%\n"
                  << "IO GAIN: " << all_io << "/" << all_inputs_size * all_outputs_size
                  << " = " << (all_io * 100 / (all_inputs_size * all_outputs_size)) << "%\n"
                  << "IO W/CACHE: " << all_io_set.size () << " REDOING " << (all_io - all_io_set.size ())
                  << std::endl;
          });
