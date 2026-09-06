#pragma once

#include "configuration.hh"

#include <cstddef>
#include <deque>
#include <optional>
#include <vector>

namespace acacia::solver_detail {

  enum class forward_result_status { win_k, lose_k, resource_limit, unknown };

  inline const char* forward_result_name (forward_result_status status) {
    switch (status) {
      case forward_result_status::win_k: return "WIN_K";
      case forward_result_status::lose_k: return "LOSE_K";
      case forward_result_status::resource_limit: return "RESOURCE_LIMIT";
      case forward_result_status::unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
  }

  enum class losing_reason {
    env_unsafe,
    env_subsumed,
    env_losing_input,
    ctrl_all_losing,
  };

  struct losing_proof {
      std::size_t id;
      losing_reason reason;
      std::size_t node;
      std::size_t witness = 0;
      std::vector<std::size_t> dependencies;
  };

  template <typename Event> using forward_work_queue = std::deque<Event>;

  enum class node_status { open, expanded, losing };

  template <typename State>
  struct successor_choice_for {
      State successor;
      std::size_t representative_action_index;
  };

  template <typename State>
  struct forward_env_node {
      State rank;
      node_status status = node_status::open;
      std::vector<std::size_t> controller_ids;  ///< One child per input class.
      /// Append-only reverse selections; old entries may be stale after a switch.
      std::vector<std::size_t> selected_by;
#if ACACIA_FORWARD_CONDITIONAL_COVERING
      /// Controllers conditionally using this rank as a downward cover.
      std::vector<std::size_t> covered_by;
#endif
      std::size_t losing_proof_id = 0;
  };

  template <typename State>
  struct forward_ctrl_node {
      std::size_t parent_env;
      std::size_t input_index;
      node_status status = node_status::open;
      std::size_t next_action_index = 0;
#if ACACIA_FORWARD_CONDITIONAL_COVERING
      /// The semantic image retained while `cover_env` stands in for it.
      std::optional<State> covered_actual_successor;
      std::optional<std::size_t> cover_env;
#endif
      std::optional<std::size_t> selected_env;
      std::optional<std::size_t> selected_action_index;
      std::vector<std::size_t> tried_env_ids;
      std::size_t losing_proof_id = 0;
  };

}  // namespace acacia::solver_detail
