#pragma once

#include "symmetry_certificate.hh"

#include <chrono>
#include <cstdint>
#include <spot/tl/formula.hh>

namespace acacia::tlsf_frontend {
  struct specification;
}

namespace symmetry {

  // Covers the checked expanded code map and action on the converted objective.
  // Source-level selector intent, including absence of wraparound, is outside its scope.
  struct encoded_certificate {
      struct bus {
          bool is_input;
          // Code bit j is members[j]. Selectors and aliases follow axis order.
          std::vector<spot::formula> members, selectors;
          std::vector<size_t> codes;
          std::vector<std::optional<size_t>> aliases;
          std::vector<std::vector<size_t>> unused_classes;
      };
      struct slot {
          size_t bus;
          bool next;
          auto operator<=> (const slot&) const = default;
      };
      struct predicate {
          spot::formula atom;
          // Mixed radix, first slot least significant. Each radix is the full code space.
          std::vector<slot> slots;
          std::vector<bool> truth;
      };
      struct swap {
          size_t bus, first, second;
      };
      using generator = std::vector<swap>;

      std::string objective;
      std::vector<std::string> inputs, outputs;
      std::vector<int64_t> coordinates;                     // moving first, then fixed
      size_t moving = 0;
      std::vector<bus> buses;
      std::vector<predicate> predicates;
      // Consume only actions witnessed by products of these checked involutions.
      std::vector<generator> generators;
      spot::formula abstract;
  };

  // Run in a bounded worker. Map abnormal worker exits to decline.
  [[nodiscard]] std::shared_ptr<const encoded_certificate> check_encoded_family (
      const acacia::tlsf_frontend::specification& spec, size_t work,
      std::chrono::steady_clock::time_point deadline);

  [[nodiscard]] bool certify_encoded_family (acacia::tlsf_frontend::specification& spec,
                                             size_t work,
                                             std::chrono::steady_clock::time_point deadline);

}  // namespace symmetry
