#pragma once

#include "configuration.hh"

#if ACACIA_FWD_ORACLES
# include "solver/direct_simulation.hh"
# include "solver/sparse_forward_rank.hh"
# include "solver/symmetry.hh"
# include <string_view>
# include <unordered_set>

# include <cstdlib>
# include <tuple>

namespace acacia::spot_lazy_game {
  // P0 diagnostics only. These facts describe the immutable *final* frozen
  // graph, never an explored prefix of a demand provider. Winning certificates
  // are lifted and checked against the original, unquotiented game.
  class FrozenRankOracles {
      using Rank = spot_rows::SparseForwardRank<>;
      using StateId = spot_rows::StateId;
      bool cap_ = false, sim_ = false, sym_ = false;
      std::vector<int> headroom_;
      direct_simulation::relation simulation_;
      std::vector<std::vector<unsigned>> generators_;

      static bool verified_symmetry (spot_rows::FrozenAcacia view, bdd inputs, bdd outputs,
                                     const std::vector<unsigned>& g,
                                     spot::relabeling_map& rename) {
        const auto& graph = view.graph;
        const unsigned n = graph->num_states ();
        if (g.size () != n ||
            g[graph->get_init_state_number ()] != graph->get_init_state_number ())
          return false;
        std::vector<bool> seen (n, false);
        for (unsigned q = 0; q < n; ++q) {
          if (g[q] >= n || seen[g[q]] ||
              (q < view.bool_threshold) != (g[q] < view.bool_threshold) ||
              graph->state_is_accepting (q) != graph->state_is_accepting (g[q]))
            return false;
          seen[g[q]] = true;
        }
        // The shared detector supplies candidates only: its family I/O tag can
        // hide mixed membership. Check the actual APs against BOTH cubes.
        for (const auto& [from, to] : rename) {
          const bdd a = bdd_ithvar (graph->get_dict ()->varnum (from));
          const bdd c = bdd_ithvar (graph->get_dict ()->varnum (to));
          for (const bdd& side : {inputs, outputs})
            if ((bdd_exist (side, a) != side) != (bdd_exist (side, c) != side))
              return false;
        }
        auto renamed = spot::make_twa_graph (graph, spot::twa::prop_set::all ());
        spot::relabel_here (renamed, &rename);
        using Edge = std::tuple<unsigned, int, spot::acc_cond::mark_t>;
        for (unsigned q = 0; q < n; ++q) {
          std::vector<Edge> expected, actual;
          for (const auto& e : graph->out (q))
            expected.emplace_back (g[e.dst], e.cond.id (), e.acc);
          for (const auto& e : renamed->out (g[q]))
            actual.emplace_back (e.dst, e.cond.id (), e.acc);
          std::sort (expected.begin (), expected.end ());
          std::sort (actual.begin (), actual.end ());
          if (expected != actual)
            return false;
        }
        return true;
      }

      bool simulates (StateId p, StateId q) const {
        const auto& qs = simulation_.simulators[p];
        return std::binary_search (qs.begin (), qs.end (), q);
      }
      bool covers (StateId p, int64_t v, StateId q, int64_t w) const {
        // q simulates p and carries at least its past accepting count. Resolve
        // mutual simulation at equal counts by retaining the smaller ID; a
        // finite preorder always retains a maximal representative this way.
        // Called only for q in simulators[p].
        return w >= v && (w > v || q < p || !simulates (q, p));
      }

    public:
      std::string active;
      size_t capped_states = 0;
      FrozenRankOracles (spot_rows::FrozenAcacia view, int K, bdd inputs, bdd outputs) {
        const char* env = std::getenv ("ACACIA_FWD_ORACLE");
        std::string_view selection = env ? env : "";
        while (!selection.empty ()) {
          const auto end = selection.find (',');
          const auto word = selection.substr (0, end);
          if (word == "cap")
            cap_ = true;
          else if (word == "sim")
            sim_ = true;
          else if (word == "sym")
            sym_ = true;
          else if (word == "all-on")
            cap_ = sim_ = sym_ = true;
          else
            throw std::invalid_argument ("unknown ACACIA_FWD_ORACLE selector");
          if (end == std::string_view::npos)
            break;
          selection.remove_prefix (end + 1);
        }
        for (auto [enabled, name] : {std::pair {cap_, "cap"}, {sim_, "sim"}, {sym_, "sym"}})
          if (enabled)
            active += (active.empty () ? "" : ",") + std::string (name);

        const auto& graph = view.graph;
        const unsigned n = graph->num_states ();
        if (cap_) {
          // cap_census counts FUTURE accepting visits; forward ranks count PAST
          // visits. Clamping a forward rank to that census would be unsound.
          // Instead compute the greatest h(q) with
          //   h(q) <= safe_cap(q), h(q) <= h(t)-increment(t) for every q->t.
          // h=-1 means no active count is universally safe. Thus v<=h(q)
          // can never violate any future safe cap, for ANY word, and its run
          // may be erased. This is K-1 minus the future-visit cap on a uniform
          // numeric graph, also accounting for the frozen Boolean cap of 0.
          // It preserves the exact K winner, including transient overflow.
          headroom_.resize (n);
          for (unsigned q = 0; q < n; ++q)
            headroom_[q] = q < view.bool_threshold ? K - 1 : 0;
          bool changed = true;
          while (changed) {
            changed = false;
            for (unsigned q = 0; q < n; ++q)
              for (const auto& e : graph->out (q)) {
                if (e.cond == bddfalse)
                  continue;
                const int h =
                    std::max (-1, headroom_[e.dst] - int (graph->state_is_accepting (e.dst)));
                if (h < headroom_[q]) {
                  headroom_[q] = h;
                  changed = true;
                }
              }
          }
          for (auto h : headroom_)
            capped_states += h >= 0;
        }
        if (sim_) {
          // Frozen rows increment on the DESTINATION, unlike SBA edge marks.
          // Reuse the existing simulation fixed point with those exact marks
          // and the Boolean safe-cap order at every related pair. If q
          // simulates p and r(q)>=r(p), every future run from p (and every
          // overflow) has a matching, at-least-as-dangerous run from q under
          // the SAME letters. Erasing p therefore preserves the K winner.
          auto marked = spot::make_twa_graph (graph, spot::twa::prop_set::all ());
          for (unsigned q = 0; q < n; ++q)
            for (auto& e : marked->out (q))
              e.acc = graph->state_is_accepting (e.dst) ? spot::acc_cond::mark_t {0}
                                                        : spot::acc_cond::mark_t {};
          marked->prop_state_acc (false);
          simulation_ = direct_simulation::compute (marked, n, view.bool_threshold);
        }
        if (sym_) {
          // Recheck every candidate at this boundary; do not trust the shipped
          // detector's I/O classification or structural verification.
          auto copy = spot::make_twa_graph (graph, spot::twa::prop_set::all ());
          auto candidates = symmetry::detect (copy, inputs, outputs);
          for (size_t i = 0; i < candidates.gens.size (); ++i) {
            const auto [a, c] = candidates.gen_pairs.at (i);
            spot::relabeling_map rename;
            for (const auto& [prefix, family] : candidates.families) {
              rename[family.idx2ap.at (a)] = family.idx2ap.at (c);
              rename[family.idx2ap.at (c)] = family.idx2ap.at (a);
            }
            if (verified_symmetry (view, inputs, outputs, candidates.gens[i], rename))
              generators_.push_back (std::move (candidates.gens[i]));
          }
        }
      }
      size_t simulation_pairs () const { return simulation_.strict_pairs; }
      size_t symmetry_generators () const { return generators_.size (); }
      bool reduces_levels () const { return cap_ || sim_; }

