#include "arg_parser.hh"
#include "solver/parity/tlsf_dpa.hh"
#include "tlsf_frontend.hh"
#include "utils/verbose.hh"
extern "C" {
#include "../subprojects/tlsf-tools/test/fuzz/decode.h"
}

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <set>
#include <spot/tl/length.hh>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/contains.hh>
#include <spot/twaalgos/translate.hh>
#include <sstream>
#include <stdexcept>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

unsigned utils::verbose = 0;
utils::voutstream utils::vout;
namespace fs = std::filesystem;
namespace dpa = acacia::tlsf_dpa;

namespace {
  constexpr unsigned max_aps = 8, max_nodes = 64, max_states = 64;

  std::string read (const fs::path& path) {
    std::ifstream stream (path, std::ios::binary);
    if (!stream)
      throw std::runtime_error ("cannot read " + path.string ());
    return {std::istreambuf_iterator<char> (stream), {}};
  }

  std::string decode (const fs::path& path, bool dpa_input) {
    auto bytes = read (path);
    if (dpa_input && !bytes.empty ())
      bytes.erase (0, 1);  // fuzz-dpa reserves the first byte for construction options.
    char text[TLSF_FUZZ_TEXT_CAPACITY];
    const auto size =
        tlsf_fuzz_decode (reinterpret_cast<const uint8_t*> (bytes.data ()), bytes.size (), text);
    return {text, size};
  }

  std::vector<std::string> replay_texts (std::string text) {
    std::vector<std::string> result {text};
    const auto finite = text.find ("SEMANTICS: Finite,");
    if (finite != std::string::npos) {
      // Add a different, infinite-word problem; keep the original finite skip.
      // Text rewrites assume decode.c's output (fixed INFO strings, literal
      // unspaced bounds); on other input a bad companion fails to parse and
      // is skipped, never reported.
      text.erase (finite + std::string ("SEMANTICS: ").size (), 7);
      // Strong and weak next coincide on infinite words, so dropping strong
      // markers preserves the infinite-word meaning, including bounded forms.
      text = std::regex_replace (text, std::regex (R"(X\[!\])"), "X");
      text = std::regex_replace (text, std::regex (R"(\[!?([0-9]+(:[0-9]+)?)!?\])"), "[$1]");
      result.push_back (std::move (text));
    }
    return result;
  }

  // Syntax adapter for decode.c's bounded grammar, not a general TLSF printer.
  std::string syfco_syntax (const std::string& text) {
    const auto end = text.find ('}');
    auto info = text.substr (0, end);
    auto body = text.substr (end);
    // INFO semicolons are optional separators; generated strings contain none.
    std::replace (info.begin (), info.end (), ';', ' ');
    // Generated tags are identifier strings: unquoting keeps their names and
    // changes no game metadata (semantics/target) or formulas.
    const auto tags = info.find ("TAGS:");
    if (tags != std::string::npos)
      info.erase (std::remove (info.begin () + tags, info.end (), '"'), info.end ());
    // Both declarations enumerate precisely ob[0], ..., ob[n-1], in that order.
    body = std::regex_replace (body, std::regex (R"(ob\[0\.\.\(n - 1\)\])"), "ob[n]");
    // Space standalone parentheses so SyFCo recognizes adjacent word operators
    // (U/R/W/X/F/G), without regrouping. Keep compound set tokens (+)/(*)/(-).
    body = std::regex_replace (body, std::regex (R"(\([+*-]\)|[()])"), " $& ");
    return info + body;
  }

  struct scratch {
      fs::path path;
      scratch () {
        std::string pattern = (fs::current_path () / "fuzz-replay-XXXXXX").string ();
        if (!mkdtemp (pattern.data ()))
          throw std::runtime_error ("mkdtemp failed");
        path = pattern;
      }
      ~scratch () { fs::remove_all (path); }
  };

