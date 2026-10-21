// Frozen pre-conversion frontend, for differential testing only.
#include "tlsf_frontend.hh"
#include <unordered_set>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <sys/resource.h>
#include <tlsf/decompose.hpp>

namespace legacy {
  using acacia::tlsf_frontend::specification;
  struct timing {
      double wall = 0;
      double cpu = 0;
  };
  timing passes[2];
  unsigned pass = 0;
  namespace {
    tlsf::Result timed_decompose (const std::string& text, const tlsf::Options& opts) {
      const auto begin = std::chrono::steady_clock::now ();
      const auto cpu = std::clock ();
      auto result = tlsf::decompose (text, opts);
      passes[pass++] = {
          std::chrono::duration<double> (std::chrono::steady_clock::now () - begin).count (),
          double (std::clock () - cpu) / CLOCKS_PER_SEC};
      return result;
    }
    bool finite_semantics (const std::string& semantics) {
      return semantics.find ("Finite") != std::string::npos;
    }

    tlsf::Options options () {
      tlsf::Options value;
      value.precheck = false;  // Conversion consumes formulas, not precheck verdicts.
      value.format = tlsf::Format::Ltlxba;
      // Match the checked-in ltlxba/.part identifier convention.  TLSF
      // semantics remain specification-defined.
      value.lowercase = true;
      return value;
    }

    bool has_indexed_conjunction (std::string_view text) {
      size_t at = 0;
      while ((at = text.find ("&&", at)) != std::string_view::npos) {
        at += 2;
        while (at < text.size () and std::isspace (static_cast<unsigned char> (text[at])))
          ++at;
        if (at < text.size () and text[at] == '[')
          return true;
      }
      return false;
    }

    void stable_signal_order (std::vector<std::string>& signals,
                              const std::vector<tlsf::IndexedFamily>& families, bool outputs) {
      std::unordered_set<std::string_view> bus_members;
      for (const auto& family : families)
        if (family.is_output == outputs)
          bus_members.insert (family.members.begin (), family.members.end ());

      std::vector<std::string> ordered;
      ordered.reserve (signals.size ());
      for (auto it = signals.rbegin (); it != signals.rend (); ++it)
        if (not bus_members.contains (*it))
          ordered.push_back (std::move (*it));
      for (auto family = families.rbegin (); family != families.rend (); ++family)
        if (family->is_output == outputs and not family->is_enum)
          ordered.insert (ordered.end (), family->members.begin (), family->members.end ());
      for (auto family = families.rbegin (); family != families.rend (); ++family)
        if (family->is_output == outputs and family->is_enum)
          ordered.insert (ordered.end (), family->members.begin (), family->members.end ());
      signals = std::move (ordered);
    }
  }  // namespace

  specification parse (std::string_view text) {
    const std::string owned {text};
    tlsf::Options conversion_options = options ();
    tlsf::Result result = timed_decompose (owned, conversion_options);
    if (finite_semantics (result.semantics))
      throw std::runtime_error ("finite TLSF semantics are not supported");

    // Acacia solves in the Mealy frame.  Preserve the requested target for
    // synthesis metadata, but ask the linked frontend for an effective Mealy
    // formula so solving and controller conversion share one internal path.
    const std::string source_target = result.target;
    if (source_target != "Mealy") {
      conversion_options.overwrite_target = "Mealy";
      result = timed_decompose (owned, conversion_options);
    }

    stable_signal_order (result.inputs, result.indexed_families, false);
    stable_signal_order (result.outputs, result.indexed_families, true);

    specification value;
    value.metadata.source_format = "tlsf";
    value.metadata.tlsf_semantics = result.semantics;
    value.metadata.tlsf_target = source_target;
    value.metadata.tlsf_effective_target = result.target;
    value.metadata.tlsf_gr_level = result.gr_level;
    if (has_indexed_conjunction (text)) {
      value.metadata.tlsf_indexed_families.reserve (result.indexed_families.size ());
      for (auto& family : result.indexed_families) {
        symmetry::indexed_family_hint hint;
        hint.is_input = not family.is_output;
        hint.lo = family.lo;
        hint.hi = family.hi;
        hint.members = std::move (family.members);
        value.metadata.tlsf_indexed_families.push_back (std::move (hint));
      }
    }

    value.formula = std::move (result.preprocessed_ltl);
    value.metadata.tlsf_normalized_objective = value.formula;
    value.inputs = std::move (result.inputs);
    value.outputs = std::move (result.outputs);
    // Stable serialization convention only; TLSF does not define a total AP
    // order.  Scalars precede ordinary and enum buses, source declarations are
    // reversed within each group, and bus members retain ascending index order.
    // Indexed-family provenance uses the same member order.
    if (value.formula.empty ())
      throw std::runtime_error ("TLSF conversion produced an empty formula");
    return value;
  }

}  // namespace legacy

