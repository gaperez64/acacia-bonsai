#include "solver/aiger_abc_codec.hh"
#include <misc/util/abc_namespaces.h>

#include <base/main/abcapis.h>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace {
  constexpr size_t payload_bytes = acacia::synthesis::aiger_abc_payload_bytes;
  using namespace acacia::synthesis::aiger_codec;

  std::string unpack (std::istream& in, const aiger& original) {
    aiger aig (in, "aig");
    if (aig.i != original.i || aig.o != original.o || aig.l != original.l ||
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

int main (int argc, char** argv) {
  if (argc != 3)
    return 5;
  try {
    std::ifstream source (argv[1], std::ios::binary);
    std::string text (payload_bytes + 1, '\0');
    source.read (text.data (), text.size ());
    text.resize (source.gcount ());
    if (!source.eof () || text.size () > payload_bytes)
      return 5;
    const auto close = [] (FILE* file) {
      if (file)
        std::fclose (file);
    };
    std::unique_ptr<FILE, decltype (close)> input (std::tmpfile (), close),
        output (std::tmpfile (), close);
    if (!input || !output)
      return 5;
    std::ostringstream binary;
    const auto original = pack (text, binary);
    const auto bytes = binary.str ();
    if (std::fwrite (bytes.data (), 1, bytes.size (), input.get ()) != bytes.size () ||
        std::fflush (input.get ()) != 0)
      return 5;
    Abc_Start ();
    const auto command = "read_aiger /dev/fd/" + std::to_string (fileno (input.get ())) +
                         "; strash; dc2; write_aiger -s /dev/fd/" +
                         std::to_string (fileno (output.get ()));
    const int status = Cmd_CommandExecute (Abc_FrameGetGlobalFrame (), command.c_str ());
    Abc_Stop ();
    if (status || std::fseek (output.get (), 0, SEEK_END) != 0 || std::ftell (output.get ()) < 0 ||
        std::ftell (output.get ()) > long (payload_bytes))
      return 5;
    std::ifstream result ("/dev/fd/" + std::to_string (fileno (output.get ())), std::ios::binary);
    const auto candidate = unpack (result, original);
    if (candidate.empty () || candidate.size () > payload_bytes)
      return 5;
    std::ofstream destination (argv[2], std::ios::binary);
    destination << response_prefix << candidate.size () << '\n' << candidate;
    destination.close ();
    return destination ? 0 : 5;
  } catch (const std::bad_alloc&) {
    return 4;
  } catch (...) {
    return 5;
  }
}
