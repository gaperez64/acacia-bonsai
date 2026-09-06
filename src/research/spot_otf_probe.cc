// P0 API audit only: separate factory, two-row consumption, materialization,
// and ownership exercises.  Each phase runs in a fresh, resource-capped child
// so a broken provider cannot erase its completed measurements or later cases.

#include <spot/tl/parse.hh>
#include <spot/twa/taatgba.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twa/twaproduct.hh>
#include <spot/twaalgos/contains.hh>
#include <spot/twaalgos/ltl2taa.hh>
#include <spot/twaalgos/translate.hh>

#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

  using clock_type = std::chrono::steady_clock;
  using fields = std::map<std::string, std::string>;

  struct options {
      std::vector<std::string> formulas;
      std::string provider = "all";
      unsigned timeout_seconds = 10;
      size_t max_memory_mib = 1024;
  };

  [[noreturn]] void fail (const std::string& message) {
    throw std::runtime_error ("acacia-spot-otf-probe: " + message);
  }

  std::string need_argument (int& index, int argc, char** argv) {
    if (++index >= argc)
      fail (std::string {argv[index - 1]} + " requires an argument");
    return argv[index];
  }

  size_t parse_size (const std::string& text, const std::string& option) {
    size_t value = 0;
    const auto parsed = std::from_chars (text.data (), text.data () + text.size (), value);
    if (text.empty () or parsed.ec != std::errc {}
        or parsed.ptr != text.data () + text.size () or value == 0)
      fail (option + " requires a positive integer");
    return value;
  }

  void usage (std::ostream& out, const char* program) {
    out << "usage: " << program << " [--formula LTL]...\n"
        << "       [--provider all|translator|taa|product]\n"
        << "       [--timeout-seconds N] [--max-memory-mib N]\n"
        << "  Without --formula, run the eight P0 formulas. Product factors are\n"
        << "  two independently translated copies of LTL (intersection = LTL).\n"
        << "  Caps apply separately to each child, including its factory.\n"
        << "  factory_peak_bytes is process RSS high-water at factory return,\n"
        << "  NOT an allocation peak; compare factory_baseline_peak_bytes.\n"
        << "  Exit 0: all checks pass; 2: provider failure/limit; 1: driver error.\n";
  }

  options parse_options (int argc, char** argv) {
    options result;
    for (int i = 1; i < argc; ++i) {
      const std::string argument = argv[i];
      if (argument == "--formula")
        result.formulas.push_back (need_argument (i, argc, argv));
      else if (argument == "--provider")
        result.provider = need_argument (i, argc, argv);
      else if (argument == "--timeout-seconds") {
        const size_t n = parse_size (need_argument (i, argc, argv), argument);
        if (n > std::numeric_limits<unsigned>::max ())
          fail (argument + " is too large");
        result.timeout_seconds = static_cast<unsigned> (n);
      }
      else if (argument == "--max-memory-mib") {
        result.max_memory_mib = parse_size (need_argument (i, argc, argv), argument);
        if (result.max_memory_mib >= std::numeric_limits<rlim_t>::max () / 1048576)
          fail (argument + " is too large");
      }
      else if (argument == "--help") {
        usage (std::cout, argv[0]);
        std::exit (0);
      }
      else
        fail ("unknown option " + argument);
    }
    if (result.provider != "all" and result.provider != "translator"
        and result.provider != "taa" and result.provider != "product")
      fail ("unknown provider " + result.provider);
    if (result.formulas.empty ())
      result.formulas = {"true", "false", "F a", "G a", "GF a", "GF a & GF b",
                         "G(a -> F b)", "(a U b) | G c"};
    return result;
  }

  std::string cell (const std::string& value) {
    std::string result;
    for (char c: value) {
      if (c == '\t') result += "\\t";
      else if (c == '\n') result += "\\n";
      else if (c == '\r') result += "\\r";
      else if (c == '\\') result += "\\\\";
      else result += c;
    }
    return result;
  }

  struct reporter {
      int fd;
      void put (const std::string& key, const std::string& value) const {
        const std::string line = key + '\t' + cell (value) + '\n';
        size_t offset = 0;
        while (offset < line.size ()) {
          const ssize_t n = write (fd, line.data () + offset, line.size () - offset);
          if (n < 0 and errno == EINTR) continue;
          if (n <= 0) fail ("cannot report child measurement");
          offset += static_cast<size_t> (n);
        }
      }
      void count (const std::string& key, size_t value) const {
        put (key, std::to_string (value));
      }
      void elapsed (const std::string& key, clock_type::time_point start) const {
        const double ms = std::chrono::duration<double, std::milli> (
            clock_type::now () - start).count ();
        std::ostringstream out;
        out << std::fixed << std::setprecision (6) << ms;
        put (key, out.str ());
      }
  };

  size_t peak_bytes () {
    rusage usage {};
    if (getrusage (RUSAGE_SELF, &usage) != 0) fail ("getrusage failed");
    return static_cast<size_t> (usage.ru_maxrss) * 1024;
  }

  std::string rss_bytes () {
    std::ifstream in {"/proc/self/statm"};
    size_t total = 0, resident = 0;
    const long page = sysconf (_SC_PAGESIZE);
    if (not (in >> total >> resident) or page <= 0) return "NA";
    return std::to_string (resident * static_cast<size_t> (page));
  }

  std::string acceptance (const spot::const_twa_ptr& aut) {
    std::ostringstream out;
    out << aut->num_sets () << ':' << aut->get_acceptance ();
    return out.str ();
  }

  // Every owner below is destroyed before its provider.  A destination retained
  // from a row survives release_iter(), including TAA's iterator-local storage.
  size_t live_states = 0, acquired_iters = 0, released_iters = 0;
  struct state_deleter {
      void operator() (const spot::state* s) const {
        s->destroy ();
        --live_states;
      }
  };
  using state_owner = std::unique_ptr<const spot::state, state_deleter>;
  state_owner own (const spot::state* s) {
    if (not s) fail ("provider returned a null state");
    ++live_states;
    return state_owner {s};
  }
  struct iterator_deleter {
      const spot::twa* aut;
      void operator() (spot::twa_succ_iterator* it) const {
        aut->release_iter (it);
        ++released_iters;
      }
  };
  using iterator_owner = std::unique_ptr<spot::twa_succ_iterator, iterator_deleter>;
  iterator_owner successors (const spot::const_twa_ptr& aut, const spot::state* s) {
    auto* it = aut->succ_iter (s);
    if (not it) fail ("provider returned a null iterator");
    ++acquired_iters;
    return iterator_owner {it, iterator_deleter {aut.get ()}};
  }

  void check_equal (const spot::state* a, const spot::state* b) {
    if (a->compare (b) != 0 or b->compare (a) != 0 or a->hash () != b->hash ())
      fail ("state compare/hash contract failed");
  }

  size_t row (const spot::const_twa_ptr& aut, const spot::state* source,
              state_owner& first_destination) {
    auto it = successors (aut, source);
    size_t edges = 0;
    for (it->first (); not it->done (); it->next ()) {
      const bdd condition = it->cond ();
      const auto marks = it->acc ();
      if ((marks - aut->acc ().all_sets ()) != spot::acc_cond::mark_t {})
        fail ("edge uses undeclared acceptance sets");
      (void) condition;
      auto dst = own (it->dst ());
      auto duplicate = own (it->dst ());
      check_equal (dst.get (), duplicate.get ());
      if (not first_destination) first_destination = std::move (dst);
      ++edges;
    }
    return edges;
  }

  void exercise_ownership (const spot::const_twa_ptr& aut) {
    auto init = own (aut->get_init_state ());
    auto clone = own (init->clone ());
    check_equal (init.get (), clone.get ());
    {
      auto it = successors (aut, init.get ());
      if (it->first ()) {
        auto dst = own (it->dst ());
        auto copy = own (dst->clone ());
        check_equal (dst.get (), copy.get ());
      } // Early exit, even if this row has more edges.
    }
    struct injected_error {};
    try {
      auto it = successors (aut, init.get ());
      state_owner dst;
      if (it->first ()) dst = own (it->dst ());
      throw injected_error {};
    }
    catch (const injected_error&) {}
    state_owner retained;
    row (aut, init.get (), retained); // Reuse after both release paths.
    if (retained) {
      auto copy = own (retained->clone ());
      check_equal (retained.get (), copy.get ());
    }
  }

  void probe (const std::string& formula,
              const std::string& provider, const std::string& phase,
              const reporter& report) {
    report.put ("stage", "parse");
    auto parsed = spot::parse_infix_psl (formula);
    std::ostringstream errors;
    if (parsed.format_errors (errors)) fail (errors.str ());
    auto dict = spot::make_bdd_dict ();
    spot::twa_graph_ptr left, right;
    if (provider == "product") {
      report.put ("stage", "factors");
      const auto start = clock_type::now ();
      left = spot::translator {dict}.run (parsed.f);
      right = spot::translator {dict}.run (parsed.f);
      report.elapsed ("factor_factory_ms", start);
      report.put ("factor_states", std::to_string (left->num_states ()) + "+"
                  + std::to_string (right->num_states ()));
    }

    report.put ("stage", "factory");
    report.count ("factory_baseline_peak_bytes", peak_bytes ());
    report.put ("factory_rss_before_bytes", rss_bytes ());
    spot::const_twa_ptr aut;
    const auto factory_start = clock_type::now ();
    if (provider == "translator")
      aut = spot::translator {dict}.run (parsed.f);
    else if (provider == "taa")
      aut = spot::ltl_to_taa (parsed.f, dict, false);
    else
      aut = spot::otf_product (left, right);
    // Sample immediately, before reporting, inspecting acceptance, or asking for
    // an initial state. RSS is process-wide, including pre-built product factors.
    const auto factory_end = clock_type::now ();
    const size_t factory_peak = peak_bytes ();
    const std::string factory_rss = rss_bytes ();
    std::ostringstream factory_ms;
    factory_ms << std::fixed << std::setprecision (6)
               << std::chrono::duration<double, std::milli> (
                      factory_end - factory_start).count ();
    report.put ("factory_ms", factory_ms.str ());
    report.count ("factory_peak_bytes", factory_peak);
    report.put ("factory_rss_after_bytes", factory_rss);
    if (auto graph = std::dynamic_pointer_cast<const spot::twa_graph> (aut))
      report.count ("states_after_factory", graph->num_states ());
    else
      report.put ("states_after_factory", "NA");
    report.put ("acceptance_before", acceptance (aut));

    if (phase == "partial") {
      report.put ("stage", "init");
      auto start = clock_type::now ();
      auto init = own (aut->get_init_state ());
      report.elapsed ("init_only_ms", start);
      report.put ("acceptance_after_init", acceptance (aut));
      report.put ("stage", "first_row");
      state_owner destination;
      start = clock_type::now ();
      const size_t first_edges = row (aut, init.get (), destination);
      report.elapsed ("first_row_ms", start);
      report.count ("first_row_edges", first_edges);
      report.put ("acceptance_after_first_row", acceptance (aut));
      if (destination) {
        report.put ("stage", "destination_row");
        state_owner unused;
        start = clock_type::now ();
        const size_t edges = row (aut, destination.get (), unused);
        report.elapsed ("one_destination_row_ms", start);
        report.count ("one_destination_row_edges", edges);
      }
      report.put ("acceptance_after_destination", acceptance (aut));
    }
    else if (phase == "full") {
      report.put ("stage", "full_enumeration");
      const auto start = clock_type::now ();
      // Deliberately materialize through Spot's public generic overload. The
      // child has wall-time/address-space caps; no truncated graph is accepted.
      auto graph = spot::make_twa_graph (aut, spot::twa::prop_set::all ());
      report.elapsed ("full_enumeration_ms", start);
      report.count ("full_enumeration_states", graph->num_states ());
      report.count ("full_enumeration_edges", graph->num_edges ());
      report.put ("acceptance_after", acceptance (aut));
      report.put ("materialized_acceptance", acceptance (graph));
      report.put ("stage", "language_check");
      auto reference = spot::translator {dict}.run (parsed.f);
      report.put ("language_check", spot::are_equivalent (graph, reference)
                  ? "pass" : "FAIL");
    }
    else {
      report.put ("stage", "ownership");
      exercise_ownership (aut);
      report.put ("ownership_acceptance_after", acceptance (aut));
    }
    if (live_states != 0 or acquired_iters != released_iters)
      fail ("consumer ownership imbalance");
    report.count ("consumer_iterators_released", released_iters);
    report.put ("stage", "teardown");
    // aut/factors/dictionary teardown occurs before the child reports success.
  }

  fields run_phase (const options& args, const std::string& formula,
                    const std::string& provider, const std::string& phase) {
    int fds[2];
    if (pipe (fds) != 0) fail ("pipe failed");
    const pid_t child = fork ();
    if (child < 0) {
      close (fds[0]);
      close (fds[1]);
      fail ("fork failed");
    }
    if (child == 0) {
      close (fds[0]);
      const reporter report {fds[1]};
      try {
        const rlim_t bytes = static_cast<rlim_t> (args.max_memory_mib) * 1048576;
        const rlimit memory {bytes, bytes}, core {0, 0};
        if (setrlimit (RLIMIT_AS, &memory) != 0 or setrlimit (RLIMIT_CORE, &core) != 0)
          fail ("setrlimit failed");
        alarm (args.timeout_seconds);
        probe (formula, provider, phase, report);
        report.put ("phase_status", "ok");
        close (fds[1]);
        _exit (0);
      }
      catch (const std::exception& error) {
        report.put ("phase_status", error.what ());
        close (fds[1]);
        _exit (1);
      }
    }
    close (fds[1]);
    std::string output;
    char buffer[4096];
    bool read_failed = false;
    for (;;) {
      const ssize_t n = read (fds[0], buffer, sizeof buffer);
      if (n < 0 and errno == EINTR) continue;
      if (n < 0) { read_failed = true; break; }
      if (n == 0) break;
      output.append (buffer, static_cast<size_t> (n));
    }
    close (fds[0]);
    int status = 0;
    while (waitpid (child, &status, 0) < 0)
      if (errno != EINTR) fail ("waitpid failed");
    if (read_failed) fail ("cannot read child measurements");
    fields result;
    std::istringstream lines {output};
    std::string line;
    while (std::getline (lines, line)) {
      const size_t tab = line.find ('\t');
      if (tab != std::string::npos)
        result[line.substr (0, tab)] = line.substr (tab + 1);
    }
    if (WIFSIGNALED (status))
      result["phase_status"] = "signal_" + std::to_string (WTERMSIG (status))
          + "@" + result["stage"];
    else if (not WIFEXITED (status) or WEXITSTATUS (status) != 0
             or not result.contains ("phase_status")) {
      if (not result.contains ("phase_status")) result["phase_status"] = "child_failed";
      result["phase_status"] += "@" + result["stage"];
    }
    return result;
  }

  std::string value (const fields& row, const std::string& key) {
    const auto found = row.find (key);
    return found == row.end () ? "NA" : found->second;
  }

}  // namespace

