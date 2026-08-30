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

#include "configuration.hh"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <posets/downsets.hh>
#include <posets/utils/vector_mm.hh>
#include <posets/vectors.hh>

namespace posets::vectors {
  size_t bool_threshold = 0;
}

namespace {

  using state = posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>;
  using SetOfStates = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<state>;

  /// avec[i] is a list of (j, increment); `apply` below reads it exactly as
  /// actioners::standard does, with the same names it uses.
  using action = std::vector<std::pair<unsigned, bool>>;
  using action_vec = std::vector<action>;

  struct event {
      int schema_version = 0;
      int k = -1;
      int loop = -1;
      size_t states = 0;
      std::vector<posets::utils::vector_mm<VECTOR_ELT_T>> before, after;
      std::vector<action_vec> actions;
  };

  [[noreturn]] void fail (const std::string& message) {
    std::cerr << "acacia-cpre-replay: " << message << '\n';
    std::exit (1);
  }

  long long field (const std::string& text, const std::string& key) {
    const auto at = text.find (key + "=");
    if (at == std::string::npos)
      return -1;
    return std::strtoll (text.c_str () + at + key.size () + 1, nullptr, 10);
  }

  std::vector<VECTOR_ELT_T> parse_row (const std::string& line) {
    std::vector<VECTOR_ELT_T> row;
    std::istringstream in {line};
    int value;
    while (in >> value)
      row.push_back (static_cast<VECTOR_ELT_T> (value));
    return row;
  }

  event load (const std::filesystem::path& path, size_t states) {
    std::ifstream in {path};
    if (not in)
      fail ("cannot open " + path.string ());

    event ev;
    ev.states = states;
    std::string line;
    enum { none, before, actions, after } section = none;

    while (std::getline (in, line)) {
      if (line.empty ())
        continue;
      if (line[0] == '#') {
        ev.schema_version = static_cast<int> (field (line, "schema_version"));
        ev.k = static_cast<int> (field (line, "k"));
        ev.loop = static_cast<int> (field (line, "loop"));
        continue;
      }
      if (line.rfind ("[before]", 0) == 0) { section = before; continue; }
      if (line.rfind ("[actions]", 0) == 0) { section = actions; continue; }
      if (line.rfind ("[after]", 0) == 0) { section = after; continue; }

      if (section == before or section == after) {
        auto row = parse_row (line);
        if (row.size () != states)
          fail ("row of width " + std::to_string (row.size ()) + " where meta.tsv says "
                + std::to_string (states));
        posets::utils::vector_mm<VECTOR_ELT_T> v (states, 0);
        std::copy (row.begin (), row.end (), v.begin ());
        (section == before ? ev.before : ev.after).push_back (std::move (v));
      }
      else if (section == actions) {
        if (line.rfind ("action\t", 0) == 0) {
          ev.actions.emplace_back (states);
          continue;
        }
        if (ev.actions.empty ())
          fail ("transition row before any action header");
        std::istringstream row {line};
        unsigned i, j;
        int increment;
        if (not (row >> i >> j >> increment))
          fail ("malformed transition row: " + line);
        if (i >= states)
          fail ("transition row indexes state " + std::to_string (i) + " of "
                + std::to_string (states));
        ev.actions.back ()[i].emplace_back (j, increment != 0);
      }
    }
    return ev;
  }

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

  size_t meta_field (const std::filesystem::path& dir, const std::string& name) {
    std::ifstream meta {dir / "meta.tsv"};
    if (not meta)
      fail ("cannot open " + (dir / "meta.tsv").string ());
    std::string header, values;
    std::getline (meta, header);
    std::getline (meta, values);
    std::istringstream hs {header}, vs {values};
    std::string h, v;
    while (hs >> h and vs >> v)
      if (h == name)
        return static_cast<size_t> (std::strtoull (v.c_str (), nullptr, 10));
    fail ("meta.tsv has no column " + name);
  }

  /// Numeric order, not filename order: `cpre-10.tsv` sorts before `cpre-2.tsv`
  /// as a string, and replaying loops out of order silently compares the wrong
  /// regions.
  std::vector<std::filesystem::path> find_events (const std::filesystem::path& dir) {
    std::map<long long, std::filesystem::path> byloop;
    for (const auto& entry : std::filesystem::directory_iterator {dir}) {
      const std::string name = entry.path ().filename ().string ();
      if (name.rfind ("cpre-", 0) != 0 or entry.path ().extension () != ".tsv")
        continue;
      byloop.emplace (std::strtoll (name.c_str () + 5, nullptr, 10), entry.path ());
    }
    std::vector<std::filesystem::path> out;
    for (auto& [loop, path] : byloop)
      out.push_back (path);
    return out;
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
