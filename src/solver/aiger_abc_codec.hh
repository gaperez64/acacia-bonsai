#pragma once

#include "solver/aiger_abc.hh"

#include <algorithm>
#include <map>
#include <sstream>
#include <stdexcept>

namespace acacia::synthesis::aiger_codec {
  inline constexpr char response_prefix[] = "acacia-aiger-abc 1 ";
  inline constexpr size_t response_header_bytes = 64;

  // ABC reads binary AIGER; Spot exports ASCII. No BDD reconstruction is needed.
  struct aiger {
      unsigned m, i, l, o, a;
      std::map<std::string, std::string> symbols;

      aiger (std::istream& in, const char* kind) {
        std::string line, magic, extra;
        std::getline (in, line);
        std::istringstream header (line);
        if (!(header >> magic >> m >> i >> l >> o >> a) || magic != kind || header >> extra ||
            m > aiger_abc_payload_bytes || o > aiger_abc_payload_bytes ||
            uint64_t (i) + l + a != m)
          throw std::runtime_error ("invalid AIGER header");
      }

      void header (std::ostream& out, const char* kind) const {
        out << kind << ' ' << m << ' ' << i << ' ' << l << ' ' << o << ' ' << a << '\n';
      }

      void names (std::istream& in, std::ostream& out, bool complete = false) {
        std::string line;
        while (std::getline (in, line)) {
          if (!complete && line == "c")
            break;
          if (!complete && line.starts_with ('l'))
            continue;
          const auto space = line.find (' ');
          if (space == std::string::npos || space + 1 == line.size () ||
              (line[0] != 'i' && line[0] != 'o') ||
              !symbols.emplace (line.substr (0, space), line.substr (space + 1)).second)
            throw std::runtime_error ("invalid AIGER symbols");
        }
        if (symbols.size () != size_t (i) + o)
          throw std::runtime_error ("missing AIGER symbols");
        for (char type : {'i', 'o'})
          for (unsigned n = 0; n < (type == 'i' ? i : o); ++n) {
            const auto key = type + std::to_string (n);
            out << key << ' ' << symbols.at (key) << '\n';
          }
      }
  };

  inline unsigned literal (std::istream& in, unsigned max) {
    unsigned value;
    if (!(in >> value) || value > max)
      throw std::runtime_error ("invalid AIGER literal");
    return value;
  }

  inline aiger pack (const std::string& text, std::ostream& out) {
    std::istringstream in (text);
    aiger aig (in, "aag");
    aig.header (out, "aig");
    const auto row = [&] (std::initializer_list<unsigned*> values, bool latch = false) {
      std::string line, extra;
      if (!std::getline (in, line))
        throw std::runtime_error ("truncated AIGER");
      std::istringstream fields (line);
      for (auto* value : values)
        *value = literal (fields, 2 * aig.m + 1);
      // Spot's latches are reset to zero, implicitly or explicitly.
      if ((latch && fields >> extra && extra != "0") || fields >> extra)
        throw std::runtime_error ("invalid AIGER row or reset");
    };
    unsigned current, left, right;
    for (unsigned n = 0; n < aig.i; ++n) {
      row ({&current});
      if (current != 2 * (n + 1))
        throw std::runtime_error ("unordered AIGER inputs");
    }
    for (unsigned n = 0; n < aig.l; ++n) {
      row ({&current, &left}, true);
      if (current != 2 * (aig.i + n + 1))
        throw std::runtime_error ("unordered AIGER latches");
      out << left << '\n';
    }
    for (unsigned n = 0; n < aig.o; ++n) {
      row ({&current});
      out << current << '\n';
    }
    const auto delta = [&] (unsigned value) {
      while (value >= 128) {
        out.put (char ((value & 127) | 128));
        value >>= 7;
      }
      out.put (char (value));
    };
    for (unsigned n = 0; n < aig.a; ++n) {
      row ({&current, &left, &right});
      if (current != 2 * (aig.i + aig.l + n + 1) || left >= current || right >= current)
        throw std::runtime_error ("unordered AIGER gates");
      delta (current - std::max (left, right));
      delta (std::max (left, right) - std::min (left, right));
    }
    aig.names (in, out, true);
    return aig;
  }
}
