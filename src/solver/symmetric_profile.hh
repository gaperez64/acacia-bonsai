#pragma once

#include "configuration.hh"
#include "utils/verbose.hh"

#include <array>
#include <chrono>
#include <cstdint>

namespace acacia::solver_detail::symmetric::profile {

  enum class bucket : unsigned {
    detect,
    block_layout,
    representative_io,
    output_representatives,
    actioner_build,
    pre_for_input,
    realize,
    action_apply,
    count_conversion,
    dominance_union,
    intersect,
    k_increment_union,
    solve_total,
    classic_backward_apply,
    classic_pre_build,
    classic_intersect,
    classic_solve_total,
    equivariant_ap_scan,
    equivariant_detect,
    equivariant_block_layout,
    equivariant_generator_match,
    equivariant_orbit_build,
    equivariant_output_enumerate,
    equivariant_action_dedup,
    equivariant_solve_loop,
    count
  };

  inline constexpr unsigned bucket_count = (unsigned) bucket::count;

  inline const char* name (bucket b) {
    switch (b) {
      case bucket::detect: return "detect";
      case bucket::block_layout: return "block_layout";
      case bucket::representative_io: return "representative_io";
      case bucket::output_representatives: return "output_representatives";
      case bucket::actioner_build: return "actioner_build";
      case bucket::pre_for_input: return "pre_for_input";
      case bucket::realize: return "realize";
      case bucket::action_apply: return "action_apply";
      case bucket::count_conversion: return "count_conversion";
      case bucket::dominance_union: return "dominance_union";
      case bucket::intersect: return "intersect";
      case bucket::k_increment_union: return "k_increment_union";
      case bucket::solve_total: return "solve_total";
      case bucket::classic_backward_apply: return "classic_backward_apply";
      case bucket::classic_pre_build: return "classic_pre_build";
      case bucket::classic_intersect: return "classic_intersect";
      case bucket::classic_solve_total: return "classic_solve_total";
      case bucket::equivariant_ap_scan: return "equivariant_ap_scan";
      case bucket::equivariant_detect: return "equivariant_detect";
      case bucket::equivariant_block_layout: return "equivariant_block_layout";
      case bucket::equivariant_generator_match: return "equivariant_generator_match";
      case bucket::equivariant_orbit_build: return "equivariant_orbit_build";
      case bucket::equivariant_output_enumerate: return "equivariant_output_enumerate";
      case bucket::equivariant_action_dedup: return "equivariant_action_dedup";
      case bucket::equivariant_solve_loop: return "equivariant_solve_loop";
      case bucket::count: break;
    }
    return "unknown";
  }

  struct counters {
      std::array<std::uint64_t, bucket_count> ns {};
      std::array<std::uint64_t, bucket_count> calls {};

      void add (bucket b, std::uint64_t elapsed_ns) {
        const auto idx = (unsigned) b;
        ns[idx] += elapsed_ns;
        calls[idx] += 1;
      }

      void reset () {
        ns.fill (0);
        calls.fill (0);
      }

      void report () const {
#if ACACIA_SYMMETRY_PROFILE
        utils::vout << "[symmetry][profile]";
        bool any = false;
        for (unsigned i = 0; i < bucket_count; ++i) {
          if (calls[i] == 0)
            continue;
          any = true;
          utils::vout << " " << name ((bucket) i) << "=" << (ns[i] / 1000000.0)
                      << "ms/" << calls[i];
        }
        if (not any)
          utils::vout << " empty";
        utils::vout << "\n";
#endif
      }
  };

  inline counters& global () {
    static counters c;
    return c;
  }

  struct scope {
      bucket b;
      std::chrono::steady_clock::time_point start;

      explicit scope (bucket b_)
        : b (b_), start (std::chrono::steady_clock::now ()) {}

      ~scope () {
#if ACACIA_SYMMETRY_PROFILE
        const auto end = std::chrono::steady_clock::now ();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds> (end - start).count ();
        global ().add (b, (std::uint64_t) elapsed);
#endif
      }
  };

}  // namespace acacia::solver_detail::symmetric::profile

#if ACACIA_SYMMETRY_PROFILE
# define ACACIA_SYMMETRY_PROFILE_CONCAT_INNER(a, b) a##b
# define ACACIA_SYMMETRY_PROFILE_CONCAT(a, b) ACACIA_SYMMETRY_PROFILE_CONCAT_INNER (a, b)
# define ACACIA_SYMMETRY_PROFILE_SCOPE(name) \
  acacia::solver_detail::symmetric::profile::scope \
      ACACIA_SYMMETRY_PROFILE_CONCAT (acacia_symmetry_profile_scope_, __LINE__) { \
          acacia::solver_detail::symmetric::profile::bucket::name}
#else
# define ACACIA_SYMMETRY_PROFILE_SCOPE(name) ((void) 0)
#endif