  // The worker's alarm also bounds SyFCo; its process group is reaped by the parent.
  bool syfco (const std::vector<std::string>& arguments, const fs::path& output) {
    const pid_t pid = fork ();
    if (pid < 0)
      throw std::runtime_error ("fork failed");
    if (!pid) {
      if (!freopen (output.c_str (), "w", stdout) || !freopen ("/dev/null", "w", stderr))
        _exit (1);
      std::vector<char*> argv {const_cast<char*> ("syfco")};
      for (const auto& argument : arguments)
        argv.push_back (const_cast<char*> (argument.c_str ()));
      argv.push_back (nullptr);
      execvp (argv[0], argv.data ());
      _exit (127);
    }
    int status;
    while (waitpid (pid, &status, 0) < 0)
      if (errno != EINTR)
        throw std::runtime_error ("waitpid failed");
    return WIFEXITED (status) && WEXITSTATUS (status) == 0;
  }

  bool have_syfco () {
    const char* path = std::getenv ("PATH");
    std::istringstream paths (path ? path : "");
    for (std::string directory; std::getline (paths, directory, ':');)
      if (!access ((fs::path (directory.empty () ? "." : directory) / "syfco").c_str (), X_OK))
        return true;
    return false;
  }

  std::pair<std::set<std::string>, std::set<std::string>> partition (const fs::path& file) {
    std::pair<std::set<std::string>, std::set<std::string>> names;
    auto* current = &names.first;
    std::istringstream stream (read (file));
    for (std::string word; stream >> word;)
      if (word == ".inputs")
        current = &names.first;
      else if (word == ".outputs")
        current = &names.second;
      else
        current->insert (word);
    return names;
  }

  // Export only the neutral formula DAG for the Büchi constructor. The existing
  // production importer below remains the sole DPA/Büchi -> Spot adapter.
  uint32_t dag (const TlsfAstNode* node, const std::vector<std::string>& names,
                std::vector<TlsfDpaFormulaNode>& nodes) {
    if (nodes.size () >= max_nodes)
      throw std::length_error ("formula budget");
    TlsfDpaFormulaNode record {tlsf_ast_node_kind (node), UINT32_MAX, UINT32_MAX, UINT32_MAX};
    const auto count = tlsf_ast_node_child_count (node);
    if (count)
      record.left = dag (tlsf_ast_node_child (node, 0), names, nodes);
    if (count == 2)
      record.right = dag (tlsf_ast_node_child (node, 1), names, nodes);
    if (record.kind == TLSF_AST_AP) {
      const auto found = std::ranges::find (names, tlsf_ast_node_ap_name (node));
      record.ap = found == names.end () ? UINT32_MAX : uint32_t (found - names.begin ());
    }
    if (nodes.size () >= max_nodes)
      throw std::length_error ("formula budget");
    nodes.push_back (record);
    return uint32_t (nodes.size () - 1);
  }

