// Adapted from vanessas-oink 872be945, src/solvers/strpm_simd.{hpp,cpp}
// (Strahler-tree progress measures by V. Flügel and G. A. Pérez), relicensed by its
// authors under this project's GPL-3.0.

#include "game.hh"
#include "lanes.hh"

#include <algorithm>
#include <bit>
#include <chrono>
#include <deque>
#include <numeric>
#include <queue>
#include <set>
#include <stdexcept>

namespace acacia::parity {
  namespace {
    struct ratio_compare {
        bool operator() (const std::pair<unsigned, unsigned>& a,
                         const std::pair<unsigned, unsigned>& b) const {
          // Deliberately preserve the reference's integer ratio ordering.
          return std::max (a.first, a.second) / std::min (a.first, a.second) >
                 std::max (b.first, b.second) / std::min (b.first, b.second);
        }
    };

    class search {
        const game& g;
        limits bounds;
        solution result;
        detail::lifting measure;
        std::vector<detail::node_measure> pm;
        std::vector<unsigned> order;
        size_t remaining;

        bool active (unsigned v) const { return result.winner[v] == 2; }
        bool top (unsigned v) const {
          return pm[v].nlanes and pm[v].levels[0] == detail::TOP_SENTINEL;
        }

        bool lift (unsigned v, unsigned player, unsigned& strategy) {
          if (top (v))
            return false;
          const auto pr = g.priority[v];
          const int pindex = measure.h - 2 - (player == 0 ? (pr + 1) / 2 : pr / 2);
          const bool progress = player == (pr & 1);
          const bool maximum = g.owner[v] == player;
          bool first = true;
          for (unsigned to : g.out (v)) {
            if (not active (to))
              continue;
            measure.to_tmp (pm[to]);
            if (progress)
              measure.prog_tmp (pindex, measure.h);
            if (first or (maximum ? measure.compare (pindex) > 0 : measure.compare (pindex) < 0)) {
              measure.tmp_to_best ();
              strategy = to;
              first = false;
            }
          }
          if (first)
            throw std::runtime_error ("remaining parity game has a dead end");
          measure.to_tmp (pm[v]);
          if (measure.compare (pindex) < 0) {
            measure.from_best (pm[v]);
            return true;
          }
          return false;
        }

        void attract (const std::vector<unsigned>& dominion, unsigned winner) {
          std::vector<size_t> degree (g.size ());
          for (unsigned v = 0; v < g.size (); ++v)
            if (active (v))
              for (unsigned to : g.out (v))
                degree[v] += active (to);
          std::deque<unsigned> queue;
          for (unsigned v : dominion) {
            result.winner[v] = winner;
            --remaining;
            queue.push_back (v);
          }
          while (not queue.empty ()) {
            const auto to = queue.front ();
            queue.pop_front ();
            for (unsigned v : g.in (to)) {
              if (not active (v))
                continue;
              if (g.owner[v] == winner or --degree[v] == 0) {
                result.winner[v] = winner;
                if (g.owner[v] == winner)
                  result.strategy[v] = to;
                --remaining;
                queue.push_back (v);
              }
            }
          }
        }

        bool run (unsigned k, unsigned t, unsigned depth, unsigned player) {
          measure.t = t;
          measure.h = depth + 1;  // The reference's extra level is required by the encoding.
          // The two players can have different heights. A tree cannot have more
          // nonempty levels than its height; clipping k preserves a sound partial solve.
          measure.k = std::min (k, depth + 1);
          detail::node_measure init;
          init.nlanes = measure.k - 1;
          if (init.nlanes)
            init.masks[0] = (1u << (t + 1)) - 1;
          for (int i = 1; i < measure.k - 1; ++i) {
            init.levels[i] = i;
            init.masks[i] = 1;
          }
          pm.assign (g.size (), init);
          std::deque<unsigned> queue;
          std::vector<bool> queued (g.size ());
          for (unsigned v : order)
            if (active (v)) {
              queue.push_back (v);
              queued[v] = true;
            }
          while (not queue.empty ()) {
            if (bounds.lift_budget and result.stats.lift_attempts >= bounds.lift_budget)
              return false;
            unsigned v = queue.front ();
            queue.pop_front ();
            queued[v] = false;
            ++result.stats.lift_attempts;
            unsigned strategy = no_vertex;
            if (lift (v, player, strategy))
              for (unsigned from : g.in (v))
                if (active (from) and not queued[from]) {
                  queue.push_back (from);
                  queued[from] = true;
                }
          }
          std::vector<unsigned> dominion;
          for (unsigned v = 0; v < g.size (); ++v) {
            if (not active (v) or top (v))
              continue;
            if (g.owner[v] != player) {
              // Recompute the minimizing successor at the fixed point.
              if (lift (v, player, result.strategy[v]))
                throw std::runtime_error ("progress measures did not reach a fixed point");
            }
            dominion.push_back (v);
          }
          attract (dominion, 1 - player);
          return true;
        }

