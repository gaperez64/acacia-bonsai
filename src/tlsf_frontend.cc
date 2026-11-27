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

    std::vector<symmetry::declared_family> declared_families (tlsf::Conversion& result) {
      std::vector<symmetry::declared_family> families;
      families.reserve (result.indexed_families.size ());
      auto* bus = result.value_origins ? result.value_origins->buses : nullptr;
      for (auto& family : result.indexed_families) {
        families.push_back ({not family.is_output, family.lo, family.hi,
                             std::move (family.members), symmetry::declared_family::kind::element});
        if (bus) {
          families.back ().value_origin = {result.value_origins, bus};
          bus = bus->next;
        }
      }
      return families;
    }

  }  // namespace

  specification parse (std::string_view text, bool capture_value_origins) {
    record_parse_call ();
    const std::string owned {text};
    // Keep source INFO while lowering directly to Acacia's Mealy frame.
    auto result = tlsf::convert (owned, "Mealy", capture_value_origins);

    specification value;
    value.metadata.source_format = "tlsf";
    value.metadata.tlsf_semantics = result.semantics;
    value.metadata.tlsf_target = result.original_target;
    value.metadata.tlsf_effective_target = result.effective_target;
    value.metadata.tlsf_gr_level = result.gr_level;
    value.metadata.tlsf_declared_families = declared_families (result);
    value.metadata.tlsf_has_indexed_conjunction = result.has_indexed_conjunction;

    value.ast = std::move (result.ast);
    value.value_origins = std::move (result.value_origins);
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
