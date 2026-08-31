/// Uninstalled research helper: recompute a recorded controller-predecessor
/// update symbolically, over a unary threshold encoding, and check it against
/// the region the solver produced.
///
/// The M2 question is not whether a region compresses but whether the complete
/// input-conditioned update survives the compression.  This answers it for one
/// candidate representation, chosen because it needs no symmetry: the hard M2
/// automata have no verified permutation group, so orbit and histogram
/// representations are not defined on them.
///
/// Encoding.  For coordinate q and level l, the variable x[q,l] means
/// `rank (q) >= l`.  Rank -1 is the absence of every level, so a numeric
/// coordinate uses levels 0..K and a Boolean coordinate (index at or above
/// bool_threshold, domain {-1, 0}) needs only level 0.  Under this encoding the
/// downward closure of a maximum is a cube, `max` is disjunction, and the
/// action's decrement is a shift by one level -- which is what makes the whole
/// update expressible without enumerating anything.
///
/// The update needs no quantifier.  W is downward closed, so for a fixed image
/// point v the constraint `v <= apply (m, a)` is exactly `m >= g_a (v)` for a
/// least witness g_a (v), and "some m in W lies above g_a (v)" is equivalent to
/// "g_a (v) is itself in W".  Hence
///
///     Pre_a (W) (v)  =  W (g_a (v))  and  v <= backward_reset
///     W_next (v)     =  W (v)  and  OR over actions a of Pre_a (W) (v)
///
/// and W (g_a (v)) is a variable substitution into W rather than a relational
/// image: x[i,L] is replaced by `g_a (v)[i] >= L`, which under the threshold
/// encoding is the disjunction `OR over (j, increment) in avec[i] of
/// x[j, max (0, L - increment)]`.  One copy of the variables suffices.
///
/// The relational form -- build T_a (m, v) over two variable copies and
/// existentially quantify m -- is also correct and was tried first.  It does not
/// finish on the smallest recorded event, and that is a fact about the
/// formulation rather than about the representation, which is why it is not
/// what is measured here.
///
/// Exactness is checked as BDD equality against the recorded region, so it
/// never has to extract maxima either.

#include "research/cpre_event.hh"

#include <bddx.h>

#include <chrono>

namespace {

  using namespace acacia::research;
  using clock_type = std::chrono::steady_clock;

  /// One copy of the variables: x[q,l] means `rank (q) >= l`.  A Boolean
  /// coordinate (index at or above bool_threshold, domain {-1, 0}) needs only
  /// level 0; a numeric one needs levels 0..K.
  struct encoding {
      size_t states = 0;
      size_t bool_threshold = 0;
      int K = 0;
      std::vector<int> base;
      std::vector<int> levels;
      std::vector<std::vector<int>> index;  ///< level-major only
      bool level_major = false;

      [[nodiscard]] int levels_of (size_t q) const { return levels[q]; }

      [[nodiscard]] int variable (size_t q, int l) const {
        return level_major ? index[q][l] : base[q] + l;
      }

      [[nodiscard]] bdd var (size_t q, int l) const {
        if (l < 0)
          return bddtrue;  // every rank is at least -1
        if (l > levels[q])
          return bddfalse;
        return bdd_ithvar (variable (q, l));
      }
  };

  enum class order { coordinate_major, level_major };

  /// Two layouts.  Coordinate-major keeps a coordinate's levels adjacent, which
  /// suits the monotonicity chains; level-major groups every coordinate's level
  /// l together, which suits an action that shifts by one level.  The sprint
  /// brief asks for both, and a representation that only works under one of
  /// them has a variable-order dependence worth knowing about.
  encoding build_encoding (const event& ev, size_t bool_threshold, order layout) {
    encoding e;
    e.states = ev.states;
    e.bool_threshold = bool_threshold;
    e.K = ev.k;
    e.levels.resize (ev.states);
    e.base.resize (ev.states);
    for (size_t q = 0; q < ev.states; ++q)
      e.levels[q] = q < bool_threshold ? e.K : 0;

    int next = 0;
    if (layout == order::coordinate_major) {
      for (size_t q = 0; q < ev.states; ++q) {
        e.base[q] = next;
        next += e.levels[q] + 1;
      }
      bdd_setvarnum (next);
      return e;
    }

    // Level-major: variables are not contiguous per coordinate, so the encoding
    // needs an explicit index rather than a base plus offset.
    e.index.assign (ev.states, {});
    for (size_t q = 0; q < ev.states; ++q)
      e.index[q].assign (e.levels[q] + 1, -1);
    for (int l = 0; l <= e.K; ++l)
      for (size_t q = 0; q < ev.states; ++q)
        if (l <= e.levels[q])
          e.index[q][l] = next++;
    e.level_major = true;
    bdd_setvarnum (next);
    return e;
  }

  /// x[q,l] implies x[q,l-1]: only monotone assignments encode a rank.
  bdd monotone (const encoding& e) {
    bdd out = bddtrue;
    for (size_t q = 0; q < e.states; ++q)
      for (int l = 1; l <= e.levels_of (q); ++l)
        out &= bdd_imp (e.var (q, l), e.var (q, l - 1));
    return out;
  }

  /// The downward closure of one maximum is a cube: rank (q) <= m[q] is exactly
  /// the absence of level m[q]+1.
  bdd principal_downset (const encoding& e,
                         const posets::utils::vector_mm<VECTOR_ELT_T>& m) {
    bdd cube = bddtrue;
    for (size_t q = 0; q < e.states; ++q) {
      const int above = static_cast<int> (m[q]) + 1;
      if (above > e.levels_of (q))
        continue;
      cube &= not e.var (q, above);
    }
    return cube;
  }

