#include "tlsf_frontend.hh"

#include "phase_records.hh"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <tlsf/convert.hpp>
#include <tlsf/symmetry.h>
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
    const std::unique_ptr<TlsfConversionResult, decltype (&tlsf_conversion_result_free)> result {
        tlsf_convert (owned.c_str (), "Mealy"), tlsf_conversion_result_free};
    if (not result)
      throw std::runtime_error ("tlsf conversion failed");

    specification value;
    value.metadata.source_format = "tlsf";
    value.metadata.tlsf_semantics = result->semantics;
    value.metadata.tlsf_target = result->original_target;
    value.metadata.tlsf_effective_target = result->effective_target;
    value.metadata.tlsf_gr_level = result->gr_level;
    const auto family_hint = [&] (size_t i) {
      const auto& family = result->indexed_families[i];
      return symmetry::indexed_family_hint {
          not family.is_output, family.lo, family.hi,
          tlsf::detail::copy_strings (family.members, family.n_members)};
    };
    if (result->has_indexed_conjunction) {
      value.metadata.tlsf_indexed_families.reserve (result->n_indexed_families);
      for (size_t i = 0; i < result->n_indexed_families; ++i)
        value.metadata.tlsf_indexed_families.push_back (family_hint (i));
    }

    TlsfSymmetryResult* detected = nullptr;
    const auto status = tlsf_symmetry_detect (result.get (), &detected);
    const std::unique_ptr<TlsfSymmetryResult, decltype (&tlsf_symmetry_result_free)> evidence {
        detected, tlsf_symmetry_result_free};
    if (status == TLSF_SYMMETRY_NO_MEMORY)
      throw std::bad_alloc ();
    if (status != TLSF_SYMMETRY_OK)
      throw std::runtime_error ("tlsf symmetry detection failed");

    value.inputs = tlsf::detail::copy_strings (result->inputs, result->n_inputs);
    value.outputs = tlsf::detail::copy_strings (result->outputs, result->n_outputs);
    auto aps = value.inputs;
    aps.insert (aps.end (), value.outputs.begin (), value.outputs.end ());
    for (size_t r = 0; r < evidence->n_ranges; ++r) {
      const auto& range = evidence->ranges[r];
      symmetry::objective_symmetry_hint hint;
      for (size_t f = 0; f < range.n_families; ++f)
        hint.families.push_back (family_hint (range.families[f]));
      for (size_t c = 0; c < range.n_candidates; ++c) {
        const auto& candidate = range.candidates[c];
        if (candidate.generator == SIZE_MAX)
          continue;
        const auto& generator = evidence->generators[candidate.generator];
        // The K-bounded block layout requires transpositions, not rotations.
        if (generator.kind != TLSF_SYMMETRY_ADJACENT)
          continue;
        symmetry::ap_transposition_candidate gen;
        std::vector<long> moved;
        for (size_t i = 0; i < size_t (range.hi - range.lo) + 1; ++i)
          if (candidate.image[i] != range.lo + i)
            moved.push_back (range.lo + i);
        gen.indices = {moved[0], moved[1]};
        for (size_t i = 0; i < aps.size (); ++i)
          if (generator.image[i] != i)
            gen.image.emplace_back (aps[i], aps[generator.image[i]]);
        hint.generators.push_back (std::move (gen));
      }
      if (not hint.generators.empty ())
        value.metadata.tlsf_symmetries.push_back (std::move (hint));
    }

    value.ast = std::shared_ptr<TlsfAst> (std::exchange (result->ast, nullptr), tlsf_ast_free);
    value.formula = result->preprocessed_ltl;
    value.metadata.tlsf_normalized_objective = value.formula;
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
