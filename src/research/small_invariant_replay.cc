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
///   width     restart the exact fixed point from progressively wider prefixes
///             of the checkpoint maxima.
///   core      peel the checkpoint's own maxima to the greatest subset that
///             satisfies the criterion using only those generators.
///
/// A failed search proves nothing.  Only a verified certificate is a result.

#include "research/all_input_actions.hh"

#include <posets/downsets.hh>
#include <posets/vectors.hh>
#include "research/rank_action_replay.hh"

#include <algorithm>
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

  std::vector<rank_vector> width_selection_order (const std::vector<rank_vector>& maxima,
                                                  const rank_vector& init) {
    generator_index index {maxima};
    unsigned long long checks = 0;
    const size_t first = index.find_dominator (init, checks);

    std::vector<size_t> order;
    order.reserve (maxima.size ());
    if (first != generator_index::npos)
      order.push_back (first);
    for (size_t i = 0; i < maxima.size (); ++i)
      if (i != first)
        order.push_back (i);

    // Keeping an initial dominator first makes every non-empty prefix relevant;
    // stable descending coordinate sum makes the remaining priority reproducible.
    auto rest = order.begin ();
    if (first != generator_index::npos)
      ++rest;
    std::stable_sort (rest, order.end (), [&] (size_t a, size_t b) {
      return rank_of (maxima[a]) > rank_of (maxima[b]);
    });

    std::vector<rank_vector> selected;
    selected.reserve (maxima.size ());
    for (const size_t i : order)
      selected.push_back (maxima[i]);
    return selected;
  }

  std::vector<size_t> width_schedule (size_t full_width, size_t max_width) {
    std::vector<size_t> widths;
    for (size_t width = 1; width <= 256 and width <= max_width and width < full_width;
         width *= 2)
      widths.push_back (width);
    widths.push_back (full_width);
    return widths;
  }

  bool same_region (const SetOfStates& a, const SetOfStates& b) {
    for (const auto& m : a)
      if (not b.contains (m))
        return false;
    for (const auto& m : b)
      if (not a.contains (m))
        return false;
    return true;
  }

  struct width_result {
      size_t width;
      size_t iterations;
      SetOfStates region;
      bool contains_init;
      long long branch_ms;
  };

  struct kernel_result {
      bool verified = false;
      size_t generators = 0;
      unsigned long long nodes = 0;
      unsigned long long dead_ends = 0;
      unsigned long long envelope_rejections = 0;
      unsigned long long forward_applications = 0;
  };

  struct kernel_search {
      const input_action_table& table;
      VECTOR_ELT_T K;
      const std::vector<rank_vector>& envelope;  ///< the exact approximation's maxima
      rank_vector safe;
      size_t budget;
      unsigned long long node_limit;
      kernel_result stats;

      /// Inside the current approximation.  The approximation contains the true
      /// winning region, so every genuine strategy from the initial vector can
      /// stay inside it -- which makes it a sound pruning envelope, never a
      /// source of candidates.
      [[nodiscard]] bool in_envelope (const rank_vector& v) const {
        for (const auto& m : envelope)
          if (leq (v, m))
            return true;
        return false;
      }

      [[nodiscard]] bool covered (const std::vector<rank_vector>& G,
                                  const rank_vector& v) const {
        for (const auto& g : G)
          if (leq (v, g))
            return true;
        return false;
      }

      /// Depth-first over choices of action for the least-resolved obligation.
      ///
      /// Coverage is monotone in G -- adding a generator never unresolves a
      /// pair -- so an obligation once discharged stays discharged along a
      /// branch, and only the current frontier has to be rescanned.
      bool search (std::vector<rank_vector>& G) {
        if (++stats.nodes > node_limit)
          return false;

        // Fail first: the obligation with the fewest admissible actions is the
        // one most likely to refute this branch, and refuting early is the
        // whole value of a bounded search.
        size_t best_g = G.size ();
        std::vector<rank_vector> best_choices;
        for (size_t g = 0; g < G.size (); ++g)
          for (size_t i = 0; i < table.input_count (); ++i) {
            std::vector<rank_vector> choices;
            bool already = false;
            for (const auto& avec : table.actions[i]) {
              ++stats.forward_applications;
              const rank_vector y = apply_forward (G[g], avec, K);
              if (not leq (y, safe))
                continue;
              if (not in_envelope (y)) {
                ++stats.envelope_rejections;
                continue;
              }
              if (covered (G, y)) {
                already = true;
                break;
              }
              bool seen = false;
              for (const auto& c : choices)
                if (leq (c, y) and leq (y, c)) { seen = true; break; }
              if (not seen)
                choices.push_back (y);
            }
            if (already)
              continue;  // this obligation is already discharged
            if (choices.empty ()) {
              ++stats.dead_ends;
              return false;  // no admissible action at all: this branch is dead
            }
            if (choices.size () < best_choices.size () or best_g == G.size ()) {
              best_g = g;
              best_choices = std::move (choices);
            }
          }

        if (best_g == G.size ())
          return true;  // every obligation discharged: G is inductive
        if (G.size () >= budget)
          return false;

        // Prefer a successor that costs least: a lower rank sum is closer to
        // the reachable states of an actual strategy than an envelope corner.
        std::ranges::sort (best_choices, [] (const rank_vector& a, const rank_vector& b) {
          return rank_of (a) < rank_of (b);
        });
        for (const auto& y : best_choices) {
          G.push_back (y);
          if (search (G))
            return true;
          G.pop_back ();
        }
        return false;
      }
  };

  /// Grow a candidate invariant from the initial vector, inside the envelope.
  ///
  /// More powerful than peeling the checkpoint's own maxima, because the
  /// successors it adds are interior vectors of the approximation rather than
  /// its maximal generators -- and the maxima of an over-approximation are
  /// exactly the vectors most likely to be pruned later.
  kernel_result find_kernel (const checkpoint& point, const input_action_table& table,
                             const rank_vector& init, const rank_vector& safe,
                             size_t budget, unsigned long long node_limit) {
    kernel_search search {table, static_cast<VECTOR_ELT_T> (point.k), point.maxima, safe,
                          budget, node_limit, {}};
    std::vector<rank_vector> G {init};
    const bool found = search.search (G);
    search.stats.verified = found;
    search.stats.generators = G.size ();

    if (found) {
      // Independent re-verification: recompute every forward image against the
      // reduced antichain. The search is a heuristic until this passes.
      std::vector<rank_vector> reduced;
      for (const auto& g : G) {
        bool dominated = false;
        for (const auto& h : G)
          if (&g != &h and leq (g, h) and not leq (h, g)) { dominated = true; break; }
        if (not dominated)
          reduced.push_back (g);
      }
      generator_index fresh {reduced};
      unsigned long long checks = 0;
      bool ok = fresh.find_dominator (init, checks) != generator_index::npos;
      for (size_t g = 0; ok and g < fresh.size (); ++g) {
        if (not leq (fresh[g], safe))
          ok = false;
        for (size_t i = 0; ok and i < table.input_count (); ++i) {
          bool supported = false;
          for (const auto& avec : table.actions[i])
            if (fresh.find_dominator (apply_forward (fresh[g], avec,
                                                     static_cast<VECTOR_ELT_T> (point.k)),
                                      checks)
                != generator_index::npos) {
              supported = true;
              break;
            }
          ok = ok and supported;
        }
      }
      search.stats.verified = ok;
      search.stats.generators = reduced.size ();
    }
    return search.stats;
  }

  void usage (std::ostream& out, const char* program) {
    out << "usage: " << program
        << " --dir DIR --mode continue|core|kernel|width [--no-header]\n"
        << "                 [--max-iterations N] [--max-width W] [--budget B] [--nodes N]\n"
        << "  DIR is an ACACIA_ANTICHAIN_SNAPSHOT_DIR automaton directory holding\n"
        << "  meta.tsv (schema 3), all-input-actions.tsv and antichain-<loop>.tsv.\n";
  }

}  // namespace

