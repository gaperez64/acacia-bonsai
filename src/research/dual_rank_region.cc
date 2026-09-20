#include "research/dual_rank_region.hh"

#include <algorithm>
#include <bit>
#include <stdexcept>
#include <utility>

namespace acacia::research {
  namespace {

    constexpr std::size_t saturated_add (std::size_t a, std::size_t b) {
      return b > std::numeric_limits<std::size_t>::max () - a
                 ? std::numeric_limits<std::size_t>::max ()
                 : a + b;
    }

    constexpr std::size_t saturated_multiply (std::size_t a, std::size_t b) {
      return a != 0 and b > std::numeric_limits<std::size_t>::max () / a
                 ? std::numeric_limits<std::size_t>::max ()
                 : a * b;
    }

    bool vector_less (const rank_vector& left, const rank_vector& right) {
      return std::lexicographical_compare (left.begin (), left.end (), right.begin (),
                                           right.end ());
    }

    std::size_t domain_accounted_bytes (const rank_domain& domain) {
      std::size_t bytes = sizeof (rank_domain);
      bytes = saturated_add (bytes,
                             saturated_multiply (domain.lower.capacity (), sizeof (VECTOR_ELT_T)));
      bytes = saturated_add (bytes,
                             saturated_multiply (domain.upper.capacity (), sizeof (VECTOR_ELT_T)));
      return saturated_add (bytes, domain.identity.capacity ());
    }

    bool same_vector (const rank_vector& left, const rank_vector& right) {
      return left.size () == right.size () and
             std::equal (left.begin (), left.end (), right.begin ());
    }

    bool charged_leq (const rank_vector& left, const rank_vector& right, operation_budget& budget,
                      bool& answer) {
      if (left.size () != right.size ()) {
        budget.invalidate ("rank-vector width mismatch");
        return false;
      }
      answer = true;
      for (std::size_t i = 0; i < left.size (); ++i) {
        if (not budget.charge ())
          return false;
        if (left[i] > right[i]) {
          answer = false;
          break;
        }
      }
      return true;
    }

    bool observe (operation_budget& budget, const dual_rank_region* source,
                  const std::vector<rank_vector>& first,
                  const std::vector<rank_vector>* second = nullptr,
                  const rank_vector* candidate = nullptr, std::size_t retained_bytes = 0,
                  std::size_t retained_generators = 0) {
      std::size_t bytes = retained_bytes;
      std::size_t live = retained_generators;
      if (source != nullptr) {
        bytes = saturated_add (bytes, source->stats ().accounted_bytes);
        live = saturated_add (live, source->generators ().size ());
      }
      bytes = saturated_add (bytes, accounted_bytes (first));
      live = saturated_add (live, first.size ());
      if (second != nullptr) {
        bytes = saturated_add (bytes, accounted_bytes (*second));
        live = saturated_add (live, second->size ());
      }
      if (candidate != nullptr) {
        bytes = saturated_add (bytes, sizeof (rank_vector));
        bytes = saturated_add (bytes,
                               saturated_multiply (candidate->capacity (), sizeof (VECTOR_ELT_T)));
        live = saturated_add (live, 1);
      }
      return budget.observe_workspace (bytes, live);
    }

    /// Insert a generator and retain either the minimal or maximal antichain.
    bool insert_canonical (std::vector<rank_vector>& frontier, rank_vector candidate,
                           frontier_form form, operation_budget& budget,
                           const dual_rank_region* source = nullptr,
                           const std::vector<rank_vector>* other = nullptr,
                           std::size_t retained_bytes = 0, std::size_t retained_generators = 0) {
      for (const auto& stored : frontier) {
        bool dominated = false;
        if (form == frontier_form::min_excluded) {
          if (not charged_leq (stored, candidate, budget, dominated))
            return false;
        }
        else if (not charged_leq (candidate, stored, budget, dominated))
          return false;
        if (dominated)
          return true;
      }

      std::size_t write = 0;
      for (std::size_t read = 0; read < frontier.size (); ++read) {
        bool remove = false;
        if (form == frontier_form::min_excluded) {
          if (not charged_leq (candidate, frontier[read], budget, remove))
            return false;
        }
        else if (not charged_leq (frontier[read], candidate, budget, remove))
          return false;
        if (remove)
          continue;
        if (write != read)
          frontier[write] = std::move (frontier[read]);
        ++write;
      }
      frontier.erase (frontier.begin () + static_cast<std::ptrdiff_t> (write), frontier.end ());
      frontier.push_back (std::move (candidate));
      return observe (budget, source, frontier, other, nullptr, retained_bytes,
                      retained_generators);
    }

