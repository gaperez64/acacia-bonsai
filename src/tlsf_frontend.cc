#include "tlsf_frontend.hh"

#include <unordered_set>

#include <algorithm>
#include <cctype>
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
      value.format = tlsf::Format::Ltlxba;
      // Match SyFCo's ltlxba/.part convention.  This also makes native
      // formulas interchangeable with the checked-in converted corpus.
      value.lowercase = true;
      value.syfco_compatibility = true;
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

    void syfco_signal_order (std::vector<std::string>& signals,
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

  specification parse (std::string_view text) {
    const std::string owned {text};
    tlsf::Result result = tlsf::decompose (owned, options ());
    if (finite_semantics (result.semantics))
      throw std::runtime_error ("finite TLSF semantics are not supported");

    syfco_signal_order (result.inputs, result.indexed_families, false);
    syfco_signal_order (result.outputs, result.indexed_families, true);

    specification value;
    value.metadata.source_format = "tlsf";
    value.metadata.tlsf_semantics = result.semantics;
    value.metadata.tlsf_target = result.target;
    value.metadata.tlsf_effective_target = "Mealy";
    value.metadata.tlsf_gr_level = result.gr_level;
    if (has_indexed_conjunction (text)) {
      value.metadata.tlsf_indexed_families.reserve (result.indexed_families.size ());
      for (auto& family : result.indexed_families) {
        symmetry::indexed_family_certificate certificate;
        certificate.is_input = not family.is_output;
        certificate.lo = family.lo;
        certificate.hi = family.hi;
        certificate.members = std::move (family.members);
        value.metadata.tlsf_indexed_families.push_back (std::move (certificate));
      }
    }

    value.formula = std::move (result.preprocessed_ltl);
    value.inputs = std::move (result.inputs);
    value.outputs = std::move (result.outputs);
    // SyFCo groups scalar declarations before ordinary buses and enum buses,
    // reverses source order within each group, and keeps each bus's members in
    // ascending index order.
    // Certificate members use that same ascending index order.
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