  int check (const std::string& text, const std::string& property, const fs::path& work) {
    acacia::tlsf_frontend::specification spec;
    try {
      spec = acacia::tlsf_frontend::parse (text);
    } catch (const std::runtime_error&) {
      return 77;  // Parse/conversion declines, including finite semantics.
    }
    auto names = spec.inputs;
    names.insert (names.end (), spec.outputs.begin (), spec.outputs.end ());
    if (names.size () > max_aps ||
        spec.metadata.tlsf_semantics.find ("Finite") != std::string::npos)
      return 77;
    std::vector<TlsfDpaFormulaNode> nodes;
    const auto root = dag (tlsf_ast_root (spec.ast.get ()), names, nodes);
    const auto parsed = spot::parse_infix_psl (spec.formula);
    if (!parsed.f || !parsed.errors.empty ())
      return 77;

    if (property == "2") {
      const auto input = work / "spec.tlsf", output = work / "syfco.ltl",
                 part = work / "spec.part";
      std::ofstream (input) << syfco_syntax (text);
      if (!syfco ({"-f", "ltl", "-m", "fully", "--part-file", part.string (), input.string ()},
                  output))
        return 77;
      const auto reference = spot::parse_infix_psl (read (output));
      if (!reference.f || !reference.errors.empty () ||
          spot::length (reference.f) > int (max_nodes))
        return 77;
      auto formula = reference.f;
      // G5: SyFCo prints in the source semantics frame. Native conversion
      // already targets Mealy; Moore -> Mealy delays inputs exactly once.
      if (spec.metadata.tlsf_semantics.find ("Moore") != std::string::npos) {
        const auto delay = [&] (auto&& self, spot::formula f) -> spot::formula {
          if (f.is (spot::op::ap))
            return std::ranges::find (spec.inputs, f.ap_name ()) != spec.inputs.end ()
                       ? spot::formula::X (f)
                       : f;
          return f.map ([&] (spot::formula child) { return self (self, child); });
        };
        formula = delay (delay, formula);
      }
      const auto [inputs, outputs] = partition (part);
      spot::translator translate;
      if (translate.run (parsed.f)->num_states () > max_states ||
          translate.run (formula)->num_states () > max_states)
        return 77;
      // P2: Both conversions must preserve the declared game alphabet and
      // denote the same infinite-word language in the effective Mealy frame.
      if (!(inputs == std::set<std::string> (spec.inputs.begin (), spec.inputs.end ()) &&
            outputs == std::set<std::string> (spec.outputs.begin (), spec.outputs.end ()) &&
            spot::are_equivalent (parsed.f, formula)))
        std::abort ();
      return 0;
    }

    dpa::limits limits;
    limits.construction = {max_states, 4u << 20, 4096, 1024};
    limits.construction.allow_unproved = true;  // Also cross-check the admitted general grammar.
    limits.import_bytes = 4u << 20;
    TlsfDpaStatus status;
    dpa::context source {nullptr, tlsf_dpa_free};
    if (property == "3-parity")
      source =
          dpa::create (tlsf_ast_root (spec.ast.get ()), spec.inputs, spec.outputs, limits, status);
    else {
      std::vector<const char*> in, out;
      for (const auto& name : spec.inputs)
        in.push_back (name.c_str ());
      for (const auto& name : spec.outputs)
        out.push_back (name.c_str ());
      TlsfDpa* raw = nullptr;
      status = tlsf_dpa_create_from_dag (nodes.data (), nodes.size (), root, in.data (),
                                         in.size (), out.data (), out.size (), TLSF_DPA_BUCHI,
                                         &limits.construction, &raw);
      source.reset (raw);
    }
    if (status.code == TLSF_DPA_DECLINED)
      return 77;
    if (status.code != TLSF_DPA_OK || !source)
      throw std::runtime_error ("unexpected DPA construction status");
    const auto imported = dpa::import (*source, limits);
    if (imported.status.code == TLSF_DPA_DECLINED)
      return 77;
    if (!imported.automaton || imported.status.code != TLSF_DPA_OK)
      throw std::runtime_error ("unexpected DPA import status");
    spot::translator translate (imported.automaton->get_dict ());
    const auto reference = translate.run (parsed.f);
    if (reference->num_states () > max_states)
      return 77;
    // P3: Construction and the production label/acceptance importer must
    // preserve the language of the converted objective, for either kind.
    if (!spot::are_equivalent (imported.automaton, reference))
      std::abort ();
    return 0;
  }

  void default_arms (const fs::path& work) {
    const auto input = (work / "default.tlsf").string ();
    char text[TLSF_FUZZ_TEXT_CAPACITY];
    tlsf_fuzz_decode (nullptr, 0, text);
    std::ofstream (input) << text;
    char program[] = "fuzz-replay", option[] = "-T";
    char* argv[] = {program, option, const_cast<char*> (input.c_str ()), nullptr};
    // Use the CLI's actual default expansion, including builds with an empty
    // acacia_default_arms; do not maintain a second list of default arms.
    const auto config = arg_parser (3, argv);
    for (const auto& arm : *config.arms) {
      if (!arm.legacy)
        std::cout << arm.native_name ();
      else {
        const auto& legacy = *arm.legacy;
        std::cout << (arm.unreal ? "unreal:" : "real:")
                  << (arm.unreal ? (legacy.unreal_x == UNREAL_X_FORMULA ? "formula" : "automaton")
                                 : translation_pref_name (legacy.translation_pref))
                  << ':' << acacia::game_backend_name (legacy.backend) << ':'
                  << acacia::automaton_provider_name (legacy.provider);
      }
      std::cout << '\n';
    }
  }
}

