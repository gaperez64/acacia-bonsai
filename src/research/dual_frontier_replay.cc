/// Offline dual-frontier experiment over existing snapshot artifacts.
///
/// `convert` measures exact dualization and round trips, `cpre` compares the
/// positive and resident/cold negative paths for captured input updates, and
/// `solve` runs a maxima-independent cyclic fixed-K driver over the complete
/// all-input action table.  No production solver dispatch is changed.

#include "research/all_input_actions.hh"
#include "research/cpre_event.hh"
#include "research/dual_rank_delta.hh"
#include "research/dual_rank_predecessor.hh"
#include <system_error>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <utility>
#include <vector>

namespace {
  using namespace acacia::research;

  enum class task_kind { convert, cpre, delta, solve };
  enum class mode_kind { positive, negative, automatic };

  struct options {
      std::filesystem::path directory;
      task_kind task = task_kind::convert;
      mode_kind mode = mode_kind::automatic;
      std::optional<std::size_t> k;
      std::optional<int> loop;
      budget_limits limits;
      std::size_t max_sweeps = 100000;
      bool header = true;
  };

  [[noreturn]] void cli_fail (const std::string& message) {
    throw std::runtime_error ("acacia-dual-frontier-replay: " + message);
  }

  std::string need_argument (int& index, int argc, char** argv) {
    if (++index >= argc)
      cli_fail (std::string {argv[index - 1]} + " requires an argument");
    return argv[index];
  }

  std::uint64_t parse_u64 (const std::string& text, const std::string& option) {
    std::uint64_t value = 0;
    const auto parsed = std::from_chars (text.data (), text.data () + text.size (), value);
    if (text.empty () or parsed.ec != std::errc {} or parsed.ptr != text.data () + text.size ())
      cli_fail (option + " requires a non-negative integer");
    return value;
  }

  std::size_t parse_size (const std::string& text, const std::string& option) {
    const std::uint64_t value = parse_u64 (text, option);
    if (value > std::numeric_limits<std::size_t>::max ())
      cli_fail (option + " is outside size_t range");
    return static_cast<std::size_t> (value);
  }

  void usage (std::ostream& out, const char* program) {
    out << "usage: " << program << " --dir DIR --task convert|cpre|delta|solve\n"
        << "       [--mode positive|negative|auto] [--k K] [--loop N] [--no-header]\n"
        << "       [--max-work N] [--max-workspace-bytes N] [--max-frontier N]\n"
        << "       [--deadline-ms N] [--max-sweeps N]\n"
        << "  convert, cpre and delta consume schema-2 cpre-<loop>.tsv events.\n"
        << "  solve consumes schema-3 meta.tsv plus all-input-actions.tsv and requires --k.\n"
        << "  A deadline of 0 disables the per-operation wall-clock deadline.\n";
  }

