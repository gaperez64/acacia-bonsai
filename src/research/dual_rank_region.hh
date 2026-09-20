#pragma once

/// Exact dual antichain representations of a downward-closed rank region.
///
/// This is research infrastructure: one object has one authoritative form and
/// every operation returns a fresh complete object or a typed failure.  Scratch
/// frontiers are never observable as regions.

#include "research/rank_action_replay.hh"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace acacia::research {

  enum class frontier_form { max_included, min_excluded };

  enum class completion {
    complete,
    work_limit,
    time_limit,
    frontier_limit,
    memory_limit,
    invalid_input,
  };

  [[nodiscard]] const char* completion_name (completion value);
  [[nodiscard]] const char* frontier_form_name (frontier_form value);

  struct budget_limits {
      std::uint64_t max_work = std::numeric_limits<std::uint64_t>::max ();
      std::size_t max_workspace_bytes = 256ULL * 1024ULL * 1024ULL;
      std::size_t max_frontier = 100000;
      std::chrono::milliseconds deadline {1000};
  };

  /// Cooperative budget shared by all nested phases of one exact operation.
  class operation_budget {
    public:
      explicit operation_budget (budget_limits limits = {});

      [[nodiscard]] bool charge (std::uint64_t amount = 1);
      [[nodiscard]] bool observe_workspace (std::size_t bytes, std::size_t live_generators);
      [[nodiscard]] bool poll ();
      void invalidate (std::string message);
      /// Account caller-owned storage retained across a nested phase.
      void set_external_workspace (std::size_t bytes, std::size_t generators);

      [[nodiscard]] bool ok () const { return outcome_ == completion::complete; }
      [[nodiscard]] completion outcome () const { return outcome_; }
      [[nodiscard]] std::uint64_t work () const { return work_; }
      [[nodiscard]] std::size_t peak_workspace_bytes () const { return peak_workspace_bytes_; }
      [[nodiscard]] std::size_t peak_live_generators () const { return peak_live_generators_; }
      [[nodiscard]] std::chrono::microseconds elapsed () const;
      [[nodiscard]] const std::string& message () const { return message_; }
      [[nodiscard]] const budget_limits& limits () const { return limits_; }

    private:
      budget_limits limits_;
      std::chrono::steady_clock::time_point started_;
      std::uint64_t work_ = 0;
      std::size_t peak_workspace_bytes_ = 0;
      std::size_t peak_live_generators_ = 0;
      std::size_t external_workspace_bytes_ = 0;
      std::size_t external_live_generators_ = 0;
      completion outcome_ = completion::complete;
      std::string message_;

      void stop (completion why, std::string message);
  };

  /// The finite product box and the identity of the transition semantics whose
  /// coordinates it describes.  Equal dimensions and bounds are insufficient:
  /// identity prevents accidental cross-automaton operations.
  struct rank_domain {
      std::vector<VECTOR_ELT_T> lower;
      std::vector<VECTOR_ELT_T> upper;
      int k = 0;
      std::size_t bool_threshold = 0;
      std::string identity;

      [[nodiscard]] static rank_domain fixed_box (std::size_t dimensions, int k,
                                                  std::size_t bool_threshold,
                                                  std::string identity);
      [[nodiscard]] std::size_t dimensions () const { return upper.size (); }
      [[nodiscard]] bool valid (std::string* reason = nullptr) const;
      [[nodiscard]] bool contains_point (const rank_vector& point) const;
      [[nodiscard]] bool compatible (const rank_domain& other) const;
      [[nodiscard]] rank_vector bottom () const;
      [[nodiscard]] rank_vector top () const;
  };

  struct region_stats {
      std::size_t generators = 0;
      std::size_t accounted_bytes = 0;
  };

  struct region_result;

  class dual_rank_region {
    public:
      [[nodiscard]] frontier_form form () const { return form_; }
      [[nodiscard]] const rank_domain& domain () const { return domain_; }
      [[nodiscard]] const std::vector<rank_vector>& generators () const { return generators_; }
      [[nodiscard]] region_stats stats () const;
      [[nodiscard]] bool contains (const rank_vector& point) const;
      [[nodiscard]] std::uint64_t semantic_hash () const;

    private:
      rank_domain domain_;
      frontier_form form_;
      std::vector<rank_vector> generators_;

      dual_rank_region (rank_domain domain, frontier_form form,
                        std::vector<rank_vector> generators);

      friend struct region_result;
      friend region_result try_make_region (rank_domain, frontier_form, std::vector<rank_vector>,
                                            operation_budget&, std::size_t, std::size_t);
      friend region_result try_convert (const dual_rank_region&, frontier_form, operation_budget&);
      friend region_result try_union (const dual_rank_region&, const dual_rank_region&,
                                      frontier_form, operation_budget&);
      friend region_result try_intersection (const dual_rank_region&, const dual_rank_region&,
                                             frontier_form, operation_budget&);
  };

  struct region_result {
      completion status = completion::invalid_input;
      std::optional<dual_rank_region> region;
      std::string message;

      [[nodiscard]] explicit operator bool () const {
        return status == completion::complete and region.has_value ();
      }
  };

  /// Validate, normalize and construct a complete authoritative region.
  [[nodiscard]] region_result try_make_region (rank_domain domain, frontier_form form,
                                               std::vector<rank_vector> generators,
                                               operation_budget& budget,
                                               std::size_t retained_bytes = 0,
                                               std::size_t retained_generators = 0);

  [[nodiscard]] region_result try_convert (const dual_rank_region& source, frontier_form target,
                                           operation_budget& budget);

  [[nodiscard]] region_result try_union (const dual_rank_region& left,
                                         const dual_rank_region& right, frontier_form result_form,
                                         operation_budget& budget);

  [[nodiscard]] region_result try_intersection (const dual_rank_region& left,
                                                const dual_rank_region& right,
                                                frontier_form result_form,
                                                operation_budget& budget);

  /// Exact equality.  Cross-form comparison performs a charged conversion.
  [[nodiscard]] std::optional<bool> exact_equal (const dual_rank_region& left,
                                                 const dual_rank_region& right,
                                                 operation_budget& budget);

  [[nodiscard]] std::size_t accounted_bytes (const std::vector<rank_vector>& vectors);

}  // namespace acacia::research