  bdd region (const encoding& e,
              const std::vector<posets::utils::vector_mm<VECTOR_ELT_T>>& maxima) {
    bdd out = bddfalse;
    for (const auto& m : maxima)
      out |= principal_downset (e, m);
    return out & monotone (e);
  }

  /// v is at or below the image's starting point, which only ever decreases:
  /// K-1 on a numeric coordinate, 0 on a Boolean one, the latter vacuous.
  bdd reset_bound (const encoding& e) {
    bdd out = bddtrue;
    for (size_t q = 0; q < e.bool_threshold and q < e.states; ++q)
      out &= not e.var (q, e.K);
    return out;
  }

  /// Substitution realizing W (g_a (v)).
  struct substitution {
      bddPair* pair;
      std::vector<bdd> images;
      ~substitution () { bdd_freepair (pair); }
  };

  bdd compose_pre (const encoding& e, const bdd& W, const action_vec& avec, int& relation_peak) {
    // avec[i] holds the pairs (j, increment) by which coordinate i constrains
    // the image at j, which is how actioners::standard stores it.
    substitution sub {bdd_newpair (), {}};
    sub.images.reserve (e.states);

    for (size_t i = 0; i < e.states; ++i)
      for (int L = 0; L <= e.levels_of (i); ++L) {
        bdd image = bddfalse;
        if (i < avec.size ())
          for (const auto& [j, increment] : avec[i])
            image |= e.var (j, std::max (0, L - (increment ? 1 : 0)));
        relation_peak = std::max (relation_peak, bdd_nodecount (image));
        sub.images.push_back (image);
        bdd_setbddpair (sub.pair, e.variable (i, L), image);
      }
    return bdd_veccompose (W, sub.pair);
  }

  struct measurement {
      bool exact = false;
      int before_nodes = 0, after_nodes = 0, peak_nodes = 0, relation_peak = 0;
      long long build_ms = 0, cpre_ms = 0;
  };

  measurement replay (const event& ev, size_t bool_threshold, order layout) {
    measurement out;
    const encoding e = build_encoding (ev, bool_threshold, layout);

    const auto build_started = clock_type::now ();
    const bdd W = region (e, ev.before);
    const bdd expected = region (e, ev.after);
    const bdd bound = reset_bound (e);
    out.before_nodes = bdd_nodecount (W);
    out.build_ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                       clock_type::now () - build_started)
                       .count ();

    const auto cpre_started = clock_type::now ();
    bdd image = bddfalse;
    for (const auto& avec : ev.actions) {
      image |= compose_pre (e, W, avec, out.relation_peak) & bound;
      out.peak_nodes = std::max (out.peak_nodes, bdd_nodecount (image));
    }
    const bdd next = W & image;
    out.cpre_ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                      clock_type::now () - cpre_started)
                      .count ();
    out.after_nodes = bdd_nodecount (next);
    out.peak_nodes = std::max (out.peak_nodes, out.after_nodes);
    out.exact = (next == expected);
    return out;
  }

  void usage (std::ostream& out, const char* program) {
    out << "usage: " << program << " --dir DIR [--no-header] [--nodes N] [--cache N]\n"
        << "  --order coordinate|level   variable layout (default coordinate)\n"
        << "  DIR is an ACACIA_ANTICHAIN_SNAPSHOT_DIR automaton directory with\n"
        << "  meta.tsv and one or more cpre-<loop>.tsv events.\n";
  }

}  // namespace

int main (int argc, char** argv) {
  std::string dir;
  bool header = true;
  int nodes = 4000000, cache = 400000;
  order layout = order::coordinate_major;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--dir" and i + 1 < argc)
      dir = argv[++i];
    else if (arg == "--nodes" and i + 1 < argc)
      nodes = std::atoi (argv[++i]);
    else if (arg == "--cache" and i + 1 < argc)
      cache = std::atoi (argv[++i]);
    else if (arg == "--order" and i + 1 < argc) {
      const std::string value = argv[++i];
      if (value == "level")
        layout = order::level_major;
      else if (value != "coordinate") {
        std::cerr << "unknown --order " << value << "\n";
        return 2;
      }
    }
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
    const size_t bool_threshold = meta_field (root, "bool_threshold");

    bdd_init (nodes, cache);
    bdd_setmaxincrease (nodes);

    if (header)
      std::cout << "order\tloop\tk\tstates\tnumeric\tbdd_vars\texplicit_before\texplicit_after"
                   "\tbdd_before\tbdd_after\tbdd_peak\trelation_peak\tbuild_ms\tcpre_ms\texact\n";

    int mismatches = 0;
    for (const auto& path : find_events (root)) {
      const event ev = load (path, states);
      if (ev.schema_version != 2)
        fail ("unsupported schema_version " + std::to_string (ev.schema_version));
      const measurement m = replay (ev, bool_threshold, layout);
      const size_t numeric = std::min (bool_threshold, states);
      const size_t vars = numeric * (ev.k + 1) + (states - numeric);
      mismatches += m.exact ? 0 : 1;
      std::cout << (layout == order::level_major ? "level" : "coordinate") << '\t'
                << ev.loop << '\t' << ev.k << '\t' << states << '\t' << numeric << '\t' << vars
                << '\t' << ev.before.size () << '\t' << ev.after.size () << '\t'
                << m.before_nodes << '\t' << m.after_nodes << '\t' << m.peak_nodes << '\t'
                << m.relation_peak << '\t' << m.build_ms << '\t' << m.cpre_ms << '\t'
                << (m.exact ? "yes" : "NO") << '\n';
    }
    bdd_done ();
    return mismatches == 0 ? 0 : 1;
  }
  catch (const std::exception& error) {
    std::cerr << "acacia-bdd-cpre-replay: " << error.what () << '\n';
    return 1;
  }
}
