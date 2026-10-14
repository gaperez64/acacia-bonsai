#pragma once

#include "actioners/standard.hh"
#include "research/cpre_event.hh"
#include <string_view>

#include <array>
#include <bit>
#include <chrono>
#include <limits>
#include <optional>
#include <stdexcept>

namespace acacia::research::downset_replay {

  enum class stage : size_t {
    construction,
    membership,
    image,
    combine,
    intersection,
    rebuild,
    destruction,
    count
  };

  struct costs {
      bool measured = false;
      std::array<double, static_cast<size_t> (stage::count)> ms {};
      double kernel_ms = 0;
      double total_ms = 0;
      double* enclosing_children = nullptr;
  };

  // Nested time belongs exclusively to the child. Disabled correctness runs
  // read no clocks. Internal library rebuilds remain charged to their caller.
  class timer {
      costs& result;
      stage operation;
      std::chrono::steady_clock::time_point start;
      double children = 0;
      double* parent = nullptr;

    public:
      timer (costs& result, stage operation) : result {result}, operation {operation} {
        if (result.measured) {
          start = std::chrono::steady_clock::now ();
          parent = result.enclosing_children;
          result.enclosing_children = &children;
        }
      }
      ~timer () {
        if (result.measured) {
          const double elapsed =
              std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now () - start)
                  .count ();
          result.ms[static_cast<size_t> (operation)] += elapsed - children;
          if (parent)
            *parent += elapsed;
          result.enclosing_children = parent;
        }
      }
  };

  using exact_rows = std::vector<std::vector<int>>;

  inline bool below (const std::vector<int>& a, const std::vector<int>& b) {
    for (size_t i = 0; i < a.size (); ++i)
      if (a[i] > b[i])
        return false;
    return true;
  }

  inline exact_rows maxima (exact_rows rows) {
    std::ranges::sort (rows);
    rows.erase (std::unique (rows.begin (), rows.end ()), rows.end ());
    exact_rows result;
    for (size_t i = 0; i < rows.size (); ++i) {
      bool dominated = false;
      for (size_t j = 0; j < rows.size (); ++j)
        if (i != j and below (rows[i], rows[j])) {
          dominated = true;
          break;
        }
      if (not dominated)
        result.push_back (rows[i]);
    }
    return result;
  }

  template <typename Range>
  exact_rows rows_of (const Range& region) {
    exact_rows rows;
    for (const auto& v : region) {
      std::vector<int> row (v.size ());
      for (size_t i = 0; i < v.size (); ++i)
        row[i] = v[i];
      rows.push_back (std::move (row));
    }
    std::ranges::sort (rows);
    return rows;
  }

  inline void validate (const event& ev, size_t numeric) {
    if (ev.states == 0 or numeric > ev.states or ev.k < 1 or
        ev.k > std::numeric_limits<VECTOR_ELT_T>::max ())
      throw std::invalid_argument ("invalid dimension, numeric boundary, or K");
    for (const auto* region : {&ev.before, &ev.after})
      for (const auto& row : *region) {
        if (row.size () != ev.states)
          throw std::invalid_argument ("wrong generator dimension");
        for (size_t i = 0; i < row.size (); ++i)
          if (row[i] < -1 or row[i] > (i < numeric ? ev.k - 1 : 0))
            throw std::invalid_argument ("generator outside safe numeric/Boolean domain");
      }
    for (const auto& action : ev.actions) {
      if (action.size () != ev.states)
        throw std::invalid_argument ("wrong action dimension");
      for (const auto& row : action)
        for (const auto& [source, increment] : row) {
          (void) increment;
          if (source >= ev.states)
            throw std::invalid_argument ("action endpoint outside domain");
        }
    }
  }

  template <class Downset>
  Downset construct (const std::vector<rank_vector>& rows, std::string_view backend) {
    if (rows.empty () and (backend == "kdtree_backed" or backend == "bboxtree_backed"))
      throw std::invalid_argument ("selected downset does not support empty generators");
    std::vector<typename Downset::value_type> values;
    values.reserve (rows.size ());
    for (const auto& row : rows)
      values.emplace_back (rank_vector (row));
    return Downset {std::move (values)};
  }

  struct replay_automaton {
      unsigned states;
      unsigned num_states () const { return states; }
      bool state_is_accepting (unsigned) const { return false; }
  };

  template <class Downset, int Mode = CPRE_AVOID_UNIONS>
  void update (Downset& f, const event& ev, costs& result, std::string_view backend) {
    static_assert (Mode == 0 or Mode == 1);
    using state = typename Downset::value_type;
    replay_automaton graph {static_cast<unsigned> (ev.states)};
    const auto* aut = &graph;
    std::list<std::pair<bdd, std::list<std::vector<std::pair<unsigned, unsigned>>>>> empty;
    // Exact production transformer and compact payload layout; event loading
    // and payload packing are deliberately outside the kernel timer.
    auto actioner = actioners::standard<state>::make (aut, empty, ev.k);
    std::vector<actioners::compact_payload> actions;
    for (const auto& action : ev.actions)
      actions.push_back (actioners::compact_payload::from_rows (action));

    const auto previous = result.ms;
    std::optional<Downset> predecessors;
    if constexpr (Mode == 0) {
      {
        timer timing {result, stage::construction};
        predecessors.emplace (state (rank_vector (ev.states, -1)));
      }
      bool first = true;
      for (const auto& action : actions) {
        std::optional<Downset> output;
        {
          timer timing {result, stage::construction};
          output.emplace (f.apply ([&] (const state& m) {
            timer image {result, stage::image};
            return actioner.apply (m, action, actioners::direction::backward);
          }));
        }
        {
          timer timing {result, first ? stage::destruction : stage::combine};
          if (first) {
            *predecessors = std::move (*output);
            first = false;
          }
          else
            predecessors->union_with (std::move (*output));
        }
        {
          timer timing {result, stage::destruction};
          output.reset ();
        }
      }
    }
    else {
      if (ev.actions.empty () and (backend == "kdtree_backed" or backend == "bboxtree_backed"))
        throw std::invalid_argument ("batched empty actions unsupported by selected downset");
      std::vector<state> images;
      {
        timer timing {result, stage::image};
        images.reserve (actions.size () * f.size ());
        for (const auto& action : actions)
          for (const auto& m : f)
            images.push_back (actioner.apply (m, action, actioners::direction::backward));
      }
      {
        timer timing {result, stage::combine};
        predecessors.emplace (std::move (images));
      }
    }
    {
      timer timing {result, stage::intersection};
      f.intersect_with (std::move (*predecessors));
    }
    {
      timer timing {result, stage::destruction};
      predecessors.reset ();
    }
    for (size_t i = 0; i < previous.size (); ++i)
      result.kernel_ms += result.ms[i] - previous[i];
  }

  // Logical retained-memory estimates, not allocator/RSS observations. See the
  // driver document for capacity/slack and temporary-overlap limitations.
  template <class Downset>
  size_t region_bytes (size_t n, size_t dimension, std::string_view backend) {
    using state = typename Downset::value_type;
    const size_t data = state::blocks_for (dimension) * sizeof (typename state::block_type);
    const size_t vector = sizeof (state) + data;
    size_t index = n * (sizeof (int64_t) + 3 * sizeof (size_t));
    if (backend == "kdtree_backed")
      index = 4 * std::bit_floor (n) * 16 + dimension * sizeof (std::optional<VECTOR_ELT_T>);
    else if (backend == "sharingtrie_backed")
      index =
          n * dimension * 16 + 2 * n * dimension * dimension * sizeof (unsigned) + dimension * 8;
    else if (backend == "bboxtree_backed") {
      const size_t nodes = n * 4 / 3 + 1;
      index = n * sizeof (long) + nodes * (2 * vector + 4 * sizeof (uint32_t)) +
              nodes * sizeof (uint32_t);
    }
    return sizeof (Downset) + n * vector + index;
  }

}  // namespace acacia::research::downset_replay
