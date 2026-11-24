#include "solver/aiger_abc.hh"

#include "solver/bounded_subprocess.hh"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>

namespace acacia::synthesis {
  void shrink_aiger (std::string& text, const abc_options& options,
                     const std::function<void ()>& before) {
    if (!options.enabled || text.size () > aiger_abc_payload_bytes)
      return;
    const auto deadline = subprocess::optional_deadline (options.deadline);
    const auto now = phase_clock (CLOCK_MONOTONIC);
    if (now >= deadline || deadline - now < 2000000)
      return;
    try {
      const char* cause = "none";
      const auto memory = subprocess::optional_memory (aiger_abc_payload_bytes,
                                                       options.worker_rss_share, nullptr, cause);
      if (!memory)
        return;
      // Unlinked immediately; a killed helper leaves no files behind.
      const auto close = [] (FILE* file) {
        if (file)
          std::fclose (file);
      };
      std::unique_ptr<FILE, decltype (close)> input (std::tmpfile (), close),
          output (std::tmpfile (), close);
      if (!input || !output ||
          std::fwrite (text.data (), 1, text.size (), input.get ()) != text.size () ||
          std::fflush (input.get ()) != 0)
        return;
      const auto attempt = subprocess::run<true> (deadline, memory, [&] {
        if (before)
          before ();
        std::fflush (nullptr);
        if (!std::freopen ("/dev/null", "w", stdout) || !std::freopen ("/dev/null", "w", stderr))
          return subprocess::error_exit;
        // ABC reads and appends abc.history, including through symlinks. This
        // directory cannot contain or create it; isolate before ABC's constructors.
        if (chdir ("/proc/self/fd") != 0)
          return subprocess::error_exit;
        const auto helper =
            std::filesystem::read_symlink ("/proc/self/exe").parent_path () / "acacia-aiger-abc";
        const auto in = "/dev/fd/" + std::to_string (fileno (input.get ()));
        const auto out = "/dev/fd/" + std::to_string (fileno (output.get ()));
        execl (helper.c_str (), helper.c_str (), in.c_str (), out.c_str (), nullptr);
        // Test executables live in build/tests rather than beside the helper.
        execl (ACACIA_ABC_HELPER_PATH, ACACIA_ABC_HELPER_PATH, in.c_str (), out.c_str (), nullptr);
        return subprocess::error_exit;
      });
      if (attempt.code != 0 || std::strcmp (attempt.cause, "none") != 0 ||
          phase_clock (CLOCK_MONOTONIC) >= deadline ||
          std::fseek (output.get (), 0, SEEK_END) != 0 || std::ftell (output.get ()) <= 0 ||
          std::ftell (output.get ()) > long (aiger_abc_payload_bytes))
        return;
      std::ifstream result ("/dev/fd/" + std::to_string (fileno (output.get ())),
                            std::ios::binary);
      std::string candidate {std::istreambuf_iterator<char> (result), {}};
      if (!result.bad () && !candidate.empty () && candidate.size () <= aiger_abc_payload_bytes &&
          phase_clock (CLOCK_MONOTONIC) < deadline)
        text = std::move (candidate);
    } catch (...) {
      // Optional work must never prevent exporting the incumbent.
    }
  }
}