    bool charge_sort (operation_budget& budget, std::size_t count) {
      if (count < 2)
        return budget.poll ();
      const auto width = static_cast<std::uint64_t> (std::bit_width (count - 1));
      if (count > std::numeric_limits<std::uint64_t>::max () / width)
        return budget.charge (std::numeric_limits<std::uint64_t>::max ());
      return budget.charge (static_cast<std::uint64_t> (count) * width);
    }

    std::optional<std::vector<rank_vector>> normalize (
        const rank_domain& domain, frontier_form form, std::vector<rank_vector> input,
        operation_budget& budget, const dual_rank_region* source = nullptr,
        std::size_t retained_bytes = 0, std::size_t retained_generators = 0) {
      if (not observe (budget, source, input, nullptr, nullptr, retained_bytes,
                       retained_generators))
        return std::nullopt;
      for (const auto& generator : input)
        if (not domain.contains_point (generator)) {
          budget.invalidate ("frontier generator lies outside its rank domain");
          return std::nullopt;
        }

      if (not charge_sort (budget, input.size ()))
        return std::nullopt;
      std::sort (input.begin (), input.end (), vector_less);
      input.erase (std::unique (input.begin (), input.end (), same_vector), input.end ());

      std::vector<rank_vector> normalized;
      for (const auto& generator : input)
        if (not insert_canonical (normalized, rank_vector (generator), form, budget, source,
                                  &input, retained_bytes, retained_generators))
          return std::nullopt;
      if (not charge_sort (budget, normalized.size ()))
        return std::nullopt;
      std::sort (normalized.begin (), normalized.end (), vector_less);
      if (not observe (budget, source, normalized, nullptr, nullptr, retained_bytes,
                       retained_generators))
        return std::nullopt;
      return normalized;
    }

    region_result failure (const operation_budget& budget) {
      return {budget.outcome (), std::nullopt, budget.message ()};
    }

    region_result incompatible (operation_budget& budget, const std::string& message) {
      budget.invalidate (message);
      return failure (budget);
    }

    rank_vector coordinate_join (const rank_vector& left, const rank_vector& right) {
      rank_vector out (left);
      for (std::size_t i = 0; i < out.size (); ++i)
        out[i] = std::max (out[i], right[i]);
      return out;
    }

    rank_vector coordinate_meet (const rank_vector& left, const rank_vector& right) {
      rank_vector out (left);
      for (std::size_t i = 0; i < out.size (); ++i)
        out[i] = std::min (out[i], right[i]);
      return out;
    }

    region_result combine (const dual_rank_region& left, const dual_rank_region& right,
                           frontier_form form, bool make_union, operation_budget& budget) {
      if (not left.domain ().compatible (right.domain ()))
        return incompatible (budget, "cannot combine incompatible rank domains");
      if (left.form () != form or right.form () != form)
        return incompatible (budget,
                             "same-form algebra requires explicit conversion of both operands");

      std::vector<rank_vector> out;
      const auto& lg = left.generators ();
      const auto& rg = right.generators ();
      const bool simple_union = (make_union and form == frontier_form::max_included) or
                                (not make_union and form == frontier_form::min_excluded);

      if (simple_union) {
        for (const auto* side : {&lg, &rg})
          for (const auto& generator : *side)
            if (not insert_canonical (out, rank_vector (generator), form, budget, &left, &rg))
              return failure (budget);
      }
      else {
        // MaxIncluded intersection uses meets; MinExcluded union uses joins.
        // Empty products implement the empty/full identities without special
        // sentinels.
        for (const auto& a : lg)
          for (const auto& b : rg) {
            if (not budget.charge (a.size ()))
              return failure (budget);
            rank_vector candidate = form == frontier_form::max_included ? coordinate_meet (a, b)
                                                                        : coordinate_join (a, b);
            if (not insert_canonical (out, std::move (candidate), form, budget, &left, &rg))
              return failure (budget);
          }
      }
      if (not charge_sort (budget, out.size ()))
        return failure (budget);
      std::sort (out.begin (), out.end (), vector_less);
      const region_stats left_stats = left.stats ();
      const region_stats right_stats = right.stats ();
      return try_make_region (
          left.domain (), form, std::move (out), budget,
          saturated_add (left_stats.accounted_bytes, right_stats.accounted_bytes),
          saturated_add (left_stats.generators, right_stats.generators));
    }

  }  // namespace

