#pragma once

#include <vector>
#include <string>

#include <spot/twaalgos/translate.hh>

#include "composition/composition_mt.hh"


inline spot::parsed_formula parse_formula(const std::string& s)
{
  // TODO: only do infix
  // if (lbt_input)
  //   return spot::parse_prefix_ltl(s);
  // else
    return spot::parse_infix_psl
      (s, spot::default_environment::instance(), false, false);
}

inline spot::formula process_ltl_string(const std::string& input)
  {
    auto pf = parse_formula(input);

    if (!pf.f || !pf.errors.empty())
    {
      error(0, 0, "parse error:");
      pf.format_errors(std::cerr);
      exit(1);
    }

    return pf.f;
  }


// TODO: remove this class
class ltl_processor final {
  private:
    spot::translator &trans_;
    std::vector<std::string> input_aps_;
    std::vector<std::string> output_aps_;
    spot::bdd_dict_ptr dict;
    std::string synth_fname_;
    std::string winreg_fname_;
    unsigned opt_K_;
    unsigned opt_Kmin_;
    unsigned opt_Kinc_;
    std::vector<int> init_state_;
    std::string formula_;

  public:

    ltl_processor (spot::translator &trans,
                   std::vector<std::string> input_aps_,
                   std::vector<std::string> output_aps_,
                   spot::bdd_dict_ptr dict_,
                   std::string synth_fname_,
                   std::string winreg_fname_,
                   unsigned opt_K_,
                   unsigned opt_Kmin_,
                   unsigned opt_Kinc_,
                   std::vector<int> init_state_,
                   std::string formula)
      : trans_ (trans), input_aps_ (input_aps_), output_aps_ (output_aps_), dict (dict_),
        synth_fname_(synth_fname_), winreg_fname_(winreg_fname_), opt_K_(opt_K_), opt_Kmin_(opt_Kmin_),
        opt_Kinc_(opt_Kinc_), init_state_(init_state_), formula_(formula)
    {}

    int run () {
      spot::formula formula = process_ltl_string(formula_);

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

      return composer.run_one (formula, synth_fname_, winreg_fname_);
    }

    ~ltl_processor () {
      dict->unregister_all_my_variables (this);
    }
};