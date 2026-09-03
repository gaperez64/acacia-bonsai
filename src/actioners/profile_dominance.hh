#pragma once

// Why this dominance is sound: for an endpoint with source q, destination p,
// and increment i, actioners::standard::apply's backward step imposes
// apply_out[q] = min (apply_out[q], max (-1, m[p] - i)).  More endpoints and
// larger increments therefore impose more, tighter constraints, producing a
// pointwise smaller result vector and hence a smaller downset.  In
// k_bounded_safety_aut.hh, cpre_inplace takes a union over the actions of one
// input.  An action whose image is pointwise below another action's image adds
// nothing to that union and may be dropped.  Thus the redundant action is the
// one with MORE/STRONGER endpoints.

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <limits>
#include <list>
#include <optional>
#include <utility>
#include <vector>

namespace actioners::profile_dominance {

  using action = std::vector<std::pair<unsigned, bool>>;
  using action_vec = std::vector<action>;
  using action_vecs = std::list<action_vec>;

  struct endpoint {
      unsigned source;
      bool increment;

      friend bool operator== (const endpoint&, const endpoint&) = default;
  };

  using normalized_action = std::vector<std::vector<endpoint>>;

  /// Sort each destination's endpoints by source and merge parallel entries.
  /// A parallel accepting endpoint subsumes a non-accepting one, so merging
  /// retains the maximum increment.
  inline normalized_action normalize (const action_vec& value) {
    normalized_action result (value.size ());
    for (std::size_t destination = 0; destination < value.size (); ++destination) {
      auto& normalized = result[destination];
      normalized.reserve (value[destination].size ());
      for (const auto& [source, increment] : value[destination])
        normalized.push_back ({source, increment});

      std::sort (normalized.begin (), normalized.end (),
                 [] (const endpoint& left, const endpoint& right) {
                   return left.source < right.source;
                 });

      std::size_t out = 0;
      for (const endpoint current : normalized) {
        if (out != 0 and normalized[out - 1].source == current.source)
          normalized[out - 1].increment =
              normalized[out - 1].increment or current.increment;
        else
          normalized[out++] = current;
      }
      normalized.resize (out);
    }
    return result;
  }

  /// Whether a is redundant in the presence of b.  Missing destinations are
  /// empty lists; destinations present only in a are harmless extra
  /// constraints.
  inline bool worse_or_equal (const normalized_action& a, const normalized_action& b) {
    static const std::vector<endpoint> empty;
    for (std::size_t destination = 0; destination < b.size (); ++destination) {
      const auto& a_endpoints = destination < a.size () ? a[destination] : empty;
      const auto& b_endpoints = b[destination];
      std::size_t a_index = 0;
      for (const endpoint requirement : b_endpoints) {
        while (a_index < a_endpoints.size ()
               and a_endpoints[a_index].source < requirement.source)
          ++a_index;
        if (a_index == a_endpoints.size ()
            or a_endpoints[a_index].source != requirement.source
            or a_endpoints[a_index].increment < requirement.increment)
          return false;
        ++a_index;
      }
    }
    return true;
  }

  struct budget {
      std::size_t max_pair_tests = 100000;
      std::size_t max_endpoint_visits = 1000000;
      std::chrono::steady_clock::duration max_duration = std::chrono::milliseconds (250);
  };

  struct stats {
      std::size_t actions_before = 0;
      std::size_t actions_after = 0;
      std::size_t pair_tests = 0;
      std::size_t endpoint_visits = 0;
      bool declined = false;
      double elapsed_ms = 0.0;
  };

  namespace detail {

    enum class comparison { no, yes, budget_exhausted };

    struct budget_state {
        const budget& limits;
        stats& measurements;
        std::chrono::steady_clock::time_point started;

        [[nodiscard]] bool over_time () const {
          return std::chrono::steady_clock::now () - started >= limits.max_duration;
        }

        [[nodiscard]] bool begin_pair_test () {
          if (measurements.pair_tests >= limits.max_pair_tests or over_time ())
            return false;
          ++measurements.pair_tests;
          return true;
        }

        [[nodiscard]] bool visit_endpoint () {
          if (measurements.endpoint_visits >= limits.max_endpoint_visits or over_time ())
            return false;
          ++measurements.endpoint_visits;
          return true;
        }
    };

