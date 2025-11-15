#pragma once

#include <boost/program_options.hpp>
// #include <error.h>
#include "error_msg.hh"
#include <configuration.hh>

// NOTE: the do...while is a macro trick to prevent this from being a compound statement.
#define debug_(A...) do { std::cout << A << std::endl; } while (0)

namespace po = boost::program_options;

/**
 * Struct that will hold the parsed argument values.
 */
struct ArgParseResult {
  std::string formula;
  bool is_file;
  bool lbt_input;
  bool lenient;


  bool moore_mode = false;
  std::vector<int> init_state = {};
  std::vector<std::string> inputs = {};
  std::vector<std::string> outputs = {};
  std::string synth_fname = "";
  unsigned int opt_Kstart = DEFAULT_KMIN;
  unsigned int opt_Kmax = DEFAULT_K;
  unsigned int opt_Kinc = DEFAULT_KINC;
  unsigned int verbose_level = 0;
  std::string extra_opts = "";
};

inline void process_arg_init_(const std::string& arg, ArgParseResult& result);
inline void process_arg_input_(const std::string& arg, ArgParseResult& result);
inline void process_arg_output_(const std::string& arg, ArgParseResult& result);
inline void process_arg_synth_(const std::string& arg, ArgParseResult& result);

/**
 * Function that parses the arguments into an ArgParseResult object.
 *
 * @param argc The value of 'argc', as passed to main().
 * @param argv The value of 'argv', as passed to main().
 *
 * @return ArgParseResult object with the argument values.
 */