int main (int argc, char** argv) {
  try {
    const options arguments = parse_options (argc, argv);
    const std::vector<std::string> columns {
        "formula", "provider", "factory_ms", "factory_peak_bytes", "states_after_factory",
        "init_only_ms", "first_row_ms", "first_row_edges", "one_destination_row_ms",
        "full_enumeration_ms", "full_enumeration_states", "full_enumeration_edges",
        "acceptance_before", "acceptance_after", "acceptance_changed", "language_check", "status",
        "acceptance_after_init", "acceptance_after_first_row", "acceptance_after_destination",
        "full_acceptance_before", "materialized_acceptance", "one_destination_row_edges",
        "factory_baseline_peak_bytes", "factory_rss_before_bytes", "factory_rss_after_bytes",
        "factor_factory_ms", "factor_states", "full_factory_ms", "ownership_check",
        "consumer_iterators_released", "ownership_iterators_released"};
    for (size_t i = 0; i < columns.size (); ++i)
      std::cout << (i ? "\t" : "") << columns[i];
    std::cout << '\n' << std::flush;
    bool failed = false;
    for (const auto& formula: arguments.formulas)
      for (const std::string provider: {"translator", "taa", "product"}) {
        if (arguments.provider != "all" and arguments.provider != provider) continue;
        fields row = run_phase (arguments, formula, provider, "partial");
        const fields full = run_phase (arguments, formula, provider, "full");
        const fields ownership = run_phase (arguments, formula, provider, "ownership");
        row["formula"] = cell (formula);
        row["provider"] = provider;
        for (const std::string key: {"full_enumeration_ms", "full_enumeration_states",
             "full_enumeration_edges", "acceptance_after", "materialized_acceptance", "language_check"})
          row[key] = value (full, key);
        row["full_acceptance_before"] = value (full, "acceptance_before");
        row["full_factory_ms"] = value (full, "factory_ms");
        row["ownership_check"] = value (ownership, "phase_status");
        row["ownership_iterators_released"] = value (ownership, "consumer_iterators_released");
        const std::string before = value (row, "acceptance_before");
        bool changed = false, incomplete = before == "NA";
        for (const std::string key: {"acceptance_after_init", "acceptance_after_first_row",
             "acceptance_after_destination", "full_acceptance_before", "acceptance_after",
             "materialized_acceptance"}) {
          const auto after = value (row, key);
          incomplete |= after == "NA";
          changed |= after != "NA" and before != "NA" and after != before;
        }
        const auto ownership_after = value (ownership, "ownership_acceptance_after");
        incomplete |= ownership_after == "NA";
        changed |= ownership_after != "NA" and before != "NA" and ownership_after != before;
        row["acceptance_changed"] = changed ? "yes" : incomplete ? "unknown" : "no";
        std::string status;
        for (const auto& phase: {std::pair<const char*, const fields*> {"partial", &row},
                                 {"full", &full}, {"ownership", &ownership}})
          if (value (*phase.second, "phase_status") != "ok")
            status += std::string {phase.first} + ":" + value (*phase.second, "phase_status") + ';';
        if (row["acceptance_changed"] != "no") status += "acceptance_" + row["acceptance_changed"] + ';';
        if (row["language_check"] != "pass") status += "language_" + row["language_check"] + ';';
        failed |= not status.empty ();
        if (status.empty ()) status = "ok;";
        status += "peak_is_process_hwm";
        if (provider == "translator") status += ";materialization_is_graph_copy";
        else status += ";factory_state_count_unobservable";
        if (value (row, "first_row_edges") == "0") status += ";no_destination";
        row["status"] = status;
        for (size_t i = 0; i < columns.size (); ++i)
          std::cout << (i ? "\t" : "") << value (row, columns[i]);
        std::cout << '\n' << std::flush;
      }
    return failed ? 2 : 0;
  }
  catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
}