  const char* completion_name (completion value) {
    switch (value) {
      case completion::complete: return "complete";
      case completion::work_limit: return "work_limit";
      case completion::time_limit: return "time_limit";
      case completion::frontier_limit: return "frontier_limit";
      case completion::memory_limit: return "memory_limit";
      case completion::invalid_input: return "invalid_input";
    }
    return "invalid_input";
  }

  const char* frontier_form_name (frontier_form value) {
    return value == frontier_form::max_included ? "positive" : "negative";
  }

  operation_budget::operation_budget (budget_limits limits)
    : limits_ (limits),
      started_ (std::chrono::steady_clock::now ()) {}

  void operation_budget::stop (completion why, std::string message) {
    if (outcome_ != completion::complete)
      return;
    outcome_ = why;
    message_ = std::move (message);
  }

  bool operation_budget::poll () {
    if (not ok ())
      return false;
    if (limits_.deadline.count () > 0 and
        std::chrono::steady_clock::now () - started_ >= limits_.deadline)
      stop (completion::time_limit, "operation deadline exceeded");
    return ok ();
  }

  bool operation_budget::charge (std::uint64_t amount) {
    if (not poll ())
      return false;
    if (amount > limits_.max_work - std::min (work_, limits_.max_work)) {
      work_ = limits_.max_work;
      stop (completion::work_limit, "operation work limit exceeded");
      return false;
    }
    work_ += amount;
    return poll ();
  }

  bool operation_budget::observe_workspace (std::size_t bytes, std::size_t live_generators) {
    if (not poll ())
      return false;
    bytes = saturated_add (bytes, external_workspace_bytes_);
    live_generators = saturated_add (live_generators, external_live_generators_);
    peak_workspace_bytes_ = std::max (peak_workspace_bytes_, bytes);
    peak_live_generators_ = std::max (peak_live_generators_, live_generators);
    if (live_generators > limits_.max_frontier) {
      stop (completion::frontier_limit, "live-generator limit exceeded");
      return false;
    }
    if (bytes > limits_.max_workspace_bytes) {
      stop (completion::memory_limit, "accounted workspace limit exceeded");
      return false;
    }
    return true;
  }

  void operation_budget::set_external_workspace (std::size_t bytes, std::size_t generators) {
    external_workspace_bytes_ = bytes;
    external_live_generators_ = generators;
  }

  void operation_budget::invalidate (std::string message) {
    stop (completion::invalid_input, std::move (message));
  }

  std::chrono::microseconds operation_budget::elapsed () const {
    return std::chrono::duration_cast<std::chrono::microseconds> (
        std::chrono::steady_clock::now () - started_);
  }

  rank_domain rank_domain::fixed_box (std::size_t dimensions, int bound, std::size_t boolean_split,
                                      std::string stable_identity) {
    rank_domain result;
    result.lower.assign (dimensions, static_cast<VECTOR_ELT_T> (-1));
    result.upper.assign (dimensions, static_cast<VECTOR_ELT_T> (0));
    result.k = bound;
    result.bool_threshold = boolean_split;
    result.identity = std::move (stable_identity);
    if (bound >= 1 and bound <= std::numeric_limits<VECTOR_ELT_T>::max ())
      for (std::size_t i = 0; i < std::min (boolean_split, dimensions); ++i)
        result.upper[i] = static_cast<VECTOR_ELT_T> (bound - 1);
    std::string reason;
    if (not result.valid (&reason))
      throw std::invalid_argument (reason);
    return result;
  }

