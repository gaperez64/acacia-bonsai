// -*- coding: utf-8 -*-
// Copyright (C) 2017-2021 Laboratoire de Recherche et Développement
// de l'Epita (LRDE).
//
// This file is part of Spot, a model checking library.
//
// Spot is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// Spot is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
// or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
// License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <config.h>

#include "argmatch.h"

#include "common_aoutput.hh"
#include "common_finput.hh"
#include "common_setup.hh"
#include "common_sys.hh"

#include <spot/misc/bddlt.hh>
#include <spot/misc/escape.hh>
#include <spot/misc/timer.hh>
#include <spot/tl/formula.hh>
#include <spot/tl/apcollect.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/aiger.hh>
#include <spot/twaalgos/game.hh>
#include <spot/twaalgos/hoa.hh>
#include <spot/twaalgos/minimize.hh>
#include <spot/twaalgos/mealy_machine.hh>
#include <spot/twaalgos/product.hh>
#include <spot/twaalgos/synthesis.hh>
#include <spot/twaalgos/translate.hh>

enum
{
  OPT_ALGO = 256,
  OPT_CSV,
  OPT_DECOMPOSE,
  OPT_INPUT,
  OPT_OUTPUT,
  OPT_PRINT,
  OPT_PRINT_AIGER,
  OPT_PRINT_HOA,
  OPT_REAL,
  OPT_SIMPLIFY,
  OPT_VERBOSE,
  OPT_VERIFY
};

static const argp_option options[] =
  {
    /**************************************************/
    { nullptr, 0, nullptr, 0, "Input options:", 1 },
    { "outs", OPT_OUTPUT, "PROPS", 0,
      "comma-separated list of controllable (a.k.a. output) atomic"
      " propositions", 0},
    { "ins", OPT_INPUT, "PROPS", 0,
      "comma-separated list of controllable (a.k.a. output) atomic"
      " propositions", 0},
    /**************************************************/
    { nullptr, 0, nullptr, 0, "Fine tuning:", 10 },
    { "algo", OPT_ALGO, "sd|ds|ps|lar|lar.old", 0,
      "choose the algorithm for synthesis:"
      " \"sd\": translate to tgba, split, then determinize;"
      " \"ds\": translate to tgba, determinize, then split;"
      " \"ps\": translate to dpa, then split;"
      " \"lar\": translate to a deterministic automaton with arbitrary"
      " acceptance condition, then use LAR to turn to parity,"
      " then split (default);"
      " \"lar.old\": old version of LAR, for benchmarking.\n", 0 },
    { "decompose", OPT_DECOMPOSE, "yes|no", 0,
      "whether to decompose the specification as multiple output-disjoint "
      "problems to solve independently (enabled by default)", 0 },
    { "simplify", OPT_SIMPLIFY, "no|bisim|bwoa|sat|bisim-sat|bwoa-sat", 0,
      "simplification to apply to the controler (no) nothing, "
      "(bisim) bisimulation-based reduction, (bwoa) bissimulation-based "
      "reduction with output assignment, (sat) SAT-based minimization, "
      "(bisim-sat) SAT after bisim, (bwoa-sat) SAT after bwoa.  Defaults "
      "to 'bwoa'.", 0 },
    /**************************************************/
    { nullptr, 0, nullptr, 0, "Output options:", 20 },
    { "print-pg", OPT_PRINT, nullptr, 0,
      "print the parity game in the pgsolver format, do not solve it", 0},
    { "print-game-hoa", OPT_PRINT_HOA, "options", OPTION_ARG_OPTIONAL,
      "print the parity game in the HOA format, do not solve it", 0},
    { "realizability", OPT_REAL, nullptr, 0,
      "realizability only, do not compute a winning strategy", 0},
    { "aiger", OPT_PRINT_AIGER, "ite|isop|both[+ud][+dc]"
                                 "[+sub0|sub1|sub2]", OPTION_ARG_OPTIONAL,
      "prints a winning strategy as an AIGER circuit. The first, and only "
      "mandatory option defines the method to be used. \"ite\" for "
      "If-then-else normal form; "
      "\"isop\" for irreducible sum of producs; "
      "\"both\" tries both encodings and keeps the smaller one. "
      "The other options further "
      "refine the encoding, see aiger::encode_bdd.", 0},
    { "verbose", OPT_VERBOSE, nullptr, 0,
      "verbose mode", -1 },
    { "verify", OPT_VERIFY, nullptr, 0,
       "verifies the strategy or (if demanded) aiger against the spec.", -1 },
    { "csv", OPT_CSV, "[>>]FILENAME", OPTION_ARG_OPTIONAL,
      "output statistics as CSV in FILENAME or on standard output "
      "(if '>>' is used to request append mode, the header line is "
      "not output)", 0 },
    /**************************************************/
    { nullptr, 0, nullptr, 0, "Miscellaneous options:", -1 },
    { "extra-options", 'x', "OPTS", 0,
        "fine-tuning options (see spot-x (7))", 0 },
    { nullptr, 0, nullptr, 0, nullptr, 0 },
  };

