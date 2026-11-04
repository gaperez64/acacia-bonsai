// Exercise the real driver with a deterministic slow summary-output observer.
#define main pipeline_bench_main
#include "parity_pipeline_bench.cc"
#undef main

#include <cassert>
#include <fstream>
#include <sstream>

namespace {
  thread_local bool fake_clock = false;
  thread_local uint64_t fake_now = 0;
  class late_output : public std::stringbuf {
      int sync () override {
        if (str ().find ("\"event\":\"constructor_result\"") != std::string::npos)
          fake_now = 2000000000ULL;
        return 0;
      }
  };
}

extern "C" int __real_clock_gettime (clockid_t, timespec*);
extern "C" int __wrap_clock_gettime (clockid_t clock, timespec* value) {
  if (!fake_clock || clock != CLOCK_MONOTONIC)
    return __real_clock_gettime (clock, value);
  *value = timespec {time_t (fake_now / 1000000000ULL), long (fake_now % 1000000000ULL)};
  return 0;
}

int main () {
  const auto file = std::filesystem::temp_directory_path () /
                    ("pipeline-observer-" + std::to_string (getpid ()) + ".tlsf");
  std::ofstream (file) << "INFO { TITLE: \"observer\" SEMANTICS: Mealy TARGET: Mealy } "
                          "MAIN { INPUTS { i; } OUTPUTS { o; } GUARANTEES { G F i; } }";
  for (const std::string mode : {"positive", "buchi"}) {
    late_output capture;
    auto* output = std::cout.rdbuf (&capture);
    std::array<std::string, 6> arguments {
        "parity-pipeline-bench", file.string (), "--construct", mode, "--seconds", "1"};
    std::array<char*, 6> argv;
    for (size_t i = 0; i < argv.size (); ++i)
      argv[i] = arguments[i].data ();
    fake_now = 1;
    fake_clock = true;
    const int code = pipeline_bench_main (int (argv.size ()), argv.data ());
    fake_clock = false;
    std::cout.rdbuf (output);
    const auto text = capture.str ();
    assert (fake_now == 2000000000ULL && code == 4);
    assert (text.find (mode == "positive" ? "\"admission\":\"ADMITTED\""
                                          : "\"admission\":\"REJECTED\"") != std::string::npos);
    assert (text.find ("\"provisional_until_exit\":1") != std::string::npos);
  }
  std::filesystem::remove (file);
  std::cout << "Late constructor summaries remain unconfirmed UNKNOWN\n";
}