  bool rank_domain::valid (std::string* reason) const {
    auto reject = [&] (const char* message) {
      if (reason != nullptr)
        *reason = message;
      return false;
    };
    if (lower.size () != upper.size ())
      return reject ("rank domain has mismatched bound widths");
    if (k < 1 or k > std::numeric_limits<VECTOR_ELT_T>::max ())
      return reject ("rank domain K is outside the supported 1..127 range");
    if (bool_threshold > dimensions ())
      return reject ("rank domain Boolean split exceeds its dimension");
    if (identity.empty ())
      return reject ("rank domain identity is empty");
    for (std::size_t i = 0; i < dimensions (); ++i) {
      if (lower[i] != static_cast<VECTOR_ELT_T> (-1))
        return reject ("rank domain lower bound is not -1");
      const auto expected = static_cast<VECTOR_ELT_T> (i < bool_threshold ? k - 1 : 0);
      if (upper[i] != expected)
        return reject ("rank domain upper bound disagrees with K/Boolean split");
    }
    return true;
  }

  bool rank_domain::contains_point (const rank_vector& point) const {
    if (point.size () != dimensions ())
      return false;
    for (std::size_t i = 0; i < point.size (); ++i)
      if (point[i] < lower[i] or point[i] > upper[i])
        return false;
    return true;
  }

  bool rank_domain::compatible (const rank_domain& other) const {
    return k == other.k and bool_threshold == other.bool_threshold and
           identity == other.identity and lower == other.lower and upper == other.upper;
  }

  rank_vector rank_domain::bottom () const { return rank_vector (lower.begin (), lower.end ()); }

  rank_vector rank_domain::top () const { return rank_vector (upper.begin (), upper.end ()); }

  dual_rank_region::dual_rank_region (rank_domain domain, frontier_form form,
                                      std::vector<rank_vector> generators)
    : domain_ (std::move (domain)),
      form_ (form),
      generators_ (std::move (generators)) {}

  region_stats dual_rank_region::stats () const {
    std::size_t bytes = sizeof (*this);
    bytes = saturated_add (bytes,
                           saturated_multiply (domain_.lower.capacity (), sizeof (VECTOR_ELT_T)));
    bytes = saturated_add (bytes,
                           saturated_multiply (domain_.upper.capacity (), sizeof (VECTOR_ELT_T)));
    bytes = saturated_add (bytes, domain_.identity.capacity ());
    bytes = saturated_add (bytes, accounted_bytes (generators_));
    return {generators_.size (), bytes};
  }

  bool dual_rank_region::contains (const rank_vector& point) const {
    if (not domain_.contains_point (point))
      return false;
    if (form_ == frontier_form::max_included) {
      for (const auto& maximum : generators_)
        if (leq (point, maximum))
          return true;
      return false;
    }
    for (const auto& exclusion : generators_)
      if (leq (exclusion, point))
        return false;
    return true;
  }

  std::uint64_t dual_rank_region::semantic_hash () const {
    std::uint64_t hash = 1469598103934665603ULL;
    auto mix = [&] (std::uint8_t value) {
      hash ^= value;
      hash *= 1099511628211ULL;
    };
    mix (static_cast<std::uint8_t> (form_));
    for (const auto& generator : generators_) {
      mix (0xff);
      for (const auto coordinate : generator)
        mix (static_cast<std::uint8_t> (coordinate));
    }
    return hash;
  }

  std::size_t accounted_bytes (const std::vector<rank_vector>& vectors) {
    std::size_t bytes = saturated_multiply (vectors.capacity (), sizeof (rank_vector));
    for (const auto& vector : vectors)
      bytes =
          saturated_add (bytes, saturated_multiply (vector.capacity (), sizeof (VECTOR_ELT_T)));
    return bytes;
  }