static const struct argp_child children[] =
  {
    { &finput_argp_headless, 0, nullptr, 0 },
    { &aoutput_argp, 0, nullptr, 0 },
    //{ &aoutput_o_format_argp, 0, nullptr, 0 },
    { &misc_argp, 0, nullptr, 0 },
    { nullptr, 0, nullptr, 0 }
  };

const char argp_program_doc[] = "\
Synthesize a controller from its LTL specification.\v\
Exit status:\n\
  0   if the input problem is realizable\n\
  1   if the input problem is not realizable\n\
  2   if any error has been reported";

static std::vector<std::string> all_output_aps;
static std::vector<std::string> all_input_aps;

static const char* opt_csv = nullptr;
static bool opt_print_pg = false;
static bool opt_print_hoa = false;
static const char* opt_print_hoa_args = nullptr;
static bool opt_real = false;
static bool opt_do_verify = false;
static const char* opt_print_aiger = nullptr;

static spot::synthesis_info* gi;

static char const *const algo_names[] =
  {
   "ds",
   "sd",
   "ps",
   "lar",
   "lar.old"
  };

static char const *const algo_args[] =
{
  "detsplit", "ds",
  "splitdet", "sd",
  "dpasplit", "ps",
  "lar",
  "lar.old",
  nullptr
};
static spot::synthesis_info::algo const algo_types[] =
{
  spot::synthesis_info::algo::DET_SPLIT, spot::synthesis_info::algo::DET_SPLIT,
  spot::synthesis_info::algo::SPLIT_DET, spot::synthesis_info::algo::SPLIT_DET,
  spot::synthesis_info::algo::DPA_SPLIT, spot::synthesis_info::algo::DPA_SPLIT,
  spot::synthesis_info::algo::LAR,
  spot::synthesis_info::algo::LAR_OLD,
};
ARGMATCH_VERIFY(algo_args, algo_types);

static const char* const decompose_args[] =
  {
    "yes", "true", "enabled", "1",
    "no", "false", "disabled", "0",
    nullptr
  };
static bool decompose_values[] =
  {
    true, true, true, true,
    false, false, false, false,
  };
ARGMATCH_VERIFY(decompose_args, decompose_values);
bool opt_decompose_ltl = true;

static const char* const simplify_args[] =
  {
    "no", "false", "disabled", "0",
    "bisim", "1",
    "bwoa", "bisim-with-output-assignment", "2",
    "sat", "3",
    "bisim-sat", "4",
    "bwoa-sat", "5",
    nullptr
  };
static unsigned simplify_values[] =
  {
    0, 0, 0, 0,
    1, 1,
    2, 2, 2,
    3, 3,
    4, 4,
    5, 5,
  };
ARGMATCH_VERIFY(simplify_args, simplify_values);

namespace
{
  auto str_tolower = [] (std::string s)
    {
      std::transform(s.begin(), s.end(), s.begin(),
                     [](unsigned char c){ return std::tolower(c); });
      return s;
    };

