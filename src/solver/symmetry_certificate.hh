#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace symmetry {

  struct encoded_certificate;
  struct logical_family {
      std::shared_ptr<const encoded_certificate> certificate;
      size_t bus;
  };

  // Physical declarations supplied by the native TLSF frontend.  Syntax-gated
  // hints do not prove symmetry; structural automorphism checking still does.
  struct declared_family {
      bool is_input = false;
      long lo = 0;
      long hi = -1;
      std::vector<std::string> members;
      enum class kind { element, opaque } encoding = kind::opaque;
      std::optional<logical_family> logical = std::nullopt;
  };

  using indexed_family_hint = declared_family;

}  // namespace symmetry