  options parse_options (int argc, char** argv) {
    options result;
    for (int i = 1; i < argc; ++i) {
      const std::string argument = argv[i];
      if (argument == "--dir")
        result.directory = need_argument (i, argc, argv);
      else if (argument == "--task") {
        const std::string value = need_argument (i, argc, argv);
        if (value == "convert")
          result.task = task_kind::convert;
        else if (value == "cpre")
          result.task = task_kind::cpre;
        else if (value == "delta")
          result.task = task_kind::delta;
        else if (value == "solve")
          result.task = task_kind::solve;
        else
          cli_fail ("unknown --task " + value);
      }
      else if (argument == "--mode") {
        const std::string value = need_argument (i, argc, argv);
        if (value == "positive")
          result.mode = mode_kind::positive;
        else if (value == "negative")
          result.mode = mode_kind::negative;
        else if (value == "auto")
          result.mode = mode_kind::automatic;
        else
          cli_fail ("unknown --mode " + value);
      }
      else if (argument == "--k")
        result.k = parse_size (need_argument (i, argc, argv), "--k");
      else if (argument == "--loop") {
        const std::size_t value = parse_size (need_argument (i, argc, argv), "--loop");
        if (value > static_cast<std::size_t> (std::numeric_limits<int>::max ()))
          cli_fail ("--loop is outside int range");
        result.loop = static_cast<int> (value);
      }
      else if (argument == "--max-work")
        result.limits.max_work = parse_u64 (need_argument (i, argc, argv), "--max-work");
      else if (argument == "--max-workspace-bytes")
        result.limits.max_workspace_bytes =
            parse_size (need_argument (i, argc, argv), "--max-workspace-bytes");
      else if (argument == "--max-frontier")
        result.limits.max_frontier = parse_size (need_argument (i, argc, argv), "--max-frontier");
      else if (argument == "--deadline-ms") {
        const std::uint64_t value = parse_u64 (need_argument (i, argc, argv), "--deadline-ms");
        if (value > static_cast<std::uint64_t> (
                        std::numeric_limits<std::chrono::milliseconds::rep>::max ()))
          cli_fail ("--deadline-ms is outside duration range");
        result.limits.deadline = std::chrono::milliseconds {value};
      }
      else if (argument == "--max-sweeps")
        result.max_sweeps = parse_size (need_argument (i, argc, argv), "--max-sweeps");
      else if (argument == "--no-header")
        result.header = false;
      else if (argument == "--help") {
        usage (std::cout, argv[0]);
        std::exit (0);
      }
      else
        cli_fail ("unknown option " + argument);
    }
    if (result.directory.empty ())
      cli_fail ("--dir is required");
    if (result.task == task_kind::solve and not result.k)
      cli_fail ("--task solve requires --k");
    if (result.k and (*result.k < 1 or *result.k > static_cast<std::size_t> (
                                                       std::numeric_limits<VECTOR_ELT_T>::max ())))
      cli_fail ("--k is outside the supported 1..127 range");
    return result;
  }

  std::size_t strict_meta_field (const std::filesystem::path& directory, const std::string& name) {
    std::ifstream meta {directory / "meta.tsv"};
    if (not meta)
      cli_fail ("cannot open " + (directory / "meta.tsv").string ());
    std::string header, values, extra;
    if (not std::getline (meta, header) or not std::getline (meta, values) or
        std::getline (meta, extra))
      cli_fail ("meta.tsv must contain exactly one header and one value row");
    std::istringstream hs {header}, vs {values};
    std::string h, v;
    while (hs >> h) {
      if (not(vs >> v))
        cli_fail ("meta.tsv has fewer values than columns");
      if (h == name)
        return parse_size (v, "meta.tsv field " + name);
    }
    if (vs >> v)
      cli_fail ("meta.tsv has more values than columns");
    cli_fail ("meta.tsv has no column " + name);
  }

