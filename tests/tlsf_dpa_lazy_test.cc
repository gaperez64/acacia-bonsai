#include "solver/parity/tlsf_dpa.hh"

#include <iostream>
#include <stdexcept>

namespace {
  std::vector<std::vector<TlsfDpaEdge>> rows;
  TlsfDpaStatus status;
  size_t fail_row = SIZE_MAX;
  const TlsfDpaLabelNode nodes[] = {{TLSF_DPA_TRUE, 0, 0, 0},
                                  {TLSF_DPA_FALSE, 0, 0, 0},
                                  {TLSF_DPA_DECISION, 0, 1, 0},
                                  {TLSF_DPA_DECISION, 0, 0, 1}};

  void require (bool condition, const char* message) {
    if (!condition)
      throw std::runtime_error (message);
  }

  void reset (std::vector<std::vector<TlsfDpaEdge>> graph) {
    rows = std::move (graph);
    status = {TLSF_DPA_OK, TLSF_DPA_NONE};
    fail_row = SIZE_MAX;
  }
}

// A deliberately small independent DPA source, with stable IDs and complete
// symbolic rows. Wrapping leaves the production importer/solver unmodified.
extern "C" TlsfDpaStatus __wrap_tlsf_dpa_status (const TlsfDpa*) { return status; }
extern "C" TlsfDpaState __wrap_tlsf_dpa_initial (const TlsfDpa*) { return 0; }
extern "C" uint32_t __wrap_tlsf_dpa_ap_count (const TlsfDpa*) { return 1; }
extern "C" const char* __wrap_tlsf_dpa_ap_name (const TlsfDpa*, uint32_t) { return "x"; }
extern "C" uint32_t __wrap_tlsf_dpa_max_priority (const TlsfDpa*) { return 100001; }
extern "C" const TlsfDpaLabelNode* __wrap_tlsf_dpa_label_node (const TlsfDpa*,
                                                            TlsfDpaLabel label) {
  return label < std::size (nodes) ? &nodes[label] : nullptr;
}
extern "C" TlsfDpaStatus __wrap_tlsf_dpa_successors (TlsfDpa*, TlsfDpaState state,
                                                    const TlsfDpaEdge** edges, size_t* count) {
  *edges = nullptr;
  *count = 0;
  if (status.code != TLSF_DPA_OK)
    return status;
  if (state == fail_row)
    return status = {TLSF_DPA_DECLINED, TLSF_DPA_HOST_LIMIT};
  require (state < rows.size (), "invalid fixture target");
  *edges = rows[state].data ();
  *count = rows[state].size ();
  return status;
}

int main () {
  try {
    // No dereference is performed by this fake API; only its opaque identity
    // is passed to the wrapped functions.
    char identity;
    auto& source = *reinterpret_cast<TlsfDpa*> (&identity);
    acacia::tlsf_dpa::limits limits;
    const auto solve = [&] (bool real, const std::vector<std::string>& outputs = {}) {
      return acacia::tlsf_dpa::solve_lazy (source, limits, outputs, real);
    };
    for (bool real : {false, true}) {
      const uint32_t terminal = real ? 100001 : 100000;
      for (bool target : {false, true}) {
        reset ({{{0, 1, 3}}, {{0, 2, 9}}, {{0, 2, terminal}}});
        const auto answer = solve (target);
        require (answer.status.code == TLSF_DPA_OK && answer.decided == (target == real) &&
                     answer.expanded == 3 && answer.solves == 3,
                 "frontier trap or late high opposite-parity priority changed the winner");
      }
      // A closed winning cone exists after expanding only states 0 and 1.
      // The opponent can choose any output/input, including the frontier.
      reset ({{{2, 1, 2}, {3, 2, 2}}, {{0, 1, terminal}}, {{0, 2, terminal ^ 1u}}});
      const auto early = solve (real, real ? std::vector<std::string> {"x"}
                                         : std::vector<std::string> {});
      require (early.decided && early.expanded == 2 && early.states == 3,
               "closed strategy cone failed to decide with frontier remaining");
    }
    // Both previously observed 3 and 9 are odd. Inserting even 6 later
    // requires remapping the older 9 as well. All three edges recur: retaining
    // its old compressed 1 below the newly inserted 2 would flip this winner.
    for (bool target : {false, true}) {
      reset ({{{0, 1, 9}}, {{0, 2, 3}}, {{0, 0, 6}}});
      const auto recurring = solve (target);
      require (recurring.status.code == TLSF_DPA_OK && recurring.decided == target &&
                   recurring.expanded == 3,
               "late inserted parity did not remap all stored recurring edges");
    }
    reset ({{{0, 1, 3}}, {{2, 1, 3}, {3, 2, 4}}, {{0, 2, 4}}});
    fail_row = 1;
    const auto failed = solve (false);
    require (!failed.decided && failed.expanded == 1 && failed.solves == 1 &&
                 failed.status.reason == TLSF_DPA_HOST_LIMIT,
             "a failed row revived an earlier optimistic snapshot");
    require (!solve (false).decided, "permanently declined source was revived");
    // The first edge alone would close a REAL strategy; the overlapping last
    // edge invalidates the full row, so no snapshot may be solved.
    reset ({{{0, 0, 3}, {0, 0, 4}}});
    const auto overlap = solve (true);
    require (!overlap.decided && !overlap.expanded && !overlap.solves &&
                 overlap.status.code == TLSF_DPA_INVALID,
             "partially imported row produced a verdict");
    reset ({{{2, 0, 3}}});
    const auto incomplete = solve (true);
    require (!incomplete.decided && !incomplete.solves && !incomplete.expanded &&
                 incomplete.status.code == TLSF_DPA_INVALID,
             "incomplete row produced a verdict");
    reset ({{{0, 0, 3}}});
    const auto declined_after_solve = acacia::tlsf_dpa::solve_lazy (
        source, limits, {}, true,
        [&] (const auto&) { status = {TLSF_DPA_DECLINED, TLSF_DPA_CANCELLED}; });
    require (!declined_after_solve.decided && declined_after_solve.solves == 1 &&
                 declined_after_solve.status.reason == TLSF_DPA_CANCELLED,
             "source decline after solving failed to suppress the verdict");
    std::cout << "Lazy frontier, full-row publication, stable priorities and declines passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
}
