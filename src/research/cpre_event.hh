#pragma once

/// cpre_event.hh — one recorded controller-predecessor update.
///
/// Shared by the explicit replay and the threshold-BDD replay so that both read
/// exactly the same record.  If they parsed it separately, a disagreement
/// between them could be a parser difference rather than a representation
/// difference, which is the only thing the comparison is for.

#include "configuration.hh"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <posets/utils/vector_mm.hh>

namespace acacia::research {

  /// avec[i] is a list of (j, increment); `apply` below reads it exactly as
  /// actioners::standard does, with the same names it uses.
  using action = std::vector<std::pair<unsigned, bool>>;
  using action_vec = std::vector<action>;

  struct event {
      int schema_version = 0;
      int k = -1;
      int loop = -1;
      size_t states = 0;
      std::vector<posets::utils::vector_mm<VECTOR_ELT_T>> before, after;
      std::vector<action_vec> actions;
  };

  [[noreturn]] inline void fail (const std::string& message) {
    std::cerr << "acacia-cpre-replay: " << message << '\n';
    std::exit (1);
  }

  inline long long field (const std::string& text, const std::string& key) {
    const auto at = text.find (key + "=");
    if (at == std::string::npos)
      return -1;
    return std::strtoll (text.c_str () + at + key.size () + 1, nullptr, 10);
  }

  inline std::vector<VECTOR_ELT_T> parse_row (const std::string& line) {
    std::vector<VECTOR_ELT_T> row;
    std::istringstream in {line};
    int value;
    while (in >> value)
      row.push_back (static_cast<VECTOR_ELT_T> (value));
    return row;
  }

  inline event load (const std::filesystem::path& path, size_t states) {
    std::ifstream in {path};
    if (not in)
      fail ("cannot open " + path.string ());

    event ev;
    ev.states = states;
    std::string line;
    enum { none, before, actions, after } section = none;

    while (std::getline (in, line)) {
      if (line.empty ())
        continue;
      if (line[0] == '#') {
        ev.schema_version = static_cast<int> (field (line, "schema_version"));
        ev.k = static_cast<int> (field (line, "k"));
        ev.loop = static_cast<int> (field (line, "loop"));
        continue;
      }
      if (line.rfind ("[before]", 0) == 0) { section = before; continue; }
      if (line.rfind ("[actions]", 0) == 0) { section = actions; continue; }
      if (line.rfind ("[after]", 0) == 0) { section = after; continue; }

      if (section == before or section == after) {
        auto row = parse_row (line);
        if (row.size () != states)
          fail ("row of width " + std::to_string (row.size ()) + " where meta.tsv says "
                + std::to_string (states));
        posets::utils::vector_mm<VECTOR_ELT_T> v (states, 0);
        std::copy (row.begin (), row.end (), v.begin ());
        (section == before ? ev.before : ev.after).push_back (std::move (v));
      }
      else if (section == actions) {
        if (line.rfind ("action\t", 0) == 0) {
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
        if (i >= states)
          fail ("transition row indexes state " + std::to_string (i) + " of "
                + std::to_string (states));
        ev.actions.back ()[i].emplace_back (j, increment != 0);
      }
    }
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