        void fallback (const char* reason) {
          result.stats.fallback = true;
          result.stats.fallback_reason = reason;
          result.stats.fallback_vertices = remaining;
          if (bounds.fallback_observer)
            bounds.fallback_observer (result.stats, -1);
          const auto start = std::chrono::steady_clock::now ();
          std::vector<unsigned> original, local (g.size (), no_vertex);
          std::vector<uint8_t> owners;
          std::vector<unsigned> priorities;
          for (unsigned v = 0; v < g.size (); ++v)
            if (active (v)) {
              local[v] = original.size ();
              original.push_back (v);
              owners.push_back (g.owner[v]);
              priorities.push_back (g.priority[v]);
            }
          std::vector<std::vector<unsigned>> edges (original.size ());
          for (unsigned i = 0; i < original.size (); ++i)
            for (unsigned to : g.out (original[i]))
              if (active (to))
                edges[i].push_back (local[to]);
          game rest {std::move (owners), std::move (priorities), edges};
          auto solved = solve_spot (rest, to_spot (rest));
          for (unsigned i = 0; i < original.size (); ++i) {
            const auto v = original[i];
            result.winner[v] = solved.winner[i];
            if (g.owner[v] == solved.winner[i])
              result.strategy[v] = original.at (solved.strategy[i]);
          }
          remaining = 0;
          result.stats.fallback_seconds =
              std::chrono::duration<double> (std::chrono::steady_clock::now () - start).count ();
          if (bounds.fallback_observer)
            bounds.fallback_observer (result.stats, result.stats.fallback_seconds);
        }

      public:
        search (const game& input, limits budget)
          : g (input),
            bounds (budget),
            remaining (g.size ()) {
          result.winner.assign (g.size (), 2);
          result.strategy.assign (g.size (), no_vertex);
          order.resize (g.size ());
          std::iota (order.begin (), order.end (), 0);
          std::stable_sort (order.begin (), order.end (), [&] (unsigned a, unsigned b) {
            return g.priority[a] > g.priority[b];
          });
        }
        solution solve () {
          const auto max_prio = g.priority[order.front ()];
          const unsigned t_bound = std::bit_width (g.size ()) - 1;
          const unsigned h0 = max_prio / 2 + 1;
          const unsigned h1 = (uint64_t (max_prio) + 1) / 2;
          const unsigned h_bound = std::max (h0, h1);
          const unsigned k_bound = std::min (t_bound + 2, h_bound);
          // Reference representability guard: 8 lanes, 8-bit strings, uint16 levels.
          const unsigned t_max = std::min (t_bound, 7u);
          const unsigned k_max = std::min (k_bound, 9u);
          if (h_bound >= detail::TOP_SENTINEL or bounds.initial_k > 9 or bounds.initial_t > 7) {
            fallback ("representation limit");
            return std::move (result);
          }
          if (not bounds.initial_k or not bounds.initial_t)
            throw std::runtime_error ("initial tree parameters must be positive");
          std::priority_queue<std::pair<unsigned, unsigned>,
                              std::vector<std::pair<unsigned, unsigned>>, ratio_compare>
              queue;
          std::set<std::pair<unsigned, unsigned>> tried;
          const auto initial = std::pair {bounds.initial_k, bounds.initial_t};
          queue.push (initial);
          tried.insert (initial);
          while (not queue.empty () and remaining) {
            const auto [k, t] = queue.top ();
            queue.pop ();
            result.stats.k = k;
            result.stats.t = t;
            ++result.stats.pairs;
            if (bounds.parameter_observer)
              bounds.parameter_observer (result.stats);
            // Odd counters first, as ODDFIRST=1 in the pinned source.
            if (not run (k, t, h1, 1) or (remaining and not run (k, t, h0, 0))) {
              fallback ("operation budget");
              break;
            }
            if (not remaining)
              break;
            for (const auto& next : {std::pair {k + 1, t}, std::pair {k, t + 1}})
              if (next.first <= k_max and next.second <= t_max and tried.insert (next).second)
                queue.push (next);
          }
          if (remaining)
            fallback ("representation search exhausted");
          return std::move (result);
        }
    };
  }
  solution solve_strpm (const game& g, limits bounds) { return search (g, bounds).solve (); }
}