  std::string domain_identity (const std::filesystem::path& directory) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute (directory, error);
    return (error ? directory : absolute).lexically_normal ().string ();
  }

  bool selected_event_path (const std::filesystem::path& path, const std::optional<int>& loop) {
    return not loop or path.filename () == "cpre-" + std::to_string (*loop) + ".tsv";
  }

  long long peak_rss_kib () {
    rusage usage {};
    if (::getrusage (RUSAGE_SELF, &usage) != 0)
      return -1;
#if defined(__APPLE__)
    return usage.ru_maxrss / 1024;
#else
    return usage.ru_maxrss;
#endif
  }

  struct measured_region {
      region_result result;
      std::uint64_t work = 0;
      std::size_t peak = 0;
      std::size_t peak_generators = 0;
      long long microseconds = 0;
  };

  template <typename Operation>
  measured_region measure_region (const budget_limits& limits, Operation&& operation) {
    operation_budget budget {limits};
    region_result result = operation (budget);
    return {std::move (result), budget.work (), budget.peak_workspace_bytes (),
            budget.peak_live_generators (), budget.elapsed ().count ()};
  }

  struct measured_update {
      update_result result;
      std::uint64_t work = 0;
      std::size_t peak = 0;
      std::size_t peak_generators = 0;
      long long microseconds = 0;
  };

  template <typename Operation>
  measured_update measure_update (const budget_limits& limits, Operation&& operation) {
    operation_budget budget {limits};
    update_result result = operation (budget);
    return {std::move (result), budget.work (), budget.peak_workspace_bytes (),
            budget.peak_live_generators (), budget.elapsed ().count ()};
  }

  struct measured_delta {
      exclusion_delta_result result;
      std::uint64_t work = 0;
      std::size_t peak = 0;
      std::size_t peak_generators = 0;
      long long microseconds = 0;
  };

  template <typename Operation>
  measured_delta measure_delta (const budget_limits& limits, Operation&& operation) {
    operation_budget budget {limits};
    exclusion_delta_result result = operation (budget);
    return {std::move (result), budget.work (), budget.peak_workspace_bytes (),
            budget.peak_live_generators (), budget.elapsed ().count ()};
  }

  measured_region build_region (const rank_domain& domain, frontier_form form,
                                std::vector<rank_vector> generators, const budget_limits& limits) {
    return measure_region (limits, [&] (operation_budget& budget) {
      return try_make_region (domain, form, std::move (generators), budget);
    });
  }

  std::string exact_label (const dual_rank_region& left, const dual_rank_region& right,
                           const budget_limits& limits) {
    operation_budget budget {limits};
    const std::optional<bool> equal = exact_equal (left, right, budget);
    if (not equal)
      return std::string {"validation_"} + completion_name (budget.outcome ());
    return *equal ? "yes" : "NO";
  }

  int run_convert (const options& arguments, std::size_t states, std::size_t bool_threshold,
                   const std::string& identity) {
    if (arguments.header)
      std::cout << "loop\tk\tdim\tpositive_count\tnegative_count\tpositive_bytes"
                   "\tnegative_bytes\tconversion_us\tconversion_work\tconversion_peak"
                   "\troundtrip_us\troundtrip_work\troundtrip_peak\tcompletion\texact"
                   "\tprocess_peak_rss_kib\n";
    int failures = 0;
    bool selected = false;
    for (const auto& path : find_events (arguments.directory)) {
      if (not selected_event_path (path, arguments.loop))
        continue;
      const event ev = load (path, states, bool_threshold);
      if (arguments.loop and ev.loop != *arguments.loop)
        cli_fail ("CPre filename loop disagrees with its header");
      selected = true;
      const rank_domain domain = rank_domain::fixed_box (states, ev.k, bool_threshold,
                                                         identity + "#k=" + std::to_string (ev.k));
      measured_region source =
          build_region (domain, frontier_form::max_included, ev.before, arguments.limits);
      if (not source.result) {
        ++failures;
        std::cout << ev.loop << '\t' << ev.k << '\t' << states << '\t' << ev.before.size ()
                  << "\t-\t-\t-\t-\t-\t-\t-\t-\t-\t" << completion_name (source.result.status)
                  << "\tvalidation_incomplete\t" << peak_rss_kib () << '\n';
        continue;
      }
      const dual_rank_region& positive = *source.result.region;
      measured_region converted =
          measure_region (arguments.limits, [&] (operation_budget& budget) {
            return try_convert (positive, frontier_form::min_excluded, budget);
          });
      measured_region roundtrip;
      std::string exact = "validation_incomplete";
      if (converted.result) {
        roundtrip = measure_region (arguments.limits, [&] (operation_budget& budget) {
          return try_convert (*converted.result.region, frontier_form::max_included, budget);
        });
        if (roundtrip.result)
          exact = exact_label (positive, *roundtrip.result.region, arguments.limits);
      }
      const completion final_status = not converted.result   ? converted.result.status
                                      : not roundtrip.result ? roundtrip.result.status
                                                             : completion::complete;
      if (final_status != completion::complete or exact != "yes")
        ++failures;
      std::cout << ev.loop << '\t' << ev.k << '\t' << states << '\t'
                << positive.generators ().size () << '\t'
                << (converted.result
                        ? std::to_string (converted.result.region->generators ().size ())
                        : "-")
                << '\t' << positive.stats ().accounted_bytes << '\t'
                << (converted.result
                        ? std::to_string (converted.result.region->stats ().accounted_bytes)
                        : "-")
                << '\t' << converted.microseconds << '\t' << converted.work << '\t'
                << converted.peak << '\t' << roundtrip.microseconds << '\t' << roundtrip.work
                << '\t' << roundtrip.peak << '\t' << completion_name (final_status) << '\t'
                << exact << '\t' << peak_rss_kib () << '\n';
    }
    if (arguments.loop and not selected)
      cli_fail ("no CPre event matched --loop " + std::to_string (*arguments.loop));
    return failures == 0 ? 0 : 1;
  }

  int run_cpre (const options& arguments, std::size_t states, std::size_t bool_threshold,
                const std::string& identity) {
    if (arguments.header)
      std::cout << "loop\tk\tbefore\tactions\trecorded_after\tpositive_after"
                   "\tnegative_before\tnegative_after\tpositive_us\tpositive_peak"
                   "\tconvert_us\tconvert_peak\tresident_negative_us"
                   "\tresident_negative_peak\tcold_roundtrip_us\tcold_peak"
                   "\tthreshold_candidates\tjoin_candidates\tpositive_exact"
                   "\tnegative_exact\tcompletion\tprocess_peak_rss_kib\n";
    int failures = 0;
    bool selected = false;
    for (const auto& path : find_events (arguments.directory)) {
      if (not selected_event_path (path, arguments.loop))
        continue;
      const event ev = load (path, states, bool_threshold);
      if (arguments.loop and ev.loop != *arguments.loop)
        cli_fail ("CPre filename loop disagrees with its header");
      selected = true;
      const rank_domain domain = rank_domain::fixed_box (states, ev.k, bool_threshold,
                                                         identity + "#k=" + std::to_string (ev.k));
      measured_region source =
          build_region (domain, frontier_form::max_included, ev.before, arguments.limits);
      measured_region expected =
          build_region (domain, frontier_form::max_included, ev.after, arguments.limits);
      if (not source.result or not expected.result)
        cli_fail ("captured event did not form a valid exact positive region");
      const dual_rank_region& positive = *source.result.region;

      measured_update positive_update =
          measure_update (arguments.limits, [&] (operation_budget& budget) {
            return try_input_update (positive, ev.actions, budget);
          });
      measured_region conversion =
          measure_region (arguments.limits, [&] (operation_budget& budget) {
            return try_convert (positive, frontier_form::min_excluded, budget);
          });
      measured_update negative_update;
      measured_region return_conversion;
      if (conversion.result) {
        negative_update = measure_update (arguments.limits, [&] (operation_budget& budget) {
          return try_input_update (*conversion.result.region, ev.actions, budget);
        });
        if (negative_update.result)
          return_conversion = measure_region (arguments.limits, [&] (operation_budget& budget) {
            return try_convert (*negative_update.result.region, frontier_form::max_included,
                                budget);
          });
      }

      const std::string positive_exact =
          positive_update.result ? exact_label (*positive_update.result.region,
                                                *expected.result.region, arguments.limits)
                                 : "validation_incomplete";
      const std::string negative_exact =
          negative_update.result ? exact_label (*negative_update.result.region,
                                                *expected.result.region, arguments.limits)
                                 : "validation_incomplete";
      const completion final_status = not positive_update.result   ? positive_update.result.status
                                      : not conversion.result      ? conversion.result.status
                                      : not negative_update.result ? negative_update.result.status
                                      : not return_conversion.result
                                          ? return_conversion.result.status
                                          : completion::complete;
      const bool exact = positive_exact == "yes" and negative_exact == "yes" and
                         return_conversion.result and
                         exact_label (*return_conversion.result.region, *expected.result.region,
                                      arguments.limits) == "yes";
      if (final_status != completion::complete or not exact)
        ++failures;

      const long long cold_us =
          conversion.microseconds + negative_update.microseconds + return_conversion.microseconds;
      const std::size_t cold_peak =
          std::max ({conversion.peak, negative_update.peak, return_conversion.peak});
      std::cout << ev.loop << '\t' << ev.k << '\t' << ev.before.size () << '\t'
                << ev.actions.size () << '\t' << ev.after.size () << '\t'
                << (positive_update.result
                        ? std::to_string (positive_update.result.region->generators ().size ())
                        : "-")
                << '\t'
                << (conversion.result
                        ? std::to_string (conversion.result.region->generators ().size ())
                        : "-")
                << '\t'
                << (negative_update.result
                        ? std::to_string (negative_update.result.region->generators ().size ())
                        : "-")
                << '\t' << positive_update.microseconds << '\t' << positive_update.peak << '\t'
                << conversion.microseconds << '\t' << conversion.peak << '\t'
                << negative_update.microseconds << '\t' << negative_update.peak << '\t' << cold_us
                << '\t' << cold_peak << '\t' << negative_update.result.stats.threshold_candidates
                << '\t' << negative_update.result.stats.join_candidates << '\t' << positive_exact
                << '\t' << negative_exact << '\t' << completion_name (final_status) << '\t'
                << peak_rss_kib () << '\n';
    }
    if (arguments.loop and not selected)
      cli_fail ("no CPre event matched --loop " + std::to_string (*arguments.loop));
    return failures == 0 ? 0 : 1;
  }

  int run_delta (const options& arguments, std::size_t states, std::size_t bool_threshold,
                 const std::string& identity) {
    if (arguments.header)
      std::cout << "loop\tK\tdimensions\tactions\tbefore_maxima\tafter_maxima"
                   "\tbefore_min_excluded\tafter_min_excluded\tdelta_min_excluded"
                   "\tpositive_maxima_still_generators"
                   "\tpositive_generator_survival_fraction"
                   "\tbefore_maxima_still_contained\tdelta_construction_status"
                   "\tconversion_work_before\tconversion_work_after\tdelta_work"
                   "\tdelta_peak_workspace\tconversion_us_before\tconversion_us_after"
                   "\tdelta_us\texact\tprocess_peak_rss_kib\n";

    int semantic_failures = 0;
    bool selected = false;
    for (const auto& path : find_events (arguments.directory)) {
      if (not selected_event_path (path, arguments.loop))
        continue;
      const event ev = load (path, states, bool_threshold);
      if (arguments.loop and ev.loop != *arguments.loop)
        cli_fail ("CPre filename loop disagrees with its header");
      selected = true;
      const rank_domain domain = rank_domain::fixed_box (states, ev.k, bool_threshold,
                                                         identity + "#k=" + std::to_string (ev.k));
      measured_region before =
          build_region (domain, frontier_form::max_included, ev.before, arguments.limits);
      measured_region after =
          build_region (domain, frontier_form::max_included, ev.after, arguments.limits);
      if (not before.result or not after.result) {
        const completion status = not before.result ? before.result.status : after.result.status;
        if (status == completion::invalid_input)
          ++semantic_failures;
        std::cout << ev.loop << '\t' << ev.k << '\t' << states << '\t' << ev.actions.size ()
                  << '\t' << ev.before.size () << '\t' << ev.after.size ()
                  << "\t-\t-\t-\t-\t-\t-\t" << completion_name (status)
                  << "\t0\t0\t0\t0\t0\t0\t0\tincomplete\t" << peak_rss_kib () << '\n';
        continue;
      }

      measured_region before_negative =
          measure_region (arguments.limits, [&] (operation_budget& budget) {
            return try_convert (*before.result.region, frontier_form::min_excluded, budget);
          });
      measured_region after_negative =
          measure_region (arguments.limits, [&] (operation_budget& budget) {
            return try_convert (*after.result.region, frontier_form::min_excluded, budget);
          });

      measured_delta delta;
      if (before_negative.result and after_negative.result)
        delta = measure_delta (arguments.limits, [&] (operation_budget& budget) {
          return try_exclusion_delta (*before.result.region, *after.result.region,
                                      *before_negative.result.region,
                                      *after_negative.result.region, budget);
        });

      const completion status = not before_negative.result ? before_negative.result.status
                                : not after_negative.result ? after_negative.result.status
                                : not delta.result          ? delta.result.status
                                                            : completion::complete;
      const bool exact = delta.result and delta.result.exact;
      if (status == completion::invalid_input or (status == completion::complete and not exact))
        ++semantic_failures;

      const auto count_or_dash = [] (const measured_region& measured) {
        return measured.result
                   ? std::to_string (measured.result.region->generators ().size ())
                   : std::string {"-"};
      };
      const std::string delta_count =
          delta.result ? std::to_string (delta.result.generators.size ()) : "-";
      const std::string stable_count =
          delta.result
              ? std::to_string (delta.result.stats.positive_maxima_still_generators)
              : "-";
      const std::string contained_count =
          delta.result ? std::to_string (delta.result.stats.before_maxima_still_contained) : "-";
      std::ostringstream survival;
      if (delta.result) {
        const double fraction = before.result.region->generators ().empty ()
                                    ? 1.0
                                    : static_cast<double> (
                                          delta.result.stats.positive_maxima_still_generators) /
                                          static_cast<double> (
                                              before.result.region->generators ().size ());
        survival << std::fixed << std::setprecision (6) << fraction;
      }
      else
        survival << '-';

      std::cout << ev.loop << '\t' << ev.k << '\t' << states << '\t' << ev.actions.size ()
                << '\t' << before.result.region->generators ().size () << '\t'
                << after.result.region->generators ().size () << '\t'
                << count_or_dash (before_negative) << '\t' << count_or_dash (after_negative)
                << '\t' << delta_count << '\t' << stable_count << '\t' << survival.str () << '\t'
                << contained_count << '\t' << completion_name (status) << '\t'
                << before_negative.work << '\t' << after_negative.work << '\t' << delta.work
                << '\t' << delta.peak << '\t' << before_negative.microseconds << '\t'
                << after_negative.microseconds << '\t' << delta.microseconds << '\t'
                << (exact ? "yes" : status == completion::complete ? "NO" : "incomplete") << '\t'
                << peak_rss_kib () << '\n';
    }
    if (arguments.loop and not selected)
      cli_fail ("no CPre event matched --loop " + std::to_string (*arguments.loop));
    return semantic_failures == 0 ? 0 : 1;
  }

  const char* mode_name (mode_kind mode) {
    switch (mode) {
      case mode_kind::positive: return "positive";
      case mode_kind::negative: return "negative";
      case mode_kind::automatic: return "auto";
    }
    return "auto";
  }

  std::uint64_t saturated_multiply (std::uint64_t left, std::uint64_t right);

  std::uint64_t saturated_add (std::uint64_t left, std::uint64_t right) {
    return right > std::numeric_limits<std::uint64_t>::max () - left
               ? std::numeric_limits<std::uint64_t>::max ()
               : left + right;
  }

  std::uint64_t structural_cost (frontier_form form, std::size_t generators,
                                 const std::vector<action_vec>& actions, std::size_t dimensions) {
    std::uint64_t transitions = 0;
    for (const auto& action : actions)
      for (const auto& row : action)
        transitions = saturated_add (transitions, row.size ());
    const std::uint64_t count = std::max<std::size_t> (1, generators);
    if (form == frontier_form::max_included)
      return saturated_multiply (count, std::max<std::uint64_t> (1, transitions));
    // Negative service always includes overflow thresholds; each exclusion may
    // additionally intersect up to one threshold per destination.
    const std::uint64_t base =
        std::max<std::uint64_t> (1, saturated_add (dimensions, transitions));
    return saturated_add (base, saturated_multiply (count, base));
  }

  std::uint64_t saturated_multiply (std::uint64_t left, std::uint64_t right) {
    return left != 0 and right > std::numeric_limits<std::uint64_t>::max () / left
               ? std::numeric_limits<std::uint64_t>::max ()
               : left * right;
  }

  std::optional<budget_limits> remaining_limits (const budget_limits& original,
                                                 std::uint64_t spent_work,
                                                 long long spent_microseconds) {
    budget_limits remaining = original;
    if (spent_work > original.max_work)
      return std::nullopt;
    remaining.max_work -= spent_work;
    if (original.deadline.count () > 0) {
      const auto elapsed = std::chrono::microseconds {spent_microseconds};
      const auto deadline =
          std::chrono::duration_cast<std::chrono::microseconds> (original.deadline);
      if (elapsed >= deadline)
        return std::nullopt;
      remaining.deadline =
          std::chrono::duration_cast<std::chrono::milliseconds> (deadline - elapsed);
      if (remaining.deadline.count () == 0)
        remaining.deadline = std::chrono::milliseconds {1};
    }
    return remaining;
  }

  struct solve_result {
      std::string status;
      completion resource = completion::complete;
      frontier_form final_form = frontier_form::max_included;
      std::size_t final_generators = 0;
      std::size_t sweeps = 0;
      std::size_t updates = 0;
      std::size_t conversions = 0;
      std::size_t failed_probes = 0;
      std::uint64_t probe_work = 0;
      std::uint64_t update_work = 0;
      long long update_us = 0;
      long long probe_us = 0;
  };

  solve_result solve_fixed_k (const options& arguments, const rank_domain& domain,
                              const input_action_table& table, std::size_t init_state) {
    const frontier_form initial_form = arguments.mode == mode_kind::negative
                                           ? frontier_form::min_excluded
                                           : frontier_form::max_included;
    std::vector<rank_vector> initial_generators;
    if (initial_form == frontier_form::max_included)
      initial_generators.push_back (domain.top ());
    measured_region built =
        build_region (domain, initial_form, std::move (initial_generators), arguments.limits);
    if (not built.result)
      return {"resource_limit", built.result.status, initial_form};
    dual_rank_region region = std::move (*built.result.region);
    const rank_vector initial = initial_vector (domain.dimensions (), init_state);

    solve_result result;
    result.final_form = region.form ();
    std::size_t cooldown = 0;
    std::size_t probes = 0;
    std::size_t next_growth_mark = 16;
    constexpr std::size_t max_probes = 8;
    const std::uint64_t max_probe_work =
        std::min<std::uint64_t> (arguments.limits.max_work, 10000000);

    for (std::size_t sweep = 0; sweep < arguments.max_sweeps; ++sweep) {
      bool changed_in_sweep = false;
      for (std::size_t input = 0; input < table.actions.size (); ++input) {
        if (arguments.mode == mode_kind::automatic and probes < max_probes and
            result.probe_work < max_probe_work and cooldown == 0 and
            (result.updates == 0 or input == 0 or
             region.generators ().size () >= next_growth_mark)) {
          ++probes;
          measured_region alternate =
              measure_region (arguments.limits, [&] (operation_budget& budget) {
                const frontier_form target = region.form () == frontier_form::max_included
                                                 ? frontier_form::min_excluded
                                                 : frontier_form::max_included;
                return try_convert (region, target, budget);
              });
          result.probe_work = saturated_add (result.probe_work, alternate.work);
          result.probe_us += alternate.microseconds;
          if (alternate.result) {
            const std::uint64_t current_cost =
                structural_cost (region.form (), region.generators ().size (),
                                 table.actions[input], domain.dimensions ());
            const std::uint64_t alternate_cost = structural_cost (
                alternate.result.region->form (), alternate.result.region->generators ().size (),
                table.actions[input], domain.dimensions ());
            const bool service_gain = alternate_cost <= current_cost / 2;
            const bool storage_gain = alternate.result.region->stats ().accounted_bytes <=
                                          region.stats ().accounted_bytes / 4 and
                                      alternate_cost <= current_cost;
            const std::uint64_t expected_updates =
                std::max<std::uint64_t> (1, saturated_multiply (2, table.input_count ()));
            const std::uint64_t saving =
                current_cost > alternate_cost ? current_cost - alternate_cost : 0;
            const bool repays = alternate.work <= saturated_multiply (saving, expected_updates);
            if ((service_gain or storage_gain) and repays) {
              region = std::move (*alternate.result.region);
              ++result.conversions;
              cooldown = std::max<std::size_t> (8, table.input_count ());
            }
          }
          else
            ++result.failed_probes;
          while (region.generators ().size () >= next_growth_mark and
                 next_growth_mark <= std::numeric_limits<std::size_t>::max () / 4)
            next_growth_mark *= 4;
        }

        measured_update update = measure_update (arguments.limits, [&] (operation_budget& budget) {
          return try_input_update (region, table.actions[input], budget);
        });
        result.update_work = saturated_add (result.update_work, update.work);
        result.update_us += update.microseconds;
        if (not update.result) {
          // Auto may retry through the other exact orientation, but the
          // discarded native attempt, conversion and retry share this input's
          // original work/deadline allowance.  Nothing is committed until the
          // alternate update has completed.
          bool recovered = false;
          if (arguments.mode == mode_kind::automatic and
              update.result.status != completion::invalid_input) {
            auto remaining = remaining_limits (arguments.limits, update.work, update.microseconds);
            if (remaining) {
              measured_region alternate =
                  measure_region (*remaining, [&] (operation_budget& budget) {
                    const frontier_form target = region.form () == frontier_form::max_included
                                                     ? frontier_form::min_excluded
                                                     : frontier_form::max_included;
                    return try_convert (region, target, budget);
                  });
              result.update_work = saturated_add (result.update_work, alternate.work);
              result.update_us += alternate.microseconds;
              remaining =
                  remaining_limits (arguments.limits, saturated_add (update.work, alternate.work),
                                    update.microseconds + alternate.microseconds);
              if (alternate.result and remaining) {
                measured_update retry =
                    measure_update (*remaining, [&] (operation_budget& budget) {
                      return try_input_update (*alternate.result.region, table.actions[input],
                                               budget);
                    });
                result.update_work = saturated_add (result.update_work, retry.work);
                result.update_us += retry.microseconds;
                if (retry.result) {
                  changed_in_sweep = changed_in_sweep or retry.result.semantic_changed;
                  region = std::move (*retry.result.region);
                  ++result.conversions;
                  cooldown = std::max<std::size_t> (8, table.input_count ());
                  recovered = true;
                }
                else
                  result.resource = retry.result.status;
              }
              else if (not alternate.result)
                result.resource = alternate.result.status;
            }
          }
          if (not recovered) {
            result.status = "resource_limit";
            if (result.resource == completion::complete)
              result.resource = update.result.status;
            result.final_form = region.form ();
            result.final_generators = region.generators ().size ();
            result.sweeps = sweep;
            return result;
          }
        }
        else {
          changed_in_sweep = changed_in_sweep or update.result.semantic_changed;
          region = std::move (*update.result.region);
        }
        ++result.updates;
        if (cooldown > 0)
          --cooldown;
        if (not region.contains (initial)) {
          result.status = "lose_k";
          result.final_form = region.form ();
          result.final_generators = region.generators ().size ();
          result.sweeps = sweep + 1;
          return result;
        }
      }
      result.sweeps = sweep + 1;
      if (not changed_in_sweep) {
        result.status = "win_k";
        result.final_form = region.form ();
        result.final_generators = region.generators ().size ();
        return result;
      }
    }
    result.status = "resource_limit";
    result.resource = completion::work_limit;
    result.final_form = region.form ();
    result.final_generators = region.generators ().size ();
    return result;
  }

  int run_solve (const options& arguments, std::size_t states, std::size_t bool_threshold,
                 const std::string& identity) {
    const std::size_t schema = strict_meta_field (arguments.directory, "schema_version");
    if (schema != 3)
      cli_fail ("--task solve requires meta.tsv schema_version 3");
    const std::size_t init_state = strict_meta_field (arguments.directory, "init_state");
    if (bool_threshold > states or init_state >= states)
      cli_fail ("invalid state metadata");
    const input_action_table table =
        load_input_actions (arguments.directory / "all-input-actions.tsv", states);
    const rank_domain domain =
        rank_domain::fixed_box (states, static_cast<int> (*arguments.k), bool_threshold,
                                identity + "#k=" + std::to_string (*arguments.k));
    const solve_result result = solve_fixed_k (arguments, domain, table, init_state);
    if (arguments.header)
      std::cout << "status\tmode\tk\tinputs\tactions\tsweeps\tupdates\tconversions"
                   "\tfailed_probes\tprobe_work\tupdate_work\tprobe_us\tupdate_us"
                   "\tfinal_form\tfinal_generators\tresource_outcome"
                   "\tprocess_peak_rss_kib\n";
    std::cout << result.status << '\t' << mode_name (arguments.mode) << '\t' << *arguments.k
              << '\t' << table.input_count () << '\t' << table.action_count () << '\t'
              << result.sweeps << '\t' << result.updates << '\t' << result.conversions << '\t'
              << result.failed_probes << '\t' << result.probe_work << '\t' << result.update_work
              << '\t' << result.probe_us << '\t' << result.update_us << '\t'
              << frontier_form_name (result.final_form) << '\t' << result.final_generators << '\t'
              << completion_name (result.resource) << '\t' << peak_rss_kib () << '\n';
    return result.status == "resource_limit" ? 2 : 0;
  }
}

int main (int argc, char** argv) {
  try {
    const options arguments = parse_options (argc, argv);
    const std::size_t states = strict_meta_field (arguments.directory, "states");
    const std::size_t bool_threshold = strict_meta_field (arguments.directory, "bool_threshold");
    if (states == 0 or bool_threshold > states)
      cli_fail ("invalid states/Boolean split in meta.tsv");
    const std::string identity = domain_identity (arguments.directory);
    switch (arguments.task) {
      case task_kind::convert: return run_convert (arguments, states, bool_threshold, identity);
      case task_kind::cpre: return run_cpre (arguments, states, bool_threshold, identity);
      case task_kind::delta: return run_delta (arguments, states, bool_threshold, identity);
      case task_kind::solve: return run_solve (arguments, states, bool_threshold, identity);
    }
  } catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
  return 1;
}