inline ArgParseResult arg_parser(int argc, char **argv) {
  debug_("[DEBUG] Parsing arguments.");
  try {
    po::options_description desc("Allowed options");
    desc.add_options()
    ("help", "print this help")
    ("version", "print program version")

    ("formula,f", po::value<std::string>()->value_name("STRING"),
      "process the formula STRING")
    ("file,F", po::value<std::string>()->value_name("FILENAME[/COL]"),
      "process each line of FILENAME as a formula; if COL "
                                                "is a positive integer, assume a CSV file and read "
                                                "column COL; use a negative COL to drop the first "
                                                "line of the CSV file")
    ("lbt-input", po::bool_switch()->default_value(false),
      "read all formulas using LBT's prefix syntax")
    ("lenient", po::bool_switch()->default_value(false),
      "parenthesized blocks that cannot be parsed as "
                             "subformulas are considered as atomic properties")

    // input options
    ("init,0", po::value<std::string>()->value_name("STATE"),
      "comma-separated state vector to use as initial state")

    ("inputs,i", po::value<std::string>()->required()->value_name("PROPS"),
      "comma-separated list of uncontrollable (a.k.a. input) atomic propositions")
    // ("workers,j", po::value<std::string>()->value_name("VAL"),
    //   "Number of parallel workers for composition")
    ("outputs,o", po::value<std::string>()->required()->value_name("PROPS"),
      "comma-separated list of controllable (a.k.a. output) atomic propositions")

    ("synth,S", po::value<std::string>()->value_name("FILENAME"),
      "enable synthesis, pass .aag filename, or - to print gates")
    // ("winreg,W", po::value<std::string>()->value_name("FILENAME"),
    //   "output winning region, pass .aag filename, or - to print gates")

    // parameters of the synth algo
    ("Kinc,I", po::value<unsigned int>()->value_name("VAL"),
      "increment value for K, used when Kmin < Kmax")
    ("Kmax,K", po::value<unsigned int>()->value_name("VAL"),
      "final value of K, or unique value if Kmin is not specified")
    ("Kstart,M", po::value<unsigned int>()->value_name("VAL"),
      "starting value of K; Kinc MUST be set when using "
                             "this option")

    // ("unreal-x,u", po::value<std::string>()->value_name("[formula|automaton]"),
    //   "for unrealizability, either add X's to outputs in "
    //                          "the input formula, or push outputs one transition "
    //                          "forward in the automaton.")

    ("moore", po::bool_switch()->default_value(false),
      "synthesise a Moore machine, if not specified a mealy machine will be symthesised.")

    // output options
    // ("check,c", po::value<std::string>()->value_name("[real|unreal|both]"),
    //   "either check for real, unreal, or both")
    // NOTE: this weird construction such that "verbose" can be specified multiple times
    // ("verbose,v", po::value<std::vector<bool>>()
    //   ->default_value(std::vector<bool>(), "false")
    //   ->implicit_value(std::vector<bool>(1), "true")
    //   ->zero_tokens(),
    ("verbose,v", po::value<int>()->default_value(0)->implicit_value(1),
      "verbose mode, can be repeated for more verbosity")
    ("extra_opts,x", po::value<std::string>()->value_name("EXTRAS"),
      "Extra options.")
      ;

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);

    if (vm.contains ("help")) {
      std::cout << "Verify realizability for LTL specifications." << std::endl;
      std::cout << desc << std::endl;
      std::cout << "Exit status:\n"
                    "\t0   if the input problem is realizable\n"
                    "\t1   if the input problem is not realizable\n"
                    "\t2   if this could not be decided\n"
                    "\t3   if any error has been reported" << std::endl;
      exit(0);
    }

    // handle any missing args
    po::notify(vm);

    ArgParseResult retval;

    // handle the formula
    if (vm.contains("formula")) {
      retval.formula = vm["formula"].as<std::string>();
      retval.is_file = false;
    }

    if (vm.contains("file")) {
      retval.formula = vm["file"].as<std::string>();
      retval.is_file = true;
    }

    if (vm.contains("file") and vm.contains("formula")) {
      error(3,0,"Error: cannot pass both a file and a formula.");
    }

    retval.lenient = vm["lenient"].as<bool>();
    retval.lbt_input = vm["lbt-input"].as<bool>();

    // manually handle the default arguments, since that is a bit clearer...
    if (vm.contains("init")) {
      process_arg_init_(vm["init"].as<std::string>(), retval);
    }

    process_arg_input_(vm["inputs"].as<std::string>(), retval);
    process_arg_output_(vm["outputs"].as<std::string>(), retval);

    if (vm.contains("synth")) {
      process_arg_synth_(vm["synth"].as<std::string>(), retval);
    }

    if (vm.contains("Kmax")) {
      retval.opt_Kmax = vm["Kmax"].as<unsigned int>();
    }

    if (vm.contains("Kstart")) {
      if (vm.count("Kinc") < 0) {
        error(3, 0, "Error: if 'Kmin' is specified, then 'Kinc' also must be provided.");
      }

      retval.opt_Kinc = vm["Kinc"].as<unsigned int>();
      retval.opt_Kstart = vm["Kstart"].as<unsigned int>();
    }

    if (vm.contains("moore")) {
      retval.moore_mode = true;
    }

    retval.verbose_level += vm["verbose"].as<int>();

    if (vm.count("extra_opts")) {
      retval.extra_opts = vm["extra_opts"].as<std::string>();
    }

    // TODO: handle removed arguments: workers, opt_unreal, check, winreg

    debug_("[DEBUG] Finished parsing arguments.");

    return retval;

  } catch (const std::exception& ex) {
    std::cout << "An error occurred while parsing arguments:" << std::endl;
    std::cout << ex.what() << std::endl;
    exit(3);
  }
}

inline void process_arg_init_(const std::string& arg, ArgParseResult& result) {
  // initial state vector

  std::istringstream state (arg);
  std::string val;

  while (std::getline (state, val, ',')) {
    try {
      result.init_state.push_back (std::stoi (val));
    } catch (std::invalid_argument const& ex) {
      std::cout << "ERROR while parsing initial state: "
                << ex.what() << std::endl;
      abort ();
    } catch (std::out_of_range const& ex) {
      std:: cout << "ERROR while parsing initial state: "
                 << ex.what () << std::endl;
      abort ();
    }
  }
}

inline void process_arg_input_(const std::string& arg, ArgParseResult& result) {
    // uncontrollable input prop
  std::istringstream props (arg);
  std::string prop;
  while (std::getline (props, prop, ',')) {
    prop.erase (std::remove_if (prop.begin (), prop.end (), isspace), prop.end ());
    result.inputs.push_back (prop);
  }
}

inline void process_arg_output_(const std::string& arg, ArgParseResult& result) {
  std::istringstream props (arg);
  std::string prop;
  while (std::getline (props, prop, ',')) {
    prop.erase (std::remove_if (prop.begin (), prop.end (), isspace), prop.end ());
    result.outputs.push_back (prop);
  }
}

inline void process_arg_synth_(const std::string& arg, ArgParseResult& result) {
    result.synth_fname = arg;
}
