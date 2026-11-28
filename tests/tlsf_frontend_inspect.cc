#include "tlsf_frontend.hh"
#include "solver/bounded_subprocess.hh"
#include "solver/encoded_family.hh"
#include "solver/realizability_simplify.hh"

#include <cstring>
#include <fstream>
#include <iostream>
#include <span>
#include <spot/tl/parse.hh>
#include <sstream>
#include <string>
#include <sys/mman.h>

namespace {

  void print_list (std::span<const std::string> values) {
    for (size_t i = 0; i < values.size (); ++i) {
      if (i != 0)
        std::cout << ',';
      std::cout << values[i];
    }
    std::cout.put ('\0');
  }

  void inspect_encoded (const char* path, std::ostream& out, std::chrono::steady_clock::time_point deadline) {
    std::ifstream file (path);
    if (not file)
      throw std::runtime_error (std::string {"unable to open "} + path);
    std::ostringstream text;
    text << file.rdbuf ();
    auto spec = acacia::tlsf_frontend::parse_with_value_origins (text.str ());
    const auto checked = symmetry::check_encoded_family (spec, 1u << 22, deadline, true);
    if (not checked) {
      out << "declined\n";
      return;
    }
    const auto& cert = *checked;
    out << "certified " << cert.moving;
    for (auto c : cert.coordinates)
      out << ' ' << c;
    out << '\n';
    for (const auto& bus : cert.buses) {
      out << "bus " << bus.is_input << ' ' << bus.members.size ();
      for (size_t i = 0; i < bus.codes.size (); ++i) {
        out << ' ' << bus.codes[i];
        if (bus.aliases[i])
          out << '/' << *bus.aliases[i];
      }
      out << '\n';
      for (const auto& group : bus.unused_classes) {
        out << "unused";
        for (auto code : group)
          out << ' ' << code;
        out << '\n';
      }
    }
    for (const auto& p : cert.predicates) {
      out << "table";
      for (auto [bus, next] : p.slots)
        out << ' ' << bus << '/' << next;
      out << ' ';
      for (bool value : p.truth)
        out << value;
      out << '\n';
    }
    for (const auto& swaps : cert.generators) {
      out << "swap";
      for (auto [bus, first, second] : swaps)
        out << ' ' << bus << '/' << first << '/' << second;
      out << '\n';
    }
  }

  void inspect_encoded (const char* path) {
    using namespace acacia;
    constexpr auto duration = std::chrono::seconds (15);
    struct payload {
        size_t size;
        char text[16u << 20];
    };
    const auto unmap = [] (payload* p) { munmap (p, sizeof (payload)); };
    auto* mapping = static_cast<payload*> (mmap (
        nullptr, sizeof (payload), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    if (mapping == MAP_FAILED) {
      std::cout << "declined\n";
      return;
    }
    std::unique_ptr<payload, decltype (unmap)> data (mapping, unmap);
    const auto deadline = std::chrono::steady_clock::now () + duration;
    const auto stop_ns = phase_clock (CLOCK_MONOTONIC) + duration / std::chrono::nanoseconds (1);
    const auto run = subprocess::run<true> (stop_ns, subprocess::max_optional_memory, [&] {
      std::ostringstream out;
      inspect_encoded (path, out, deadline);
      const auto text = out.view ();
      if (text.size () > sizeof (data->text))
        return subprocess::resource_exit;
      std::memcpy (data->text, text.data (), text.size ());
      data->size = text.size ();
      return 0;
    });
    if (run.code == 0 && std::strcmp (run.cause, "none") == 0)
      std::cout.write (data->text, data->size);
    else
      std::cout << "declined\n";
  }

}  // namespace

int main (int argc, char** argv) {
  const bool apply_rsimp = argc == 3 and std::string (argv[1]) == "--rsimp";
  const bool encoded = argc == 3 and std::string (argv[1]) == "--encoded";
  if (argc != 2 and not apply_rsimp and not encoded) {
    std::cerr << "usage: tlsf-frontend-inspect [--rsimp|--encoded] FILE.tlsf\n";
    return 2;
  }
  try {
    if (encoded) {
      inspect_encoded (argv[2]);
      return 0;
    }
    const auto parsed = acacia::tlsf_frontend::load (argv[apply_rsimp ? 2 : 1]);
    if (apply_rsimp) {
      auto formula = spot::parse_infix_psl (
          parsed.formula, spot::default_environment::instance (), false, false);
      if ((not formula.f) or (not formula.errors.empty ())) {
        formula.format_errors (std::cerr);
        return 1;
      }
      acacia::realizability::apply_simplifier (formula.f, parsed.inputs);
      std::cout << formula.f;
    }
    else
      std::cout << parsed.formula;
    std::cout.put ('\0');
    print_list (parsed.inputs);
    print_list (parsed.outputs);
    std::cout << parsed.metadata.tlsf_semantics;
    std::cout.put ('\0');
    std::cout << parsed.metadata.tlsf_target;
    std::cout.put ('\0');
    std::cout << parsed.metadata.tlsf_effective_target;
    std::cout.put ('\0');
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
}