  static void
  print_csv(const spot::formula& f)
  {
    auto& vs = gi->verbose_stream;
    auto& bv = gi->bv;
    if (not bv)
      error(2, 0, "no information available for csv (please send bug report)");
    if (vs)
      *vs << "writing CSV to " << opt_csv << '\n';

    output_file outf(opt_csv);
    std::ostream& out = outf.ostream();

    // Do not output the header line if we append to a file.
    // (Even if that file was empty initially.)
    if (!outf.append())
      {
        out << ("\"formula\",\"algo\",\"tot_time\",\"trans_time\","
                "\"split_time\",\"todpa_time\"");
        if (!opt_print_pg && !opt_print_hoa)
          {
            out << ",\"solve_time\"";
            if (!opt_real)
              out << ",\"strat2aut_time\"";
            if (opt_print_aiger)
              out << ",\"aig_time\"";
            out << ",\"realizable\""; //-1: Unknown, 0: Unreal, 1: Real
          }
        out << ",\"dpa_num_states\",\"dpa_num_states_env\""
            << ",\"strat_num_states\",\"strat_num_edges\"";
        if (opt_print_aiger)
            out << ",\"nb latches\",\"nb gates\"";
        out << '\n';
      }
    std::ostringstream os;
    os << f;
    spot::escape_rfc4180(out << '"', os.str());
    out << "\",\"" << algo_names[(int) gi->s]
        << "\"," << bv->total_time
        << ',' << bv->trans_time
        << ',' << bv->split_time
        << ',' << bv->paritize_time;
    if (!opt_print_pg && !opt_print_hoa)
      {
        out << ',' << bv->solve_time;
        if (!opt_real)
          out << ',' << bv->strat2aut_time;
        if (opt_print_aiger)
          out << ',' << bv->aig_time;
        out << ',' << bv->realizable;
      }
    out << ',' << bv->nb_states_arena
        << ',' << bv->nb_states_arena_env
        << ',' << bv->nb_strat_states
        << ',' << bv->nb_strat_edges;

    if (opt_print_aiger)
    {
      out << ',' << bv->nb_latches
          << ',' << bv->nb_gates;
    }
    out << '\n';
    outf.close(opt_csv);
  }

