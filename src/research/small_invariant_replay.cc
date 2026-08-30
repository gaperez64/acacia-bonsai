/// Uninstalled research helper: search a recorded region for a small inductive
/// subregion, instead of representing the whole region more compactly.
///
/// A set of generators G is a winning certificate when the initial rank vector
/// lies in its downward closure and, for every generator g and every input
/// class i, some action a and some h in G satisfy tau_{i,a}(g) <= h.  Any such
/// post-fixed point sits inside the greatest winning region, so a verified
/// certificate proves the worker winning without that region ever being built.
///
/// Modes:
///   continue  reproduce the ordinary exact fixed point from a checkpoint.
///             This is the gate: if the offline continuation does not agree
///             with the solver, nothing measured afterwards means anything.
///   core      peel the checkpoint's own maxima to the greatest subset that
///             satisfies the criterion using only those generators.
///
/// A failed search proves nothing.  Only a verified certificate is a result.

#include "research/all_input_actions.hh"

#include <posets/downsets.hh>
#include <posets/vectors.hh>
#include "research/rank_action_replay.hh"

#include <chrono>
#include <deque>
#include <iostream>
#include <limits>
#include <map>
#include <optional>

namespace posets::vectors {
  size_t bool_threshold = 0;
}

namespace {

  using namespace acacia::research;
  using clock_type = std::chrono::steady_clock;
  using state = posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>;
  using SetOfStates = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<state>;

  [[noreturn]] void fail (const std::string& message) {
    std::cerr << "acacia-small-invariant: " << message << '\n';
    std::exit (1);
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
    fail ("meta.tsv has no column " + name + "; a schema-2 dump cannot be used here");
  }

  std::vector<std::filesystem::path> find_checkpoints (const std::filesystem::path& dir) {
    // Numeric order: as strings antichain-10 sorts before antichain-2.
    std::map<long long, std::filesystem::path> byloop;
    for (const auto& entry : std::filesystem::directory_iterator {dir}) {
      const std::string name = entry.path ().filename ().string ();
      if (name.rfind ("antichain-", 0) != 0 or entry.path ().extension () != ".tsv")
        continue;
      // antichain-final.tsv is the region the solver finished with; sort it
      // last, after every numbered checkpoint.
      const bool is_final = name.rfind ("antichain-final", 0) == 0;
      byloop.emplace (is_final ? std::numeric_limits<long long>::max ()
                               : std::strtoll (name.c_str () + 10, nullptr, 10),
                      entry.path ());
    }
    std::vector<std::filesystem::path> out;
    for (auto& [loop, path] : byloop)
      out.push_back (path);
    return out;
  }

  struct counters {
      unsigned long long forward_applications = 0;
      unsigned long long partial_order_checks = 0;
      unsigned long long witness_cache_hits = 0;
      unsigned long long witness_replacements = 0;
      unsigned long long generators_removed = 0;
      unsigned long long cascade_depth = 0;
  };

  /// The greatest subset of `point.maxima` satisfying the criterion, by
  /// decremental peeling.
  ///
  /// A generator survives only while every input class still has an action
  /// whose forward image is dominated by an *active* generator.  Removing one
  /// can invalidate the witness of any generator that pointed at it, so those
  /// are re-queued; the reverse lists are lazy and may name pairs whose witness
  /// has since moved, which is why each is rechecked before being trusted.
  ///
  /// This is the picker's own test (`input_pickers/critical.hh`) run
  /// decrementally over a subset. At the solver's fixed point every generator
  /// passes it -- that is why the picker returns nothing -- so peeling can only
  /// remove anything at an intermediate checkpoint.
  generator_index peel (const checkpoint& point, const input_action_table& table,
                        counters& stats) {
    generator_index index {point.maxima};
    const auto K = static_cast<VECTOR_ELT_T> (point.k);
    const size_t inputs = table.input_count ();

    // witness[g * inputs + i] = the generator that (g, i) currently relies on.
    std::vector<size_t> witness (index.size () * inputs, generator_index::npos);
    std::vector<unsigned> witness_action (index.size () * inputs, 0);
    std::vector<std::vector<std::pair<size_t, size_t>>> dependents (index.size ());

    std::deque<size_t> queue;
    for (size_t g = 0; g < index.size (); ++g)
      queue.push_back (g);

    unsigned long long depth = 0;
    while (not queue.empty ()) {
      const size_t g = queue.front ();
      queue.pop_front ();
      if (not index.is_active (g))
        continue;
      ++depth;

      bool supported = true;
      for (size_t i = 0; i < inputs and supported; ++i) {
        const size_t slot = g * inputs + i;
        const size_t cached = witness[slot];
        if (cached != generator_index::npos and index.is_active (cached)) {
          ++stats.witness_cache_hits;
          continue;
        }
        if (cached != generator_index::npos)
          ++stats.witness_replacements;

        // An input class with no actions can never support a generator, which
        // is also how the picker treats it.
        bool found = false;
        for (size_t a = 0; a < table.actions[i].size (); ++a) {
          ++stats.forward_applications;
          const rank_vector image = apply_forward (index[g], table.actions[i][a], K);
          const size_t h = index.find_dominator (image, stats.partial_order_checks);
          if (h != generator_index::npos) {
            witness[slot] = h;
            witness_action[slot] = static_cast<unsigned> (a);
            dependents[h].emplace_back (g, i);
            found = true;
            break;
          }
        }
        if (not found)
          supported = false;
      }

      if (not supported) {
        index.deactivate (g);
        ++stats.generators_removed;
        for (const auto& [source, input] : dependents[g])
          if (index.is_active (source) and witness[source * inputs + input] == g)
            queue.push_back (source);
        dependents[g].clear ();
      }
    }
    stats.cascade_depth = depth;
    return index;
  }