  region_result try_make_region (rank_domain domain, frontier_form form,
                                 std::vector<rank_vector> generators, operation_budget& budget,
                                 std::size_t retained_bytes, std::size_t retained_generators) {
    std::string reason;
    if (not domain.valid (&reason))
      return incompatible (budget, reason);
    retained_bytes = saturated_add (retained_bytes, domain_accounted_bytes (domain));
    auto normalized = normalize (domain, form, std::move (generators), budget, nullptr,
                                 retained_bytes, retained_generators);
    if (not normalized)
      return failure (budget);
    return {completion::complete,
            dual_rank_region (std::move (domain), form, std::move (*normalized)),
            {}};
  }

  region_result try_convert (const dual_rank_region& source, frontier_form target,
                             operation_budget& budget) {
    if (source.form () == target) {
      std::vector<rank_vector> copy = source.generators ();
      if (not observe (budget, &source, copy))
        return failure (budget);
      return {
          completion::complete, dual_rank_region (source.domain (), target, std::move (copy)), {}};
    }

    const rank_domain& domain = source.domain ();
    std::vector<rank_vector> current;
    current.push_back (target == frontier_form::min_excluded ? domain.bottom () : domain.top ());
    if (not observe (budget, &source, current))
      return failure (budget);

    for (const auto& blocker : source.generators ()) {
      std::vector<rank_vector> next;
      for (const auto& generator : current) {
        bool blocked = false;
        if (target == frontier_form::min_excluded) {
          if (not charged_leq (generator, blocker, budget, blocked))
            return failure (budget);
        }
        else if (not charged_leq (blocker, generator, budget, blocked))
          return failure (budget);

        if (not blocked) {
          if (not insert_canonical (next, rank_vector (generator), target, budget, &source,
                                    &current))
            return failure (budget);
          continue;
        }

        for (std::size_t coordinate = 0; coordinate < domain.dimensions (); ++coordinate) {
          if (not budget.charge ())
            return failure (budget);
          const bool can_step = target == frontier_form::min_excluded
                                    ? blocker[coordinate] < domain.upper[coordinate]
                                    : blocker[coordinate] > domain.lower[coordinate];
          if (not can_step)
            continue;
          rank_vector candidate (generator);
          const int stepped = static_cast<int> (blocker[coordinate]) +
                              (target == frontier_form::min_excluded ? 1 : -1);
          candidate[coordinate] = static_cast<VECTOR_ELT_T> (stepped);
          if (not insert_canonical (next, std::move (candidate), target, budget, &source,
                                    &current))
            return failure (budget);
        }
      }
      current = std::move (next);
      if (not observe (budget, &source, current))
        return failure (budget);
    }

    if (not charge_sort (budget, current.size ()))
      return failure (budget);
    std::sort (current.begin (), current.end (), vector_less);
    return {completion::complete, dual_rank_region (domain, target, std::move (current)), {}};
  }

  region_result try_union (const dual_rank_region& left, const dual_rank_region& right,
                           frontier_form result_form, operation_budget& budget) {
    return combine (left, right, result_form, true, budget);
  }

  region_result try_intersection (const dual_rank_region& left, const dual_rank_region& right,
                                  frontier_form result_form, operation_budget& budget) {
    return combine (left, right, result_form, false, budget);
  }

  std::optional<bool> exact_equal (const dual_rank_region& left, const dual_rank_region& right,
                                   operation_budget& budget) {
    if (not left.domain ().compatible (right.domain ())) {
      budget.invalidate ("cannot compare incompatible rank domains");
      return std::nullopt;
    }
    const dual_rank_region* comparable = &left;
    std::optional<dual_rank_region> converted;
    if (left.form () != right.form ()) {
      region_result result = try_convert (left, right.form (), budget);
      if (not result)
        return std::nullopt;
      converted = std::move (*result.region);
      comparable = &*converted;
    }
    const auto& lg = comparable->generators ();
    const auto& rg = right.generators ();
    if (lg.size () != rg.size ())
      return false;
    for (std::size_t i = 0; i < lg.size (); ++i) {
      if (not budget.charge (lg[i].size ()))
        return std::nullopt;
      if (not same_vector (lg[i], rg[i]))
        return false;
    }
    return true;
  }

}  // namespace acacia::research