  int
  solve_formula(const spot::formula& f,
                const std::vector<std::string>& input_aps,
                const std::vector<std::string>& output_aps)
  {
    spot::stopwatch sw;
    if (gi->bv)
      sw.start();

    auto safe_tot_time = [&]()
    {
      if (gi->bv)
        gi->bv->total_time = sw.stop();
    };

    std::vector<spot::formula> sub_form;
    std::vector<std::set<spot::formula>> sub_outs;
    if (opt_decompose_ltl)
    {
      auto subs = split_independant_formulas(f, output_aps);
      if (subs.first.size() > 1)
      {
        sub_form = subs.first;
        sub_outs = subs.second;
      }
    }
    // When trying to split the formula, we can apply transformations that
    // increase its size. This is why we will use the original formula if it
    // has not been cut.
    if (sub_form.empty())
    {
      sub_form = { f };
      sub_outs.resize(1);
      std::transform(output_aps.begin(), output_aps.end(),
            std::inserter(sub_outs[0], sub_outs[0].begin()),
            [](const std::string& name) {
              return spot::formula::ap(name);
            });
    }
    std::vector<std::vector<std::string>> sub_outs_str;
    std::transform(sub_outs.begin(), sub_outs.end(),
                   std::back_inserter(sub_outs_str),
                   [](const auto& forms)
                    {
                      std::vector<std::string> r;
                      r.reserve(forms.size());
                      for (auto f : forms)
                        r.push_back(f.ap_name());
                      return r;
                    });

    assert((sub_form.size() == sub_outs.size())
           && (sub_form.size() == sub_outs_str.size()));

    const bool want_game = opt_print_pg || opt_print_hoa;

    std::vector<spot::twa_graph_ptr> arenas;

    auto sub_f = sub_form.begin();
    auto sub_o = sub_outs_str.begin();
    std::vector<spot::mealy_like> mealy_machines;

    auto print_game = want_game ?
      [](const spot::twa_graph_ptr& game)->void
        {
          if (opt_print_pg)
            pg_print(std::cout, game);
          else
            spot::print_hoa(std::cout, game, opt_print_hoa_args) << '\n';
        }
      :
      [](const spot::twa_graph_ptr&)->void{};

    for (; sub_f != sub_form.end(); ++sub_f, ++sub_o)
    {
      spot::mealy_like m_like
              {
                spot::mealy_like::realizability_code::UNKNOWN,
                nullptr,
                bddfalse
              };
      // If we want to print a game,
      // we never use the direct approach
      if (!want_game)
        m_like =
            spot::try_create_direct_strategy(*sub_f, *sub_o, *gi);

      switch (m_like.success)
      {
      case spot::mealy_like::realizability_code::UNREALIZABLE:
        {
          std::cout << "UNREALIZABLE" << std::endl;
          safe_tot_time();
          return 1;
        }
      case spot::mealy_like::realizability_code::UNKNOWN:
        {
          auto arena = spot::ltl_to_game(*sub_f, *sub_o, *gi);
          if (gi->bv)
            {
              gi->bv->nb_states_arena += arena->num_states();
              auto spptr =
                  arena->get_named_prop<std::vector<bool>>("state-player");
              assert(spptr);
              gi->bv->nb_states_arena_env +=
                  std::count(spptr->cbegin(), spptr->cend(), false);
              assert((spptr->at(arena->get_init_state_number()) == false)
                     && "Env needs first turn");
            }
          print_game(arena);
          if (!spot::solve_game(arena, *gi))
            {
              std::cout << "UNREALIZABLE" << std::endl;
              safe_tot_time();
              return 1;
            }
          // Create the (partial) strategy
          // only if we need it
          if (!opt_real)
            {
              spot::mealy_like ml;
              ml.success =
                spot::mealy_like::realizability_code::REALIZABLE_REGULAR;
              ml.mealy_like =
                  spot::solved_game_to_separated_mealy(arena, *gi);
              ml.glob_cond = bddfalse;
              mealy_machines.push_back(ml);
            }
          break;
        }
      case spot::mealy_like::realizability_code::REALIZABLE_REGULAR:
        {
          // the direct approach yielded a strategy
          // which can now be minimized
          // We minimize only if we need it
          assert(m_like.mealy_like && "Expected success but found no mealy!");
          if (!opt_real)
            {
              spot::stopwatch sw_direct;
              sw_direct.start();

              if ((0 < gi->minimize_lvl) && (gi->minimize_lvl < 3))
                // Uses reduction or not,
                // both work with mealy machines (non-separated)
                reduce_mealy_here(m_like.mealy_like, gi->minimize_lvl == 2);

              auto delta = sw_direct.stop();

              sw_direct.start();
              // todo better algo here?
              m_like.mealy_like =
                  split_2step(m_like.mealy_like,
                              spot::get_synthesis_outputs(m_like.mealy_like),
                              false);
              if (gi->bv)
                gi->bv->split_time += sw_direct.stop();

              sw_direct.start();
              if (gi->minimize_lvl >= 3)
                {
                  sw_direct.start();
                  // actual minimization, works on split mealy
                  m_like.mealy_like = minimize_mealy(m_like.mealy_like,
                                                     gi->minimize_lvl - 4);
                  delta = sw_direct.stop();
                }

              // Unsplit to have separated mealy
              m_like.mealy_like = unsplit_mealy(m_like.mealy_like);

              if (gi->bv)
                gi->bv->strat2aut_time += delta;
              if (gi->verbose_stream)
                *gi->verbose_stream << "final strategy has "
                                    << m_like.mealy_like->num_states()
                                    << " states and "
                                    << m_like.mealy_like->num_edges()
                                    << " edges\n"
                                    << "minimization took " << delta
                                    << " seconds\n";
            }
          SPOT_FALLTHROUGH;
        }
      case spot::mealy_like::realizability_code::REALIZABLE_DTGBA:
        if (!opt_real)
          mealy_machines.push_back(m_like);
        break;
      default:
        error(2, 0, "unexpected success code during mealy machine generation "
              "(please send bug report)");
      }
    }

    // If we only wanted to print the game we are done
    if (want_game)
      {
        safe_tot_time();
        return 0;
      }

    std::cout << "REALIZABLE" << std::endl;
    if (opt_real)
      {
        safe_tot_time();
        return 0;
      }
    // If we reach this line
    // a strategy was found for each subformula
    assert(mealy_machines.size() == sub_form.size()
           && "There are subformula for which no mealy like object"
                "has been created.");

    spot::aig_ptr saig = nullptr;
    spot::twa_graph_ptr tot_strat = nullptr;
    automaton_printer printer;
    spot::process_timer timer_printer_dummy;

    if (opt_print_aiger)
      {
        spot::stopwatch sw2;
        if (gi->bv)
          sw2.start();
        saig = spot::mealy_machines_to_aig(mealy_machines, opt_print_aiger,
                                           input_aps,
                                           sub_outs_str);
        if (gi->bv)
          {
            gi->bv->aig_time = sw2.stop();
            gi->bv->nb_latches = saig->num_latches();
            gi->bv->nb_gates = saig->num_gates();
          }
        if (gi->verbose_stream)
          {
            *gi->verbose_stream << "AIG circuit was created in "
                               << gi->bv->aig_time
                               << " seconds and has " << saig->num_latches()
                               << " latches and "
                               << saig->num_gates() << " gates\n";
          }
        spot::print_aiger(std::cout, saig) << '\n';
      }
    else
      {
        assert(std::all_of(
           mealy_machines.begin(), mealy_machines.end(),
           [](const auto& ml)
           {return ml.success ==
               spot::mealy_like::realizability_code::REALIZABLE_REGULAR; })
               && "ltlsynt: Cannot handle TGBA as strategy.");
        tot_strat = mealy_machines.front().mealy_like;
        for (size_t i = 1; i < mealy_machines.size(); ++i)
          tot_strat = spot::product(tot_strat, mealy_machines[i].mealy_like);
        printer.print(tot_strat, timer_printer_dummy);
      }

    // Final step: Do verification if demanded
    if (!opt_do_verify)
      {
        safe_tot_time();
        return 0;
      }


    // TODO: different options to speed up verification?!
    spot::translator trans(gi->dict, &gi->opt);
    auto neg_spec = trans.run(spot::formula::Not(f));
    if (saig)
      {
        // Test the aiger
        auto saigaut = saig->as_automaton(false);
        if (neg_spec->intersects(saigaut))
          error(2, 0, "Aiger and negated specification do intersect: "
                "circuit is not OK.");
        std::cout << "c\nCircuit was verified\n";
      }
    else if  (tot_strat)
      {
        // Test the strategy
        if (neg_spec->intersects(tot_strat))
          error(2, 0, "Strategy and negated specification do intersect: "
                "strategy is not OK.");
        std::cout << "/*Strategy was verified*/\n";
      }
    // Done

    safe_tot_time();
    return 0;
  }