int main (int argc, char** argv) {
  std::string dir, mode = "core";
  bool header = true;
  size_t max_iterations = 100000;
  size_t max_width = 256;
  size_t budget = 64;
  unsigned long long node_limit = 200000;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--dir" and i + 1 < argc)
      dir = argv[++i];
    else if (arg == "--mode" and i + 1 < argc)
      mode = argv[++i];
    else if (arg == "--max-iterations" and i + 1 < argc)
      max_iterations = std::strtoull (argv[++i], nullptr, 10);
    else if (arg == "--max-width" and i + 1 < argc)
      max_width = std::strtoull (argv[++i], nullptr, 10);
    else if (arg == "--budget" and i + 1 < argc)
      budget = std::strtoull (argv[++i], nullptr, 10);
    else if (arg == "--nodes" and i + 1 < argc)
      node_limit = std::strtoull (argv[++i], nullptr, 10);
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
  if (dir.empty () or (mode != "core" and mode != "continue" and mode != "kernel"
                       and mode != "width")) {
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
      if (mode == "kernel")
        std::cout << "loop\tk\tcheckpoint_maxima\tbudget\tkernel_maxima\tverified"
                     "\tsearch_nodes\tdead_ends\tenvelope_rejections"
                     "\tforward_applications\tsearch_ms\n";
      else if (mode == "core")
        std::cout << "loop\tk\tafter_bound_raise\tcheckpoint_maxima\tcore_maxima"
                     "\tcore_contains_init\tverified\tforward_applications"
                     "\tpartial_order_checks\twitness_cache_hits\tgenerators_removed"
                     "\tcascade_depth\tprobe_ms\n";
      else if (mode == "width")
        std::cout << "loop\tk\tcheckpoint_maxima\twidth\titerations\tfinal_maxima"
                     "\tcontains_init\tmatches_full_width\tbranch_ms\n";
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

      if (mode == "width") {
        const auto ordered = width_selection_order (point.maxima, init);
        std::vector<width_result> results;
        for (const size_t width : width_schedule (point.maxima.size (), max_width)) {
          // A contracted branch cannot be widened soundly, so every width is
          // seeded anew. The full branch is the ordinary continuation itself.
          const std::vector<rank_vector> maxima =
              width == point.maxima.size ()
                  ? point.maxima
                  : std::vector<rank_vector> (ordered.begin (), ordered.begin () + width);
          const auto branch_started = clock_type::now ();
          size_t iterations = 0;
          auto region = continue_exact (maxima, table, K, bool_threshold, max_iterations,
                                        iterations);
          const bool has_init =
              region.size () > 0 and region.contains (state (rank_vector (init)));
          const auto branch_ms =
              std::chrono::duration_cast<std::chrono::milliseconds> (clock_type::now ()
                                                                     - branch_started)
                  .count ();
          results.push_back (
              {width, iterations, std::move (region), has_init, branch_ms});
        }

        const SetOfStates& full = results.back ().region;
        for (size_t i = 0; i < results.size (); ++i) {
          const auto& r = results[i];
          const std::string matches =
              i + 1 == results.size () ? "-" : (same_region (r.region, full) ? "yes" : "no");
          std::cout << point.loop << '\t' << point.k << '\t' << point.maxima.size () << '\t'
                    << r.width << '\t' << r.iterations << '\t' << r.region.size () << '\t'
                    << (r.contains_init ? "yes" : "no") << '\t' << matches << '\t'
                    << r.branch_ms << '\n';
        }
        continue;
      }

      if (mode == "kernel") {
        const kernel_result r = find_kernel (point, table, init, safe, budget, node_limit);
        std::cout << point.loop << '\t' << point.k << '\t' << point.maxima.size () << '\t'
                  << budget << '\t' << r.generators << '\t' << (r.verified ? "yes" : "no")
                  << '\t' << r.nodes << '\t' << r.dead_ends << '\t'
                  << r.envelope_rejections << '\t' << r.forward_applications << '\t'
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
