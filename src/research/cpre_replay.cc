/// Uninstalled research helper: replay one recorded controller-predecessor
/// update offline and check that it reproduces what the solver produced.
///
/// This is the correctness floor for every compressed-representation
/// experiment.  A compression is only interesting if the *operation* survives
/// it, and that claim is meaningless unless the explicit operation can first be
/// reproduced exactly outside the solver.  So this tool computes
///
///     D_after  =  D_before  intersect  union over actions a of Pre_a (D_before)
///
/// from a `cpre-<loop>.tsv` event and compares it against the `[after]` region
/// the solver recorded.  Production paths are not altered.
///
/// The event carries the action vectors themselves, which determine the update
/// completely, so no automaton, BDD library or Spot dictionary is needed here.

#include "research/cpre_event.hh"

#include <posets/downsets.hh>
#include <posets/vectors.hh>

namespace posets::vectors {
  size_t bool_threshold = 0;
}

namespace {

  using namespace acacia::research;

  using state = posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>;
  using SetOfStates = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<state>;

  /// The backward image, transcribed from actioners::standard::apply so the
  /// replay cannot drift from the operation it is checking.
  state apply_backward (const posets::utils::vector_mm<VECTOR_ELT_T>& m,
                        const action_vec& avec, VECTOR_ELT_T K) {
    posets::utils::vector_mm<VECTOR_ELT_T> out (m.size (), 0);
    std::fill_n (out.begin (), posets::vectors::bool_threshold, (VECTOR_ELT_T) (K - 1));
    std::fill_n (out.begin () + posets::vectors::bool_threshold,
                 m.size () - posets::vectors::bool_threshold, (VECTOR_ELT_T) 0);

    for (size_t p = 0; p < m.size (); ++p)
      for (const auto& [q, p_final] : avec[p])
        if (out[q] != -1)
          out[q] = std::min (out[q], std::max ((VECTOR_ELT_T) -1,
                                               (VECTOR_ELT_T) (m[p] - (VECTOR_ELT_T) (p_final ? 1 : 0))));
    return state (out);
  }

  SetOfStates replay (const event& ev) {
    std::vector<state> before;
    for (const auto& v : ev.before)
      before.push_back (state (posets::utils::vector_mm<VECTOR_ELT_T> (v)));
    SetOfStates f {std::move (before)};

    bool first = true;
    SetOfStates f1i {state (posets::utils::vector_mm<VECTOR_ELT_T> (ev.states, -1))};
    for (const auto& avec : ev.actions) {
      std::vector<state> images;
      for (const auto& v : ev.before)
        images.push_back (apply_backward (v, avec, (VECTOR_ELT_T) ev.k));
      SetOfStates f1io {std::move (images)};
      if (first) {
        f1i = std::move (f1io);
        first = false;
      }
      else
        f1i.union_with (std::move (f1io));
    }
    f.intersect_with (std::move (f1i));
    return f;
  }

  bool same_downset (const SetOfStates& a, const SetOfStates& b) {
    if (a.size () != b.size ())
      return false;
    for (const auto& m : a)
      if (not b.contains (m))
        return false;
    for (const auto& m : b)
      if (not a.contains (m))
        return false;
    return true;
  }



  void usage (std::ostream& out, const char* program) {
    out << "usage: " << program << " --dir DIR [--no-header]\n"
        << "  DIR is an ACACIA_ANTICHAIN_SNAPSHOT_DIR automaton directory\n"
        << "  containing meta.tsv and one or more cpre-<loop>.tsv events.\n";
  }

}  // namespace

int main (int argc, char** argv) {
  std::string dir;
  bool header = true;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--dir" and i + 1 < argc)
      dir = argv[++i];
    else if (arg == "--no-header")
      header = false;
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
      std::cout << "loop\tk\tbefore\tactions\tsolver_after\treplay_after\texact\n";

    int mismatches = 0;
    for (const auto& path : find_events (root)) {
      const event ev = load (path, states);
      if (ev.schema_version != 2)
        fail ("unsupported schema_version " + std::to_string (ev.schema_version) + " in "
              + path.string ());
      const SetOfStates replayed = replay (ev);

      std::vector<state> recorded;
      for (const auto& v : ev.after)
        recorded.push_back (state (posets::utils::vector_mm<VECTOR_ELT_T> (v)));
      const SetOfStates expected {std::move (recorded)};

      const bool exact = same_downset (replayed, expected);
      mismatches += exact ? 0 : 1;
      std::cout << ev.loop << '\t' << ev.k << '\t' << ev.before.size () << '\t'
                << ev.actions.size () << '\t' << ev.after.size () << '\t' << replayed.size ()
                << '\t' << (exact ? "yes" : "NO") << '\n';
    }
    return mismatches == 0 ? 0 : 1;
  }
  catch (const std::exception& error) {
    std::cerr << "acacia-cpre-replay: " << error.what () << '\n';
    return 1;
  }
}