    inline comparison worse_or_equal (const normalized_action& a,
                                      const normalized_action& b,
                                      budget_state& state) {
      if (not state.begin_pair_test ())
        return comparison::budget_exhausted;

      static const std::vector<endpoint> empty;
      for (std::size_t destination = 0; destination < b.size (); ++destination) {
        const auto& a_endpoints = destination < a.size () ? a[destination] : empty;
        const auto& b_endpoints = b[destination];
        std::size_t a_index = 0;
        for (const endpoint requirement : b_endpoints) {
          if (not state.visit_endpoint ())
            return comparison::budget_exhausted;
          while (a_index < a_endpoints.size ()
                 and a_endpoints[a_index].source < requirement.source) {
            if (not state.visit_endpoint ())
              return comparison::budget_exhausted;
            ++a_index;
          }
          if (a_index == a_endpoints.size ())
            return comparison::no;
          if (not state.visit_endpoint ())
            return comparison::budget_exhausted;
          if (a_endpoints[a_index].source != requirement.source
              or a_endpoints[a_index].increment < requirement.increment)
            return comparison::no;
          ++a_index;
        }
      }
      return comparison::yes;
    }

  }  // namespace detail

  /// Stably remove redundant actions.  On budget exhaustion, only positions
  /// for which a completed comparison has already proved redundancy are
  /// removed; the current candidate and every unvisited action are retained.
  inline stats prune (action_vecs& actions, const budget& limits = {}) {
    using clock = std::chrono::steady_clock;
    const auto started = clock::now ();

    stats result;
    result.actions_before = actions.size ();

    struct indexed_action {
        std::size_t position;
        action_vecs::iterator iterator;
        std::optional<normalized_action> normalized;
    };

    std::vector<indexed_action> indexed;
    indexed.reserve (actions.size ());
    std::size_t position = 0;
    for (auto iterator = actions.begin (); iterator != actions.end (); ++iterator)
      indexed.push_back ({position++, iterator, std::nullopt});

    std::vector<bool> keep (indexed.size (), true);
    std::vector<std::size_t> survivors;
    survivors.reserve (indexed.size ());
    detail::budget_state state {limits, result, started};

    for (std::size_t candidate = 0; candidate < indexed.size (); ++candidate) {
      if (state.over_time ()) {
        result.declined = true;
        break;
      }
      indexed[candidate].normalized = normalize (*indexed[candidate].iterator);
      if (state.over_time ()) {
        result.declined = true;
        break;
      }

      bool candidate_is_dominated = false;
      for (const std::size_t survivor : survivors) {
        const auto verdict = detail::worse_or_equal (*indexed[candidate].normalized,
                                                     *indexed[survivor].normalized, state);
        if (verdict == detail::comparison::budget_exhausted) {
          result.declined = true;
          break;
        }
        if (verdict == detail::comparison::yes) {
          keep[candidate] = false;
          candidate_is_dominated = true;
          break;
        }
      }
      if (result.declined)
        break;
      if (candidate_is_dominated)
        continue;

      std::vector<std::size_t> updated_survivors;
      updated_survivors.reserve (survivors.size () + 1);
      for (std::size_t survivor_index = 0; survivor_index < survivors.size ();
           ++survivor_index) {
        const std::size_t survivor = survivors[survivor_index];
        const auto verdict = detail::worse_or_equal (*indexed[survivor].normalized,
                                                     *indexed[candidate].normalized, state);
        if (verdict == detail::comparison::budget_exhausted) {
          result.declined = true;
          updated_survivors.insert (updated_survivors.end (),
                                    survivors.begin ()
                                        + static_cast<std::ptrdiff_t> (survivor_index),
                                    survivors.end ());
          break;
        }
        if (verdict == detail::comparison::yes)
          keep[survivor] = false;
        else
          updated_survivors.push_back (survivor);
      }
      if (result.declined)
        break;
      updated_survivors.push_back (candidate);
      survivors = std::move (updated_survivors);
    }

    std::vector<std::size_t> surviving_positions;
    surviving_positions.reserve (indexed.size ());
    for (std::size_t i = 0; i < keep.size (); ++i)
      if (keep[i])
        surviving_positions.push_back (indexed[i].position);
    std::sort (surviving_positions.begin (), surviving_positions.end ());

    action_vecs filtered;
    for (const std::size_t survivor : surviving_positions)
      filtered.splice (filtered.end (), actions, indexed[survivor].iterator);
    actions.swap (filtered);

    result.actions_after = actions.size ();
    result.elapsed_ms =
        std::chrono::duration<double, std::milli> (clock::now () - started).count ();
    return result;
  }

}  // namespace actioners::profile_dominance
