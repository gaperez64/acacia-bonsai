#include "tlsf_frontend.hh"

#include "phase_records.hh"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <tlsf/convert.hpp>
#include <utility>

namespace acacia::tlsf_frontend {

  namespace {
    void record_parse_call () noexcept {
      if (!phase_records_enabled ())
        return;
      static pid_t owner = -1;
      static unsigned calls = 0;
      const auto pid = getpid ();
      if (owner != pid) {
        owner = pid;
        calls = 0;
      }
      const auto* worker = active_worker_record ();
      char line[256];
      const int length =
          snprintf (line, sizeof line,
                    "{\"event\":\"tlsf_parse\",\"pid\":%ld,\"role\":\"%s\",\"call\":%u,"
                    "\"mono_ns\":%llu}\n",
                    long (pid), worker ? worker->requested_backend : "parent", ++calls,
                    (unsigned long long) phase_clock (CLOCK_MONOTONIC));
      if (length > 0 && size_t (length) < sizeof line)
        phase_records_send (line, size_t (length));
      else
        phase_records_drop ();
    }

  }  // namespace

  specification parse (std::string_view text) {
    record_parse_call ();
    const std::string owned {text};
    // Keep source INFO while lowering directly to Acacia's Mealy frame.
    auto result = tlsf::convert (owned, "Mealy");

    specification value;
    value.metadata.source_format = "tlsf";
    value.metadata.tlsf_semantics = result.semantics;
    value.metadata.tlsf_target = result.original_target;
    value.metadata.tlsf_effective_target = result.effective_target;
    value.metadata.tlsf_gr_level = result.gr_level;
    if (result.has_indexed_conjunction) {
      value.metadata.tlsf_indexed_families.reserve (result.indexed_families.size ());
      for (auto& family : result.indexed_families) {
        symmetry::indexed_family_hint hint;
        hint.is_input = not family.is_output;
        hint.lo = family.lo;
        hint.hi = family.hi;
        hint.members = std::move (family.members);
        value.metadata.tlsf_indexed_families.push_back (std::move (hint));
      }
    }

    value.ast = std::move (result.ast);
    value.formula = std::move (result.preprocessed_ltl);
    value.metadata.tlsf_normalized_objective = value.formula;
    value.inputs = std::move (result.inputs);
    value.outputs = std::move (result.outputs);
    // Stable serialization convention only; TLSF does not define a total AP
    // order.  Scalars precede ordinary and enum buses, source declarations are
    // reversed within each group, and bus members retain ascending index order.
    // Indexed-family provenance uses the same member order.
    if (value.formula.empty ())
      throw std::runtime_error ("TLSF conversion produced an empty formula");
    return value;
  }

  specification load (const std::string& path) {
    std::ifstream input {path};
    if (not input)
      throw std::runtime_error ("unable to open TLSF file: " + path);
    std::ostringstream contents;
    contents << input.rdbuf ();
    try {
      return parse (contents.str ());
    } catch (const std::exception& error) {
      throw std::runtime_error ("unable to convert TLSF file " + path + ": " + error.what ());
    }
  }

}  // namespace acacia::tlsf_frontend