int main (int argc, char** argv) {
  try {
    if (argc == 2 && std::string (argv[1]) == "--default-arms") {
      scratch work;
      default_arms (work.path);
      return 0;
    }
    if (argc != 4 || (std::string (argv[2]) != "roundtrip" && std::string (argv[2]) != "dpa"))
      throw std::runtime_error (
          "usage: tlsf-fuzz-replay 2|3|decode|decode-infinite roundtrip|dpa FILE|DIRECTORY\n"
          "       tlsf-fuzz-replay --default-arms");
    const std::string property = argv[1];
    const bool dpa_input = std::string (argv[2]) == "dpa";
    if (property == "decode" || property == "decode-infinite") {
      const auto text = decode (argv[3], dpa_input);
      std::cout << (property == "decode" ? text : replay_texts (text).back ());
      return 0;
    }
    if (property != "2" && property != "3")
      throw std::runtime_error ("property must be 2, 3, decode or decode-infinite");
    if (property == "2" && !have_syfco ()) {
      std::cout << "P2 SKIP: syfco is absent from PATH\n";
      return 77;
    }
    std::vector<fs::path> files;
    if (fs::is_regular_file (argv[3]))
      files.emplace_back (argv[3]);
    else
      for (const auto& entry : fs::recursive_directory_iterator (argv[3]))
        if (entry.is_regular_file ())
          files.push_back (entry.path ());
    std::ranges::sort (files);
    if (files.empty ())
      throw std::runtime_error ("empty corpus");
    scratch work;
    unsigned failures = 0, total_checked = 0;
    for (const auto& kind : property == "2" ? std::vector<std::string> {"2"}
                                            : std::vector<std::string> {"3-parity", "3-buchi"}) {
      unsigned checked = 0, skipped = 0, cases = 0;
      const auto previous_failures = failures;
      for (const auto& file : files) {
        const auto texts = replay_texts (decode (file, dpa_input));
        for (const auto& text : texts) {
          ++cases;
          const auto suffix = &text == &texts.front () ? "" : "-infinite";
          std::cout.flush ();
          const pid_t pid = fork ();
          if (pid < 0)
            throw std::runtime_error ("fork failed");
          if (!pid) {
            setsid ();
            alarm (5);  // Hard bound around every Spot call, including equivalence.
            rlimit core {0, 0};
            setrlimit (RLIMIT_CORE, &core);
            if (!std::getenv ("ACACIA_TEST_SKIP_RLIMIT_AS")) {
              rlimit memory {1u << 30, 1u << 30};
              setrlimit (RLIMIT_AS, &memory);
            }
            try {
              _exit (check (text, kind, work.path));
            } catch (const std::bad_alloc&) {
              _exit (77);
            } catch (const std::length_error&) {
              _exit (77);
            } catch (const std::exception& error) {
              std::cerr << error.what () << '\n';
              _exit (1);
            }
          }
          int status;
          while (waitpid (pid, &status, 0) < 0)
            if (errno != EINTR)
              throw std::runtime_error ("waitpid failed");
          kill (-pid, SIGKILL);  // Also remove a SyFCo child left by an expired alarm.
          if (WIFEXITED (status) && WEXITSTATUS (status) == 0)
            ++checked;
          else if ((WIFEXITED (status) && WEXITSTATUS (status) == 77) ||
                   (WIFSIGNALED (status) && WTERMSIG (status) == SIGALRM))
            ++skipped;
          else {
            ++failures;
            std::cerr << "P" << kind << " FAILED: " << file << suffix << " (wait status " << status
                      << ")\n";
            std::ofstream ("findings-p2.txt", std::ios::app)
                << "P" << kind << " " << file << suffix << " (wait status " << status << ")\n"
                << text << '\n';
            fs::create_directories ("findings");
            const auto name = "findings/" + file.filename ().string () + "-p" + kind + suffix;
            fs::copy_file (file, name, fs::copy_options::overwrite_existing);
            std::ofstream (name + ".tlsf") << text;
          }
        }
      }
      total_checked += checked;
      std::cout << "P" << kind << ": checked=" << checked << " skipped=" << skipped
                << " failed=" << failures - previous_failures << " seeds=" << files.size ()
                << " finite-variants=" << cases - files.size () << '\n';
    }
    return failures ? 1 : total_checked ? 0 : 77;
  } catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
}