      // Operate on guarded contributions before either predicate layout is
      // built. Keeping guards symbolic avoids an exponential letter partition.
      template <typename Levels, typename Budget>
      void reduce_levels (Levels& levels, int K, Budget& b) const {
        if (!cap_ && !sim_)
          return;
        const auto raw = sim_ ? levels : Levels {};
        for (auto& [p, entries] : levels)
          for (auto& [level, guard] : entries) {
            b.step ();
            level = std::min (int64_t (K), level);
            if (cap_ && level <= headroom_[p]) {
              guard = bddfalse;
              continue;
            }
            if (sim_)
              for (auto q : simulation_.simulators[p]) {
                const auto found = raw.find (q);
                if (found == raw.end ())
                  continue;
                for (const auto& [w, condition] : found->second) {
                  b.step ();
                  if (covers (p, level, q, std::min (int64_t (K), w)))
                    guard = b.land (guard, b.negate (condition));
                }
              }
          }
        for (auto it = levels.begin (); it != levels.end ();) {
          std::erase_if (it->second, [] (const auto& e) { return e.second == bddfalse; });
          if (it->second.empty ())
            it = levels.erase (it);
          else
            ++it;
        }
      }
      Rank initial (StateId q, int K) const {
        // Every generator fixes init. A singleton has no simulation redundancy.
        return Rank {cap_ && headroom_[q] >= 0 ? std::vector<Rank::Entry> {}
                                               : std::vector<Rank::Entry> {{q, 0}},
                     K};
      }
      template <typename Budget>
      std::vector<Rank> orbit (const Rank& r, int K, Budget& b) const {
        std::vector<Rank> result {r};
        if (generators_.empty ())
          return result;
        std::unordered_set<Rank> seen {r};
        for (size_t i = 0; i < result.size (); ++i)
          for (const auto& g : generators_) {
            b.step ();
            std::vector<Rank::Entry> entries;
            for (auto [q, v] : result[i].entries ()) {
              b.step ();
              entries.emplace_back (g.at (q), v);
            }
            Rank next {std::move (entries), K};
            if (seen.insert (next).second)
              result.push_back (std::move (next));
          }
        return result;
      }
      template <typename Budget>
      std::vector<Rank> lift (const Rank& r, int K, Budget& b) const {
        auto result = orbit (r, K, b);
        if (!reduces_levels ())
          return result;
        for (auto& rank : result) {
          std::vector<Rank::Entry> entries;
          for (StateId p = 0; p < std::max (headroom_.size (), simulation_.simulators.size ());
               ++p) {
            b.step ();
            int value = rank.at (p);
            // Restore every universally safe count and every count covered by
            // a retained simulator. This is only an invariant candidate: the
            // verifier checks its safety and closure without any reductions.
            if (cap_)
              value = std::max (value, headroom_[p]);
            if (sim_)
              for (auto q : simulation_.simulators[p]) {
                b.step ();
                value = std::max (value, int (rank.at (q)));
              }
            if (value >= 0)
              entries.emplace_back (p, value);
          }
          rank = Rank {std::move (entries), K};
        }
        return result;
      }
      template <typename Budget>
      Rank canonical (const Rank& r, int K, Budget& b) const {
        const auto ranks = orbit (r, K, b);
        return *std::min_element (ranks.begin (), ranks.end (), [] (const Rank& a, const Rank& c) {
          return a.entries () < c.entries ();
        });
      }
  };
}  // namespace acacia::spot_lazy_game
#endif
