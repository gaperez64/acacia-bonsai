#pragma once

/// all_input_actions.hh — the complete input/action table of one worker.
///
/// The CPre event carries the single input the picker selected, which is what
/// replaying that update needs.  Searching for an inductive subregion instead
/// asks, of every candidate generator, whether *every* input class has some
/// action keeping it inside -- so it needs the whole table.  The table is
/// independent of the region and of K, so one dump serves every checkpoint.

#include "research/rank_action_replay.hh"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace acacia::research {

  struct input_action_table {
      int schema_version = 0;
      size_t states = 0;
      /// actions[i] is input class i's ordered action list, in the order the
      /// solver would scan it.
      std::vector<std::vector<action_vec>> actions;

      [[nodiscard]] size_t input_count () const { return actions.size (); }
      [[nodiscard]] size_t action_count () const {
        size_t n = 0;
        for (const auto& per_input : actions)
          n += per_input.size ();
        return n;
      }
  };

  inline input_action_table load_input_actions (const std::filesystem::path& path,
                                                size_t states) {
    std::ifstream in {path};
    if (not in)
      throw std::runtime_error ("cannot open " + path.string ());

    input_action_table table;
    table.states = states;
    std::string line;
    while (std::getline (in, line)) {
      if (line.empty ())
        continue;
      if (line[0] == '#') {
        const auto at = line.find ("schema_version=");
        if (at != std::string::npos)
          table.schema_version =
              static_cast<int> (std::strtol (line.c_str () + at + 15, nullptr, 10));
        continue;
      }
      if (line.rfind ("[input", 0) == 0) {
        table.actions.emplace_back ();
        continue;
      }
      if (line.rfind ("action\t", 0) == 0) {
        if (table.actions.empty ())
          throw std::runtime_error ("action row before any input header");
        table.actions.back ().emplace_back (states);
        continue;
      }
      if (table.actions.empty () or table.actions.back ().empty ())
        throw std::runtime_error ("transition row before any action header");
      std::istringstream row {line};
      unsigned i, j;
      int increment;
      if (not (row >> i >> j >> increment))
        throw std::runtime_error ("malformed transition row: " + line);
      if (i >= states)
        throw std::runtime_error ("transition row indexes state " + std::to_string (i) + " of "
                                  + std::to_string (states));
      table.actions.back ().back ()[i].emplace_back (j, increment != 0);
    }
    if (table.schema_version != 1)
      throw std::runtime_error ("unsupported all-input-actions schema_version "
                                + std::to_string (table.schema_version));
    return table;
  }

  /// One recorded region: the maxima, the bound they were taken at, and whether
  /// the bound had just been raised -- which matters because a raise
  /// deliberately over-approximates, so such a region is not a candidate for
  /// "is this already inductive".
  struct checkpoint {
      int loop = -1;
      int k = -1;
      bool after_bound_raise = false;
      std::vector<rank_vector> maxima;
  };

  inline checkpoint load_checkpoint (const std::filesystem::path& path, size_t states) {
    std::ifstream in {path};
    if (not in)
      throw std::runtime_error ("cannot open " + path.string ());

    checkpoint point;
    std::string line;
    auto field = [] (const std::string& text, const std::string& key) -> long long {
      const auto at = text.find (key + "=");
      return at == std::string::npos
                 ? -1
                 : std::strtoll (text.c_str () + at + key.size () + 1, nullptr, 10);
    };
    while (std::getline (in, line)) {
      if (line.empty ())
        continue;
      if (line[0] == '#') {
        point.loop = static_cast<int> (field (line, "loop"));
        point.k = static_cast<int> (field (line, "k"));
        point.after_bound_raise = field (line, "after_bound_raise") == 1;
        continue;
      }
      std::istringstream row {line};
      rank_vector v (states, 0);
      int value;
      size_t i = 0;
      while (row >> value and i < states)
        v[i++] = static_cast<VECTOR_ELT_T> (value);
      if (i != states)
        throw std::runtime_error ("row of width " + std::to_string (i) + " where meta.tsv says "
                                  + std::to_string (states));
      point.maxima.push_back (std::move (v));
    }
    return point;
  }

}  // namespace acacia::research
