#pragma once

/// cpre_event.hh — one recorded controller-predecessor update.
///
/// Shared by the explicit replay and the threshold-BDD replay so that both read
/// exactly the same record.  If they parsed it separately, a disagreement
/// between them could be a parser difference rather than a representation
/// difference, which is the only thing the comparison is for.

#include "configuration.hh"
#include "research/rank_action_replay.hh"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <posets/utils/vector_mm.hh>

namespace acacia::research {

  struct event {
      int schema_version = 0;
      int k = -1;
      int loop = -1;
      size_t states = 0;
      std::vector<posets::utils::vector_mm<VECTOR_ELT_T>> before, after;
      std::vector<action_vec> actions;
  };

  [[noreturn]] inline void fail (const std::string& message) {
    throw std::runtime_error (message);
  }

  inline long long field (const std::string& text, const std::string& key) {
    const auto at = text.find (key + "=");
    if (at == std::string::npos)
      return -1;
    const auto begin = text.data () + at + key.size () + 1;
    const auto end_at = text.find_first_of (" \t\r\n", at + key.size () + 1);
    const auto end = end_at == std::string::npos ? text.data () + text.size ()
                                                 : text.data () + end_at;
    long long value = 0;
    const auto parsed = std::from_chars (begin, end, value);
    if (begin == end or parsed.ec != std::errc {} or parsed.ptr != end)
      fail ("invalid integer for " + key);
    return value;
  }

  inline std::vector<int> parse_row (const std::string& line) {
    std::vector<int> row;
    std::istringstream in {line};
    int value;
    while (in >> value)
      row.push_back (value);
    in.clear ();
    in >> std::ws;
    if (not in.eof ())
      fail ("malformed rank row: " + line);
    return row;
  }