namespace {
  using acacia::tlsf_frontend::specification;
  std::string read (const std::filesystem::path& path) {
    std::ifstream in (path);
    if (!in)
      throw std::runtime_error ("cannot read " + path.string ());
    return {std::istreambuf_iterator<char> (in), {}};
  }
  bool equal (const specification& a, const specification& b) {
    const auto& x = a.metadata;
    const auto& y = b.metadata;
    if (a.formula != b.formula || a.inputs != b.inputs || a.outputs != b.outputs ||
        x.source_format != y.source_format || x.tlsf_semantics != y.tlsf_semantics ||
        x.tlsf_target != y.tlsf_target || x.tlsf_effective_target != y.tlsf_effective_target ||
        x.tlsf_gr_level != y.tlsf_gr_level ||
        x.tlsf_normalized_objective != y.tlsf_normalized_objective ||
        x.tlsf_indexed_families.size () != y.tlsf_indexed_families.size ())
      return false;
    for (size_t i = 0; i < x.tlsf_indexed_families.size (); ++i) {
      const auto& f = x.tlsf_indexed_families[i];
      const auto& g = y.tlsf_indexed_families[i];
      if (f.is_input != g.is_input || f.lo != g.lo || f.hi != g.hi || f.members != g.members)
        return false;
    }
    return true;
  }
  bool compare (const std::string& text, const std::string& name, int expected = -1) {
    std::optional<specification> old, current;
    legacy::pass = 0;
    try {
      old = legacy::parse (text);
    } catch (const std::exception&) {
    }
    try {
      current = acacia::tlsf_frontend::parse (text);
    } catch (const std::exception&) {
    }
    const bool ok = old.has_value () == current.has_value () && (!old || equal (*old, *current)) &&
                    (expected < 0 || bool (current) == bool (expected));
    if (!ok)
      std::cerr << "MISMATCH " << name << " old=" << bool (old) << " new=" << bool (current)
                << '\n';
    return ok;
  }
  unsigned focused () {
    unsigned failures = 0;
    // The legacy AP table also exposes undeclared occurrences. After
    // lowercasing, an occurrence can alias a bus member; ordering removes it.
    failures += !compare (R"(INFO { TITLE: "AP aliases" SEMANTICS: Mealy TARGET: Mealy }
MAIN { INPUTS { req[2]; } OUTPUTS { grant; } GUARANTEE { G (REQ_0 -> grant); } })",
                          "undeclared bus alias", 1);
    failures += !compare (R"(INFO { TITLE: "enums" SEMANTICS: Mealy TARGET: Moore }
GLOBAL { DEFINITIONS { enum mode = A: 01 B: 10 C: 11; } }
MAIN { INPUTS { go; unused_i; } OUTPUTS { mode st; unused_o; }
       GUARANTEE { G (go -> st == A); G (!(st == C)); } })",
                          "enums", 1);
    for (const auto* sem :
         {"Mealy", "Moore", "Mealy,Strict", "Moore,Strict", "Finite,Mealy", "Finite,Moore"})
      for (const auto* target : {"Mealy", "Moore"})
        for (const auto* body :
             {"INPUTS { Req; unused_i; } OUTPUTS { Grant; unused_o; } GUARANTEE { G (Req -> "
              "Grant); }",
              "INPUTS { Req[3]; unused_i; } OUTPUTS { Grant[3]; unused_o; } GUARANTEE { &&[0 <= i "
              "< 3] G (Req[i] -> Grant[i]); }",
              "INPUTS { Req[3]; } OUTPUTS { Grant[3]; } GUARANTEE { G (Req[0] -> Grant[2]); }",
              "INPUTS { a; b; c; } OUTPUTS { x; y; z; } INITIALLY { a; } PRESET { x; } REQUIRE { "
              "b; } ASSERT { y; } ASSUME { c; } GUARANTEE { z; }"}) {
          const std::string semantics (sem);
          failures +=
              !compare ("INFO { TITLE: \"focused\" SEMANTICS: " + semantics +
                            " TARGET: " + target + " } MAIN { " + body + " }",
                        semantics + "/" + target, semantics.find ("Finite") == std::string::npos);
        }
    failures += !compare (R"(INFO { TITLE: "parameterized" SEMANTICS: Moore,Strict TARGET: Moore }
GLOBAL { PARAMETERS { n = 3; } }
MAIN { INPUTS { a[n]; } OUTPUTS { b[n]; } GUARANTEE { &&[0 <= i < n] G(a[i] -> b[i]); } })",
                          "parameters", 1);
    for (const auto* decl : {"INPUTS { a'; } OUTPUTS { b; } GUARANTEE { G b; }",
                             "INPUTS { A; } OUTPUTS { a; } GUARANTEE { G a; }",
                             "INPUTS { a; } OUTPUTS { b; } GUARANTEE { X[!] b; }",
                             "INPUTS { a; } OUTPUTS { b@; } GUARANTEE { G b@; }"})
      failures += !compare (
          std::string ("INFO { TITLE: \"invalid\" SEMANTICS: Mealy TARGET: Mealy } MAIN { ") +
              decl + " }",
          "rejection", 0);
    return failures;
  }
}
int main (int argc, char** argv) {
  try {
    if (argc == 3 &&
        (std::string (argv[1]) == "--measure" || std::string (argv[1]) == "--measure-old")) {
      const auto source = read (argv[2]);
      const auto start = std::chrono::steady_clock::now ();
      const auto cpu = std::clock ();
      const auto result = std::string (argv[1]) == "--measure-old"
                              ? legacy::parse (source)
                              : acacia::tlsf_frontend::parse (source);
      const double wall =
          std::chrono::duration<double> (std::chrono::steady_clock::now () - start).count ();
      const double seconds = double (std::clock () - cpu) / CLOCKS_PER_SEC;
      rusage usage {};
      getrusage (RUSAGE_SELF, &usage);
      std::cout << wall << '\t' << seconds << '\t' << usage.ru_maxrss << '\t'
                << legacy::passes[0].wall << '\t' << legacy::passes[0].cpu << '\t'
                << legacy::passes[1].wall << '\t' << legacy::passes[1].cpu << '\n';
      return result.formula.empty ();
    }
    if (argc != 2)
      throw std::runtime_error (
          "usage: tlsf-conversion-compare DIRECTORY | --focused | --measure[-old] FILE");
    if (std::string (argv[1]) == "--focused")
      return focused () != 0;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator (argv[1]))
      if (entry.is_regular_file () && entry.path ().extension () == ".tlsf")
        files.push_back (entry.path ());
    std::sort (files.begin (), files.end ());
    if (files.empty ())
      throw std::runtime_error ("no .tlsf files");
    const auto start = std::chrono::steady_clock::now ();
    unsigned failures = 0;
    for (const auto& path : files)
      failures += !compare (read (path), path.string ());
    std::cout << "Compared: " << files.size () << " Mismatches: " << failures << " Wall: "
              << std::chrono::duration<double> (std::chrono::steady_clock::now () - start).count ()
              << "s\n";
    return failures != 0;
  } catch (const std::exception& e) {
    std::cerr << e.what () << '\n';
    return 2;
  }
}
