#pragma once

#include <vector>
#include <string>

#include <spot/twaalgos/translate.hh>

// #include "common_aoutput.hh"
// #include "common_file.hh"
#include "common_finput.hh" // OK
// #include "common_sys.hh"
#include "composition/composition_mt.hh"


// TODO: remove this class
class ltl_processor final : public job_processor {
  private:
    spot::translator &trans_;
    std::vector<std::string> input_aps_;
    std::vector<std::string> output_aps_;
    std::vector<spot::formula> formulas;
    spot::bdd_dict_ptr dict;
    std::string synth_fname_;
    std::string winreg_fname_;
    bool check_real_;
    unreal_x_t opt_unreal_x_;
    int workers_;
    unsigned opt_K_;
    unsigned opt_Kmin_;
    unsigned opt_Kinc_;
    std::vector<int> init_state_;


  public:

    ltl_processor (spot::translator &trans,
                   std::vector<std::string> input_aps_,
                   std::vector<std::string> output_aps_,
                   spot::bdd_dict_ptr dict_,
                   std::string synth_fname_,
                   std::string winreg_fname_,
                   bool check_real_,
                   unreal_x_t opt_unreal_x_,
                   int workers_,
                   unsigned opt_K_,
                   unsigned opt_Kmin_,
                   unsigned opt_Kinc_,
                   std::vector<int> init_state_)
      : trans_ (trans), input_aps_ (input_aps_), output_aps_ (output_aps_), dict (dict_),
        synth_fname_(synth_fname_), winreg_fname_(winreg_fname_), check_real_(check_real_),
        opt_unreal_x_(opt_unreal_x_), workers_(workers_), opt_K_(opt_K_), opt_Kmin_(opt_Kmin_),
        opt_Kinc_(opt_Kinc_), init_state_(init_state_)
    {}

    int process_formula (spot::formula f, const char *, int) override {
      formulas.push_back (f);
      return 0;
    }

    int run () override {
      // call base class ::run which adds the formulas passed with -f to the vector
      job_processor::run ();

      if (formulas.empty ()) {
        utils::vout << "Pass a formula!\n";
        return 0;
      }

      // manually register inputs/outputs
      bdd all_inputs = bddtrue;
      bdd all_outputs = bddtrue;

      for(std::string ap: input_aps_) {
        unsigned v = dict->register_proposition (spot::formula::ap (ap), this);
        all_inputs &= bdd_ithvar (v);
      }
      for(std::string ap: output_aps_) {
        unsigned v = dict->register_proposition (spot::formula::ap (ap), this);
        all_outputs &= bdd_ithvar (v);
      }

      composition_mt composer (opt_K_, opt_Kmin_, opt_Kinc_, dict, trans_, all_inputs, all_outputs, input_aps_, output_aps_,
                               init_state_);

      if (formulas.size () == 1) {
        // one formula: don't make subprocesses, do everything here by calling the functions directly
        return composer.run_one (formulas[0], synth_fname_, winreg_fname_, check_real_, opt_unreal_x_);
      }

      // NOTE: Everything after this point plays a role
      // ONLY if there is MORE THAN ONE LTL formula
      // TODO: purge everything after this.
      if (!check_real_) {
        utils::vout << "Error: can't do composition for unrealizability!\n";
        return 0;
      }

      if (init_state_.size () > 0) {
        utils::vout << "Error: can't do composition with given initial state!\n";
        return 0;
      }

      if (opt_Kinc_ != 0) {
        utils::vout << "Error: can't do composition with incrementing K values!\n";
        return 0;
      }

      for(spot::formula& f: formulas) {
        composer.add_formula (f);
      }

      return composer.run (workers_, synth_fname_, winreg_fname_);
    }

    ~ltl_processor () override {
      dict->unregister_all_my_variables (this);
    }
};