  class ltl_processor final : public job_processor
  {
  private:
    std::vector<std::string> input_aps_;
    std::vector<std::string> output_aps_;

  public:
    ltl_processor(std::vector<std::string> input_aps_,
                  std::vector<std::string> output_aps_)
        : input_aps_(std::move(input_aps_)),
          output_aps_(std::move(output_aps_))
    {
    }

    int process_formula(spot::formula f,
                        const char* filename, int linenum) override
    {
      auto unknown_aps = [](spot::formula f,
                            const std::vector<std::string>& known,
                            const std::vector<std::string>* known2 = nullptr)
      {
        std::vector<std::string> unknown;
        std::set<spot::formula> seen;
        f.traverse([&](const spot::formula& s)
        {
          if (s.is(spot::op::ap))
            {
              if (!seen.insert(s).second)
                return false;
              const std::string& a = s.ap_name();
              if (std::find(known.begin(), known.end(), a) == known.end()
                  && (!known2
                      || std::find(known2->begin(),
                                   known2->end(), a) == known2->end()))
                unknown.push_back(a);
            }
          return false;
        });
        return unknown;
      };

      // Decide which atomic propositions are input or output.
      int res;
      if (input_aps_.empty() && !output_aps_.empty())
        {
          res = solve_formula(f, unknown_aps(f, output_aps_), output_aps_);
        }
      else if (output_aps_.empty() && !input_aps_.empty())
        {
          res = solve_formula(f, input_aps_, unknown_aps(f, input_aps_));
        }
      else if (output_aps_.empty() && input_aps_.empty())
        {
          for (const std::string& ap: unknown_aps(f, input_aps_, &output_aps_))
            error_at_line(2, 0, filename, linenum,
                          "one of --ins or --outs should list '%s'",
                          ap.c_str());
          res = solve_formula(f, input_aps_, output_aps_);
        }
      else
        {
          for (const std::string& ap: unknown_aps(f, input_aps_, &output_aps_))
            error_at_line(2, 0, filename, linenum,
                          "both --ins and --outs are specified, "
                          "but '%s' is unlisted",
                          ap.c_str());
          res = solve_formula(f, input_aps_, output_aps_);
        }

      if (opt_csv)
        print_csv(f);
      return res;
    }
  };
}

