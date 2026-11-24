#include "solver/aiger_abc.hh"
#include "solver/bounded_subprocess.hh"

#include <misc/util/abc_namespaces.h>
#include <base/main/abcapis.h>

#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace acacia::synthesis {
  namespace {
    constexpr size_t payload_bytes = 1024 * 1024;
    struct payload {
        size_t size;
        char text[payload_bytes];
    };

    // ABC reads binary AIGER; Spot exports ASCII. No BDD reconstruction is needed.
    struct aiger {
        unsigned m, i, l, o, a;
        std::map<std::string, std::string> symbols;

        aiger (std::istream& in, const char* kind) {
          std::string line, magic, extra;
          std::getline (in, line);
          std::istringstream header (line);
          if (!(header >> magic >> m >> i >> l >> o >> a) || magic != kind || header >> extra ||
              m > payload_bytes || o > payload_bytes || uint64_t (i) + l + a != m)
            throw std::runtime_error ("invalid AIGER header");
        }

        void header (std::ostream& out, const char* kind) const {
          out << kind << ' ' << m << ' ' << i << ' ' << l << ' ' << o << ' ' << a << '\n';
        }

        void names (std::istream& in, std::ostream& out) {
          std::string line;
          while (std::getline (in, line) && line != "c") {
            if (line.starts_with ('l'))
              continue;
            const auto space = line.find (' ');
            if (space == std::string::npos || line.empty () ||
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

    unsigned literal (std::istream& in, unsigned max) {
      unsigned value;
      if (!(in >> value) || value > max)
        throw std::runtime_error ("invalid AIGER literal");
      return value;
    }

    aiger pack (const std::string& text, std::ostream& out) {
      std::istringstream in (text);
      aiger aig (in, "aag");
      aig.header (out, "aig");
      const auto read = [&] { return literal (in, 2 * aig.m + 1); };
      for (unsigned n = 0; n < aig.i; ++n)
        if (read () != 2 * (n + 1))
          throw std::runtime_error ("unordered AIGER inputs");
      for (unsigned n = 0; n < aig.l; ++n) {
        if (read () != 2 * (aig.i + n + 1))
          throw std::runtime_error ("unordered AIGER latches");
        out << read () << '\n';
      }
      for (unsigned n = 0; n < aig.o; ++n)
        out << read () << '\n';
      const auto delta = [&] (unsigned value) {
        while (value >= 128) {
          out.put (char ((value & 127) | 128));
          value >>= 7;
        }
        out.put (char (value));
      };
      for (unsigned n = 0; n < aig.a; ++n) {
        const auto current = read (), left = read (), right = read ();
        if (current != 2 * (aig.i + aig.l + n + 1) || left >= current || right >= current)
          throw std::runtime_error ("unordered AIGER gates");
        delta (current - std::max (left, right));
        delta (std::max (left, right) - std::min (left, right));
      }
      in.ignore (1, '\n');
      aig.names (in, out);
      return aig;
    }

    std::string unpack (std::istream& in, const aiger& original) {
      aiger aig (in, "aig");
      if (aig.i != original.i || aig.o != original.o ||
          uint64_t (aig.a) + aig.l >= uint64_t (original.a) + original.l)
        return {};
      std::ostringstream out;
      aig.header (out, "aag");
      for (unsigned n = 0; n < aig.i; ++n)
        out << 2 * (n + 1) << '\n';
      std::string line, extra;
      for (unsigned n = 0; n < aig.l + aig.o; ++n) {
        if (!std::getline (in, line))
          throw std::runtime_error ("truncated AIGER");
        std::istringstream row (line);
        const auto value = literal (row, 2 * aig.m + 1);
        if (n < aig.l) {
          if ((row >> extra && extra != "0") || row >> extra)
            throw std::runtime_error ("nonzero AIGER initial state");
          out << 2 * (aig.i + n + 1) << ' ';
        }
        else if (row >> extra)
          throw std::runtime_error ("invalid AIGER output");
        out << value << '\n';
      }
      const auto delta = [&] {
        unsigned value = 0;
        for (unsigned shift = 0; shift < 28; shift += 7) {
          const int byte = in.get ();
          if (byte == EOF)
            break;
          value |= unsigned (byte & 127) << shift;
          if (!(byte & 128))
            return value;
        }
        throw std::runtime_error ("invalid AIGER delta");
      };
      for (unsigned n = 0; n < aig.a; ++n) {
        const auto current = 2 * (aig.i + aig.l + n + 1), first = delta ();
        if (!first || first > current)
          throw std::runtime_error ("invalid AIGER gate");
        const auto left = current - first, second = delta ();
        if (second > left)
          throw std::runtime_error ("invalid AIGER gate");
        out << current << ' ' << left << ' ' << left - second << '\n';
      }
      aig.names (in, out);
      return aig.symbols == original.symbols ? out.str () : std::string {};
    }
  }

  void shrink_aiger (std::string& text, const abc_options& options,
                     const std::function<void ()>& before) {
    if (!options.enabled || text.size () > payload_bytes)
      return;
    const auto deadline = subprocess::optional_deadline (options.deadline);
    const auto now = phase_clock (CLOCK_MONOTONIC);
    if (now >= deadline || deadline - now < 2000000)
      return;
    try {
      const char* cause = "none";
      const auto memory = subprocess::optional_memory (sizeof (payload), options.worker_rss_share,
                                                       nullptr, cause);
      if (!memory)
        return;
      const auto unmap = [] (payload* data) { munmap (data, sizeof (payload)); };
      auto* mapping = static_cast<payload*> (mmap (
          nullptr, sizeof (payload), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0));
      if (mapping == MAP_FAILED)
        return;
      std::unique_ptr<payload, decltype (unmap)> data (mapping, unmap);
      const auto attempt = subprocess::run<true> (deadline, memory, [&] {
        if (before)
          before ();
        // tmpfile unlinks immediately: a killed child leaves no files behind.
        const auto close = [] (FILE* file) { if (file) std::fclose (file); };
        std::unique_ptr<FILE, decltype (close)> input (std::tmpfile (), close),
            output (std::tmpfile (), close);
        if (!input || !output)
          return subprocess::error_exit;
        std::ostringstream binary;
        const auto original = pack (text, binary);
        const auto bytes = binary.str ();
        if (std::fwrite (bytes.data (), 1, bytes.size (), input.get ()) != bytes.size () ||
            std::fflush (input.get ()) != 0)
          return subprocess::error_exit;
        // Keep ABC diagnostics off the solver's result and diagnostic streams.
        std::fflush (nullptr);
        if (!std::freopen ("/dev/null", "w", stdout) || !std::freopen ("/dev/null", "w", stderr))
          return subprocess::error_exit;
        Abc_Start ();
        const auto command = "read_aiger /dev/fd/" + std::to_string (fileno (input.get ())) +
                             "; strash; dc2; write_aiger -s /dev/fd/" +
                             std::to_string (fileno (output.get ()));
        const int status = Cmd_CommandExecute (Abc_FrameGetGlobalFrame (), command.c_str ());
        Abc_Stop ();
        if (status || std::fseek (output.get (), 0, SEEK_END) != 0 ||
            std::ftell (output.get ()) < 0 || std::ftell (output.get ()) > long (payload_bytes))
          return subprocess::error_exit;
        std::ifstream result ("/dev/fd/" + std::to_string (fileno (output.get ())), std::ios::binary);
        const auto candidate = unpack (result, original);
        if (candidate.empty () || candidate.size () > payload_bytes)
          return subprocess::error_exit;
        std::memcpy (data->text, candidate.data (), candidate.size ());
        data->size = candidate.size ();
        return 0;
      });
      if (attempt.code == 0 && std::strcmp (attempt.cause, "none") == 0 &&
          data->size && data->size <= payload_bytes && phase_clock (CLOCK_MONOTONIC) < deadline)
        text.assign (data->text, data->size);
    } catch (...) {
      // Optional work must never prevent exporting the incumbent.
    }
  }
}
