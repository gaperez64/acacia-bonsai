#include "tlsf_frontend.hh"

#include <unordered_set>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <tlsf/decompose.hpp>
#include <utility>

namespace acacia::tlsf_frontend {

  namespace {
    bool finite_semantics (const std::string& semantics) {
      return semantics.find ("Finite") != std::string::npos;
    }

    tlsf::Options options () {
      tlsf::Options value;
      value.precheck = false; // Conversion consumes formulas, not precheck verdicts.
      value.format = tlsf::Format::Ltlxba;
      // Match the checked-in ltlxba/.part identifier convention.  TLSF
      // semantics remain specification-defined.
      value.lowercase = true;
      return value;
    }

    bool has_indexed_conjunction (std::string_view text) {
      size_t at = 0;
      while ((at = text.find ("&&", at)) != std::string_view::npos) {
        at += 2;
        while (at < text.size () and std::isspace (static_cast<unsigned char> (text[at])))
          ++at;
        if (at < text.size () and text[at] == '[')
          return true;
      }
      return false;
    }

    void stable_signal_order (std::vector<std::string>& signals,
                              const std::vector<tlsf::IndexedFamily>& families, bool outputs) {
      std::unordered_set<std::string_view> bus_members;
      for (const auto& family : families)
        if (family.is_output == outputs)
          bus_members.insert (family.members.begin (), family.members.end ());

      std::vector<std::string> ordered;
      ordered.reserve (signals.size ());
      for (auto it = signals.rbegin (); it != signals.rend (); ++it)
        if (not bus_members.contains (*it))
          ordered.push_back (std::move (*it));
      for (auto family = families.rbegin (); family != families.rend (); ++family)
        if (family->is_output == outputs and not family->is_enum)
          ordered.insert (ordered.end (), family->members.begin (), family->members.end ());
      for (auto family = families.rbegin (); family != families.rend (); ++family)
        if (family->is_output == outputs and family->is_enum)
          ordered.insert (ordered.end (), family->members.begin (), family->members.end ());
      signals = std::move (ordered);
    }
  }  // namespace

  normalization_policy parse_normalization_policy (std::string_view policy) {
    if (policy == "off")
      return normalization_policy::off;
    if (policy == "match-safe")
      return normalization_policy::match_safe;
    if (policy == "route-safe")
      return normalization_policy::route_safe;
    throw std::invalid_argument ("normalization expects off, match-safe, or route-safe");
  }

  void report_normalization (const specification_metadata& metadata, const char* route) {
    // Canonical schedules contain only pass names, commas, colons and digits.
    std::fprintf (stderr,
                  "{\"stage\":\"tlsf_normalization\",\"route\":\"%s\",\"schedule\":\"%s\","
                  "\"changed\":%s,\"bounds_rejected\":%s,\"nodes_before\":%llu,"
                  "\"nodes_after\":%llu,\"time_ns\":%llu}\n",
                  route, metadata.tlsf_normalization_schedule.c_str (),
                  metadata.tlsf_normalization_changed ? "true" : "false",
                  metadata.tlsf_normalization_bounds_rejected ? "true" : "false",
                  (unsigned long long) metadata.tlsf_normalization_nodes_before,
                  (unsigned long long) metadata.tlsf_normalization_nodes_after,
                  (unsigned long long) metadata.tlsf_normalization_time_ns);
  }

  specification parse (std::string_view text, normalization_policy policy) {
    const std::string owned {text};
    tlsf::Options conversion_options = options ();
    tlsf::Result result = tlsf::decompose (owned, conversion_options);
    if (finite_semantics (result.semantics))
      throw std::runtime_error ("finite TLSF semantics are not supported");

    // Acacia solves in the Mealy frame.  Preserve the requested target for
    // synthesis metadata, but ask the linked frontend for an effective Mealy
    // formula so solving and controller conversion share one internal path.
    const std::string source_target = result.target;
    if (source_target != "Mealy" || policy != normalization_policy::off) {
      conversion_options.overwrite_target = "Mealy";
      if (policy != normalization_policy::off) {
        conversion_options.normalization_schedule =
            policy == normalization_policy::match_safe ? "match-safe:1" : "route-safe:1";
        conversion_options.normalization_phase =
            policy == normalization_policy::match_safe ? TLSF_NORM_PHASE_MATCH : TLSF_NORM_PHASE_ROUTE;
        conversion_options.normalization_max_growth_percent = 200;
        conversion_options.normalization_max_nodes = 20000;
      }
      result = tlsf::decompose (owned, conversion_options);
    }

    stable_signal_order (result.inputs, result.indexed_families, false);
    stable_signal_order (result.outputs, result.indexed_families, true);

    specification value;
    value.metadata.source_format = "tlsf";
    value.metadata.tlsf_semantics = result.semantics;
    value.metadata.tlsf_target = source_target;
    value.metadata.tlsf_effective_target = result.target;
    value.metadata.tlsf_gr_level = result.gr_level;
    if (policy != normalization_policy::off) {
      value.metadata.tlsf_normalization_schedule = std::move (result.normalization_schedule);
      value.metadata.tlsf_normalization_nodes_before = result.normalization_nodes_before;
      value.metadata.tlsf_normalization_nodes_after = result.normalization_nodes_after;
      value.metadata.tlsf_normalization_time_ns = result.normalization_time_ns;
      value.metadata.tlsf_normalization_changed = result.normalization_changed;
      value.metadata.tlsf_normalization_bounds_rejected = result.normalization_bounds_rejected;
    }
    if (has_indexed_conjunction (text)) {
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

  specification load (const std::string& path, normalization_policy policy) {
    std::ifstream input {path};
    if (not input)
      throw std::runtime_error ("unable to open TLSF file: " + path);
    std::ostringstream contents;
    contents << input.rdbuf ();
    try {
      return parse (contents.str (), policy);
    } catch (const std::exception& error) {
      throw std::runtime_error ("unable to convert TLSF file " + path + ": " + error.what ());
    }
  }

}  // namespace acacia::tlsf_frontend