static int
parse_opt(int key, char *arg, struct argp_state *)
{
  // Called from C code, so should not raise any exception.
  BEGIN_EXCEPTION_PROTECT;
  switch (key)
    {
    case OPT_ALGO:
      gi->s = XARGMATCH("--algo", arg, algo_args, algo_types);
      break;
    case OPT_CSV:
      opt_csv = arg ? arg : "-";
      if (not gi->bv)
        gi->bv = spot::synthesis_info::bench_var();
      break;
    case OPT_DECOMPOSE:
      opt_decompose_ltl = XARGMATCH("--decompose", arg,
                                    decompose_args, decompose_values);
      break;
    case OPT_INPUT:
      {
        std::istringstream aps(arg);
        std::string ap;
        while (std::getline(aps, ap, ','))
          {
            ap.erase(remove_if(ap.begin(), ap.end(), isspace), ap.end());
            all_input_aps.push_back(str_tolower(ap));
          }
        break;
      }
    case OPT_OUTPUT:
      {
        std::istringstream aps(arg);
        std::string ap;
        while (std::getline(aps, ap, ','))
          {
            ap.erase(remove_if(ap.begin(), ap.end(), isspace), ap.end());
            all_output_aps.push_back(str_tolower(ap));
          }
        break;
      }
    case OPT_PRINT:
      opt_print_pg = true;
      gi->force_sbacc = true;
      break;
    case OPT_PRINT_HOA:
      opt_print_hoa = true;
      opt_print_hoa_args = arg;
      break;
    case OPT_PRINT_AIGER:
      opt_print_aiger = arg ? arg : "ite";
      break;
    case OPT_REAL:
      opt_real = true;
      break;
    case OPT_SIMPLIFY:
      gi->minimize_lvl = XARGMATCH("--simplify", arg,
                                   simplify_args, simplify_values);
      break;
    case OPT_VERBOSE:
      gi->verbose_stream = &std::cerr;
      if (not gi->bv)
        gi->bv = spot::synthesis_info::bench_var();
      break;
    case OPT_VERIFY:
      opt_do_verify = true;
      break;
    case 'x':
      {
        const char* opt = gi->opt.parse_options(arg);
        if (opt)
          error(2, 0, "failed to parse --options near '%s'", opt);
      }
      break;
    }
  END_EXCEPTION_PROTECT;
  return 0;
}

int
main(int argc, char **argv)
{
  return protected_main(argv, [&] {
    // Create gi_ as a local variable, so make sure it is destroyed
    // before all global variables.
    spot::synthesis_info gi_;
    gi = &gi_;
    //gi_.opt.set("simul", 0);     // no simulation, except...
    //gi_.opt.set("dpa-simul", 1); // ... after determinization
    gi_.opt.set("tls-impl", 1);  // no automata-based implication check
    gi_.opt.set("wdba-minimize", 2); // minimize only syntactic oblig
    const argp ap = { options, parse_opt, nullptr,
                      argp_program_doc, children, nullptr, nullptr };
    if (int err = argp_parse(&ap, argc, argv, ARGP_NO_HELP, nullptr, nullptr))
      exit(err);
    check_no_formula();

    // Check if inputs and outputs are distinct
    for (const std::string& ai : all_input_aps)
      if (std::find(all_output_aps.begin(), all_output_aps.end(), ai)
          != all_output_aps.end())
        error(2, 0, "'%s' appears both in --ins and --outs", ai.c_str());

    ltl_processor processor(all_input_aps, all_output_aps);
    if (int res = processor.run(); res == 0 || res == 1)
      {
        // Diagnose unused -x options
        gi_.opt.report_unused_options();
        return res;
      }
    return 2;
  });
}
