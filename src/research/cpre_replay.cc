/// Uninstalled fixed-work replay of a complete schema-2 CPre event.
#include "research/cpre_downset_replay.hh"
#include "utils/verbose.hh"

#include <posets/downsets.hh>
#include <posets/vectors.hh>

namespace posets::vectors {
  size_t bool_threshold = 0;
}
namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}

#define ACACIA_STRINGIFY_IMPL(x) #x
#define ACACIA_STRINGIFY(x) ACACIA_STRINGIFY_IMPL (x)

namespace {
  using namespace acacia::research;
  using namespace acacia::research::downset_replay;
  using state = posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>;
  using SetOfStates = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<state>;
  constexpr std::string_view backend = ACACIA_STRINGIFY (VECTOR_AND_BITSET_DOWNSET_IMPL);
  constexpr std::string_view model = ACACIA_STRINGIFY (ACTIONER) ==
                                            std::string_view {"actioners::standard"}
                                        ? "production-mode" : "algebra-only";

  void usage (std::ostream& out, const char* program) {
    out << "usage: " << program << " --dir DIR [--no-header] [--measure]\n"
        << "  Complete schema-2 events only; --measure is for the external driver.\n";
  }
}

int main (int argc, char** argv) {
  std::string dir;
  bool header = true, measured = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--dir" and i + 1 < argc)
      dir = argv[++i];
    else if (arg == "--no-header")
      header = false;
    else if (arg == "--measure")
      measured = true;
    else if (arg == "--help") {
      usage (std::cout, argv[0]);
      return 0;
    }
    else {
      usage (std::cerr, argv[0]);
      return 2;
    }
  }
  if (dir.empty ()) {
    usage (std::cerr, argv[0]);
    return 2;
  }

  try {
    const std::filesystem::path root {dir};
    const size_t states = meta_field (root, "states");
    posets::vectors::bool_threshold = meta_field (root, "bool_threshold");
    if (header)
      std::cout << "loop\tk\tbefore\tactions\tsolver_after\treplay_after\texact"
                << "\tbackend\tmode\tmeasured\tconstruction_ms\tmembership_ms\timage_ms"
                << "\tcombine_ms\tintersection_ms\trebuild_ms\tdestruction_ms\tkernel_ms"
                << "\ttotal_ms\tbefore_bytes_estimate\tafter_bytes_estimate"
                << "\tlargest_temporary_bytes_estimate\tmembership_hits\tmodel\n";
    const auto paths = find_events (root);
    if (paths.empty ())
      throw std::invalid_argument ("no CPre events");
    int mismatches = 0;
    for (const auto& path : paths) {
      const event ev = load (path, states);
      if (ev.schema_version != 2 or not ev.complete)
        throw std::invalid_argument ("unsupported or incomplete CPre event: " + path.string ());
      validate (ev, posets::vectors::bool_threshold);
      costs cost;
      cost.measured = measured;
      std::optional<SetOfStates> f;
      {
        timer timing {cost, stage::construction};
        f.emplace (construct<SetOfStates> (ev.before, backend));
      }
      const auto before_n = f->size ();
      unsigned hits = 0;
      {
        timer timing {cost, stage::membership};
        for (const auto& row : ev.before)
          hits += f->contains (state (rank_vector (row)));
        for (const auto& row : ev.after)
          hits += f->contains (state (rank_vector (row)));
      }
      update (*f, ev, cost, backend);
      const auto after_n = f->size ();
      // Plain integer maximal generators, independent of candidate membership.
      const auto exact = rows_of (*f) == maxima (rows_of (ev.after));
      mismatches += not exact;
      std::optional<SetOfStates> rebuilt;
      {
        timer timing {cost, stage::rebuild};
        rebuilt.emplace (f->apply ([] (const auto& m) { return m.copy (); }));
      }
      if (rows_of (*rebuilt) != rows_of (*f))
        throw std::runtime_error ("rebuild changed represented region");
      {
        timer timing {cost, stage::destruction};
        rebuilt.reset ();
        f.reset ();
      }
      for (const auto ms : cost.ms)
        cost.total_ms += ms;
      const size_t generators = std::max (before_n, after_n);
      const size_t image_count = before_n * ev.actions.size ();
      const size_t temporary_count = std::max ({generators, image_count, before_n * image_count});
      std::cout << ev.loop << '\t' << ev.k << '\t' << ev.before.size () << '\t'
                << ev.actions.size () << '\t' << ev.after.size () << '\t' << after_n << '\t'
                << (exact ? "yes" : "NO") << '\t' << backend << '\t' << CPRE_AVOID_UNIONS << '\t'
                << (measured ? "yes" : "no");
      for (const auto ms : cost.ms)
        std::cout << '\t' << ms;
      std::cout << '\t' << cost.kernel_ms << '\t' << cost.total_ms << '\t'
                << region_bytes<SetOfStates> (before_n, states, backend) << '\t'
                << region_bytes<SetOfStates> (after_n, states, backend) << '\t'
                << region_bytes<SetOfStates> (temporary_count, states, backend) << '\t' << hits
                << '\t' << model << '\n';
    }
    return mismatches == 0 ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "acacia-cpre-replay: " << error.what () << '\n';
    return 1;
  }
}
