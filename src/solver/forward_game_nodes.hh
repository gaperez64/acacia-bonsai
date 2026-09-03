#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace acacia::solver_detail {

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
      std::size_t losing_proof_id = 0;
  };

  template <typename State>
  struct forward_ctrl_node {
      std::size_t parent_env;
      std::size_t input_index;
      node_status status = node_status::open;
      std::size_t next_action_index = 0;
      std::optional<std::size_t> selected_env;
      std::optional<std::size_t> selected_action_index;
      std::vector<std::size_t> tried_env_ids;
      std::size_t losing_proof_id = 0;
  };

}  // namespace acacia::solver_detail