  /// Recompute everything from scratch against the surviving generators.  The
  /// peeling result is a heuristic until this passes; nothing else is trusted.
  bool verify (const generator_index& index, const input_action_table& table, VECTOR_ELT_T K,
               const rank_vector& init, const rank_vector& safe, size_t& survivors,
               bool& contains_init) {
    std::vector<rank_vector> kept;
    for (size_t g = 0; g < index.size (); ++g)
      if (index.is_active (g))
        kept.push_back (index[g]);
    survivors = kept.size ();
    contains_init = false;
    if (kept.empty ())
      return false;

    for (const auto& g : kept)
      if (not leq (g, safe))
        return false;  // a generator outside the safe set is never a certificate

    generator_index fresh {kept};
    unsigned long long checks = 0;
    for (size_t g = 0; g < fresh.size (); ++g)
      for (size_t i = 0; i < table.input_count (); ++i) {
        bool ok = false;
        for (const auto& avec : table.actions[i])
          if (fresh.find_dominator (apply_forward (fresh[g], avec, K), checks)
              != generator_index::npos) {
            ok = true;
            break;
          }
        if (not ok)
          return false;
      }

    contains_init = fresh.find_dominator (init, checks) != generator_index::npos;
    return true;
  }

  /// Is there a maximum of `region` for which this input has no action whose
  /// forward image stays inside?  This is `input_pickers/critical.hh`'s test,
  /// and it is also the inductive-invariant criterion: an input is critical
  /// exactly when the criterion fails somewhere.
  bool input_is_critical (const SetOfStates& region, const std::vector<action_vec>& actions,
                          VECTOR_ELT_T K) {
    if (actions.empty ())
      return true;  // no action can support anything, as the picker also treats it
    for (const auto& m : region) {
      rank_vector v (m.size (), 0);
      for (size_t c = 0; c < m.size (); ++c)
        v[c] = m[c];
      bool supported = false;
      for (const auto& avec : actions)
        if (region.contains (state (apply_forward (v, avec, K)))) {
          supported = true;
          break;
        }
      if (not supported)
        return true;
    }
    return false;
  }

  /// The ordinary exact fixed point, offline, using the solver's own downset.
  ///
  /// This mirrors the solve loop rather than sweeping every input: find a
  /// critical input, apply that one update, repeat.  Applying CPre for
  /// non-critical inputs is correct but wasteful -- it recomputes an
  /// intersection that cannot remove anything -- and at frontier sizes in the
  /// thousands the wasted meets dominate everything else.
  SetOfStates continue_exact (const std::vector<rank_vector>& maxima,
                              const input_action_table& table, VECTOR_ELT_T K,
                              size_t bool_threshold, size_t max_rounds, size_t& rounds) {
    std::vector<state> seed;
    for (const auto& v : maxima)
      seed.push_back (state (rank_vector (v)));
    SetOfStates region {std::move (seed)};

    rounds = 0;
    for (; rounds < max_rounds; ++rounds) {
      size_t critical = table.input_count ();
      for (size_t i = 0; i < table.input_count (); ++i)
        if (input_is_critical (region, table.actions[i], K)) {
          critical = i;
          break;
        }
      if (critical == table.input_count ())
        return region;  // no critical input: this is the fixed point

      bool first = true;
      SetOfStates image {state (rank_vector (region.begin ()->size (), -1))};
      for (const auto& avec : table.actions[critical]) {
        SetOfStates one = region.apply ([&] (const auto& m) {
          rank_vector v (m.size (), 0);
          for (size_t c = 0; c < m.size (); ++c)
            v[c] = m[c];
          return state (apply_backward (v, avec, K, bool_threshold));
        });
        if (first) {
          image = std::move (one);
          first = false;
        }
        else
          image.union_with (std::move (one));
      }
      region.intersect_with (std::move (image));
      if (region.size () == 0)
        return region;
    }
    return region;
  }