  inline event load (const std::filesystem::path& path, size_t states,
                     size_t bool_threshold) {
    if (states == 0)
      fail ("serialized CPre events cannot have zero dimensions");
    if (bool_threshold > states)
      fail ("Boolean split exceeds CPre event dimension");
    std::ifstream in {path};
    if (not in)
      fail ("cannot open " + path.string ());

    event ev;
    ev.states = states;
    std::string line;
    enum { none, before, actions, after } section = none;
    bool saw_header = false, saw_before = false, saw_actions = false, saw_after = false;
    size_t declared_before = std::numeric_limits<size_t>::max ();
    size_t declared_actions = std::numeric_limits<size_t>::max ();
    size_t declared_after = std::numeric_limits<size_t>::max ();
    size_t line_number = 0;

    while (std::getline (in, line)) {
      ++line_number;
      if (not line.empty () and line.back () == '\r')
        line.pop_back ();
      if (line.empty ())
        continue;
      if (line[0] == '#') {
        if (saw_header or section != none)
          fail ("duplicate or misplaced CPre header at line " +
                std::to_string (line_number));
        ev.schema_version = static_cast<int> (field (line, "schema_version"));
        ev.k = static_cast<int> (field (line, "k"));
        ev.loop = static_cast<int> (field (line, "loop"));
        const long long before_count = field (line, "before");
        const long long action_count = field (line, "actions");
        if (ev.schema_version != 2)
          fail ("unsupported schema_version " + std::to_string (ev.schema_version));
        if (ev.k < 1 or ev.k > std::numeric_limits<VECTOR_ELT_T>::max ())
          fail ("CPre event K is outside the supported 1..127 range");
        if (ev.loop < 0 or before_count < 0 or action_count <= 0)
          fail ("invalid CPre header counts");
        declared_before = static_cast<size_t> (before_count);
        declared_actions = static_cast<size_t> (action_count);
        saw_header = true;
        continue;
      }
      if (not saw_header)
        fail ("content before CPre header at line " + std::to_string (line_number));
      if (line == "[before]") {
        if (section != none or saw_before)
          fail ("misordered [before] section");
        section = before;
        saw_before = true;
        continue;
      }
      if (line == "[actions]") {
        if (section != before or saw_actions)
          fail ("misordered [actions] section");
        section = actions;
        saw_actions = true;
        continue;
      }
      if (line.rfind ("[after]\t", 0) == 0) {
        if (section != actions or saw_after)
          fail ("misordered [after] section");
        const std::string count = line.substr (8);
        if (count.empty ())
          fail ("missing [after] count");
        const auto parsed = std::from_chars (count.data (), count.data () + count.size (),
                                             declared_after);
        if (parsed.ec != std::errc {} or parsed.ptr != count.data () + count.size ())
          fail ("invalid [after] count");
        section = after;
        saw_after = true;
        continue;
      }

      if (section == before or section == after) {
        auto row = parse_row (line);
        if (row.size () != states)
          fail ("row of width " + std::to_string (row.size ()) + " where meta.tsv says "
                + std::to_string (states));
        posets::utils::vector_mm<VECTOR_ELT_T> v (states, 0);
        for (size_t i = 0; i < states; ++i) {
          const int upper = i < bool_threshold ? ev.k - 1 : 0;
          if (row[i] < -1 or row[i] > upper)
            fail ("rank coordinate outside the event's safe box at line " +
                  std::to_string (line_number));
          v[i] = static_cast<VECTOR_ELT_T> (row[i]);
        }
        (section == before ? ev.before : ev.after).push_back (std::move (v));
      }
      else if (section == actions) {
        if (line.rfind ("action\t", 0) == 0) {
          std::istringstream header {line};
          std::string label;
          size_t index;
          if (not (header >> label >> index) or label != "action"
              or index != ev.actions.size ())
            fail ("invalid or out-of-order action header: " + line);
          header >> std::ws;
          if (not header.eof ())
            fail ("trailing data in action header: " + line);
          ev.actions.emplace_back (states);
          continue;
        }
        if (ev.actions.empty ())
          fail ("transition row before any action header");
        std::istringstream row {line};
        unsigned i, j;
        int increment;
        if (not (row >> i >> j >> increment))
          fail ("malformed transition row: " + line);
        row >> std::ws;
        if (not row.eof ())
          fail ("trailing data in transition row: " + line);
        if (i >= states or j >= states)
          fail ("transition row indexes a state outside dimension " +
                std::to_string (states));
        if (increment != 0 and increment != 1)
          fail ("transition increment is not 0 or 1");
        ev.actions.back ()[i].emplace_back (j, increment == 1);
      }
      else
        fail ("data outside a CPre section at line " + std::to_string (line_number));
    }
    if (not saw_header or not saw_before or not saw_actions or not saw_after)
      fail ("truncated CPre event: missing required section");
    if (ev.before.size () != declared_before)
      fail ("CPre before-count mismatch");
    if (ev.actions.size () != declared_actions)
      fail ("CPre action-count mismatch");
    if (ev.after.size () != declared_after)
      fail ("CPre after-count mismatch");
    return ev;
  }


  inline size_t meta_field (const std::filesystem::path& dir, const std::string& name) {
    std::ifstream meta {dir / "meta.tsv"};
    if (not meta)
      fail ("cannot open " + (dir / "meta.tsv").string ());
    std::string header, values;
    std::getline (meta, header);
    std::getline (meta, values);
    std::istringstream hs {header}, vs {values};
    std::string h, v;
    while (hs >> h and vs >> v)
      if (h == name)
        return static_cast<size_t> (std::strtoull (v.c_str (), nullptr, 10));
    fail ("meta.tsv has no column " + name);
  }

  /// Numeric order, not filename order: `cpre-10.tsv` sorts before `cpre-2.tsv`
  /// as a string, and replaying loops out of order silently compares the wrong
  /// regions.
  inline std::vector<std::filesystem::path> find_events (const std::filesystem::path& dir) {
    std::map<long long, std::filesystem::path> byloop;
    for (const auto& entry : std::filesystem::directory_iterator {dir}) {
      const std::string name = entry.path ().filename ().string ();
      if (name.rfind ("cpre-", 0) != 0 or entry.path ().extension () != ".tsv")
        continue;
      byloop.emplace (std::strtoll (name.c_str () + 5, nullptr, 10), entry.path ());
    }
    std::vector<std::filesystem::path> out;
    for (auto& [loop, path] : byloop)
      out.push_back (path);
    return out;
  }

}  // namespace acacia::research
