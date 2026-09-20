#pragma once

/// all_input_actions.hh — the complete input/action table of one worker.
///
/// The CPre event carries the single input the picker selected, which is what
/// replaying that update needs.  Searching for an inductive subregion instead
/// asks, of every candidate generator, whether *every* input class has some
/// action keeping it inside -- so it needs the whole table.  The table is
/// independent of the region and of K, so one dump serves every checkpoint.

#include "research/rank_action_replay.hh"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <limits>
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
    if (states == 0)
      throw std::runtime_error ("serialized all-input table cannot have zero dimensions");
    std::ifstream in {path};
    if (not in)
      throw std::runtime_error ("cannot open " + path.string ());

    input_action_table table;
    table.states = states;
    std::string line;
    bool saw_header = false;
    size_t declared_inputs = std::numeric_limits<size_t>::max ();
    size_t declared_actions = std::numeric_limits<size_t>::max ();
    size_t declared_transitions = std::numeric_limits<size_t>::max ();
    size_t actual_transitions = 0;
    auto header_size = [&] (const std::string& text, const std::string& key) {
      const auto at = text.find (key + "=");
      if (at == std::string::npos)
        throw std::runtime_error ("all-input header has no " + key);
      const auto start = at + key.size () + 1;
      const auto finish = text.find_first_of (" \t\r\n", start);
      const auto* begin = text.data () + start;
      const auto* end = finish == std::string::npos ? text.data () + text.size ()
                                                    : text.data () + finish;
      size_t value = 0;
      const auto parsed = std::from_chars (begin, end, value);
      if (begin == end or parsed.ec != std::errc {} or parsed.ptr != end)
        throw std::runtime_error ("invalid all-input header field " + key);
      return value;
    };
    size_t line_number = 0;
    while (std::getline (in, line)) {
      ++line_number;
      if (not line.empty () and line.back () == '\r')
        line.pop_back ();
      if (line.empty ())
        continue;
      if (line[0] == '#') {
        if (saw_header or not table.actions.empty ())
          throw std::runtime_error ("duplicate or misplaced all-input header");
        table.schema_version = static_cast<int> (header_size (line, "schema_version"));
        declared_inputs = header_size (line, "inputs");
        declared_actions = header_size (line, "actions");
        declared_transitions = header_size (line, "transitions");
        if (table.schema_version != 1)
          throw std::runtime_error ("unsupported all-input-actions schema_version "
                                    + std::to_string (table.schema_version));
        saw_header = true;
        continue;
      }
      if (not saw_header)
        throw std::runtime_error ("content before all-input header at line "
                                  + std::to_string (line_number));
      if (line.rfind ("[input\t", 0) == 0 and line.back () == ']') {
        const std::string text = line.substr (7, line.size () - 8);
        size_t index = 0;
        const auto parsed = std::from_chars (text.data (), text.data () + text.size (), index);
        if (text.empty () or parsed.ec != std::errc {}
            or parsed.ptr != text.data () + text.size () or index != table.actions.size ())
          throw std::runtime_error ("invalid or out-of-order input header: " + line);
        table.actions.emplace_back ();
        continue;
      }
      if (line.rfind ("action\t", 0) == 0) {
        if (table.actions.empty ())
          throw std::runtime_error ("action row before any input header");
        const std::string text = line.substr (7);
        size_t index = 0;
        const auto parsed = std::from_chars (text.data (), text.data () + text.size (), index);
        if (text.empty () or parsed.ec != std::errc {}
            or parsed.ptr != text.data () + text.size ()
            or index != table.actions.back ().size ())
          throw std::runtime_error ("invalid or out-of-order action header: " + line);
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
      row >> std::ws;
      if (not row.eof ())
        throw std::runtime_error ("trailing data in transition row: " + line);
      if (i >= states or j >= states)
        throw std::runtime_error ("transition row indexes a state outside dimension "
                                  + std::to_string (states));
      if (increment != 0 and increment != 1)
        throw std::runtime_error ("transition increment is not 0 or 1");
      table.actions.back ().back ()[i].emplace_back (j, increment == 1);
      ++actual_transitions;
    }
    if (not saw_header)
      throw std::runtime_error ("missing all-input-actions header");
    if (table.input_count () != declared_inputs or table.action_count () != declared_actions
        or actual_transitions != declared_transitions)
      throw std::runtime_error ("all-input-actions declared count mismatch");
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