  void usage (std::ostream& out, const char* program) {
    out << "usage: " << program << " --dir DIR --mode continue|core [--no-header]\n"
        << "                 [--max-iterations N]\n"
        << "  DIR is an ACACIA_ANTICHAIN_SNAPSHOT_DIR automaton directory holding\n"
        << "  meta.tsv (schema 3), all-input-actions.tsv and antichain-<loop>.tsv.\n";
  }

}  // namespace

int main (int argc, char** argv) {
  std::string dir, mode = "core";
  bool header = true;
  size_t max_iterations = 100000;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--dir" and i + 1 < argc)
      dir = argv[++i];
    else if (arg == "--mode" and i + 1 < argc)
      mode = argv[++i];
    else if (arg == "--max-iterations" and i + 1 < argc)
      max_iterations = std::strtoull (argv[++i], nullptr, 10);
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
  if (dir.empty () or (mode != "core" and mode != "continue")) {
    usage (std::cerr, argv[0]);
    return 2;
  }

  try {
    const std::filesystem::path root {dir};
    const size_t states = meta_field (root, "states");
    const size_t bool_threshold = meta_field (root, "bool_threshold");
    const size_t init_state = meta_field (root, "init_state");
    posets::vectors::bool_threshold = bool_threshold;
    const auto table = load_input_actions (root / "all-input-actions.tsv", states);
    const rank_vector init = initial_vector (states, init_state);

    if (header) {
      if (mode == "core")
        std::cout << "loop\tk\tafter_bound_raise\tcheckpoint_maxima\tcore_maxima"
                     "\tcore_contains_init\tverified\tforward_applications"
                     "\tpartial_order_checks\twitness_cache_hits\tgenerators_removed"
                     "\tcascade_depth\tprobe_ms\n";
      else
        std::cout << "loop\tk\tcheckpoint_maxima\titerations\tfinal_maxima"
                     "\tfinal_contains_init\tmatches_solver_final\telapsed_ms\n";
    }

    // The solver's own final region, when it recorded one. Gate 0 is not "did
    // the continuation stop" but "did it stop at the same region".
    std::optional<SetOfStates> solver_final;
    if (std::filesystem::exists (root / "antichain-final.tsv")) {
      const checkpoint point = load_checkpoint (root / "antichain-final.tsv", states);
      std::vector<state> seed;
      for (const auto& v : point.maxima)
        seed.push_back (state (rank_vector (v)));
      solver_final.emplace (std::move (seed));
    }

    int problems = 0;
    for (const auto& path : find_checkpoints (root)) {
      const checkpoint point = load_checkpoint (path, states);
      const auto K = static_cast<VECTOR_ELT_T> (point.k);
      const rank_vector safe = safe_vector (states, K, bool_threshold);
      const auto started = clock_type::now ();

      if (mode == "continue") {
        size_t iterations = 0;
        auto region = continue_exact (point.maxima, table, K, bool_threshold, max_iterations,
                                      iterations);
        const bool has_init = region.size () > 0 and region.contains (state (rank_vector (init)));
        std::string agrees = "-";
        if (solver_final.has_value ()) {
          bool same = region.size () == solver_final->size ();
          for (const auto& m : region)
            same = same and solver_final->contains (m);
          for (const auto& m : *solver_final)
            same = same and region.contains (m);
          agrees = same ? "yes" : "NO";
          problems += same ? 0 : 1;
        }
        std::cout << point.loop << '\t' << point.k << '\t' << point.maxima.size () << '\t'
                  << iterations << '\t' << region.size () << '\t' << (has_init ? "yes" : "no")
                  << '\t' << agrees << '\t'
                  << std::chrono::duration_cast<std::chrono::milliseconds> (clock_type::now ()
                                                                           - started)
                         .count ()
                  << '\n';
        continue;
      }

      counters stats;
      const generator_index core = peel (point, table, stats);
      size_t survivors = 0;
      bool contains_init = false;
      const bool verified = verify (core, table, K, init, safe, survivors, contains_init);
      problems += (verified and survivors > 0 and not contains_init) ? 0 : 0;

      std::cout << point.loop << '\t' << point.k << '\t' << (point.after_bound_raise ? 1 : 0)
                << '\t' << point.maxima.size () << '\t' << survivors << '\t'
                << (contains_init ? "yes" : "no") << '\t' << (verified ? "yes" : "no") << '\t'
                << stats.forward_applications << '\t' << stats.partial_order_checks << '\t'
                << stats.witness_cache_hits << '\t' << stats.generators_removed << '\t'
                << stats.cascade_depth << '\t'
                << std::chrono::duration_cast<std::chrono::milliseconds> (clock_type::now ()
                                                                         - started)
                       .count ()
                << '\n';
    }
    return problems == 0 ? 0 : 1;
  }
  catch (const std::exception& error) {
    std::cerr << "acacia-small-invariant: " << error.what () << '\n';
    return 1;
  }
}
