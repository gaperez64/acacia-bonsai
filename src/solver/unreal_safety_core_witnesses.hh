#pragma once

#include "solver/unreal_weakening_records.hh"

#include <algorithm>
#include <cstddef>
#include <map>
#include <ranges>
#include <set>
#include <spot/tl/formula.hh>
#include <utility>
#ifdef __linux__
# include <sys/prctl.h>
#endif
#include <vector>

namespace acacia::unreal_witnesses {

  struct census {
      const char* reason = "eligible";
      size_t oversized_globals = 0, largest_global_conjunction_size = 0;
      size_t safety_conjuncts = 0, obligations = 0;
  };

  // A large G(a & b & ...) becomes a generalized-Buchi translation with one
  // acceptance set per liveness conjunct.  Spot is intentionally compiled
  // with a finite acceptance-set limit, so build a few sound, much smaller
  // unrealizability witnesses.  Each witness is safety_core & obligation, a
  // logical consequence of the original specification: proving one
  // unrealizable proves the original unrealizable, while an inconclusive
  // witness changes no verdict.
  inline std::vector<spot::formula> make_safety_core_witnesses (const spot::formula& formula,
                                                                size_t max_witnesses = 8,
                                                                census* observed = nullptr) {
    if (observed)
      *observed = {};
    if (max_witnesses == 0 or not formula.is (spot::op::And)) {
      if (observed)
        observed->reason = max_witnesses == 0 ? "zero_allowance" : "not_top_level_and";
      return {};
    }

    std::vector<spot::formula> conjuncts;
    bool has_oversized_global_conjunction = false;
    for (spot::formula conjunct : formula) {
      if (conjunct.is (spot::op::G) and conjunct[0].is (spot::op::And)) {
        has_oversized_global_conjunction =
            has_oversized_global_conjunction or conjunct[0].size () > 64;
        if (observed) {
          observed->largest_global_conjunction_size =
              std::max<size_t> (observed->largest_global_conjunction_size, conjunct[0].size ());
          if (conjunct[0].size () > 64)
            ++observed->oversized_globals;
        }
        for (spot::formula nested : conjunct[0])
          conjuncts.push_back (spot::formula::G (nested));
      }
      else {
        conjuncts.push_back (conjunct);
      }
    }
    if (not has_oversized_global_conjunction and not observed)
      return {};

    std::vector<spot::formula> safety_core;
    std::vector<spot::formula> obligations;
    for (spot::formula conjunct : conjuncts)
      if (conjunct.is_syntactic_safety ())
        safety_core.push_back (conjunct);
      else
        obligations.push_back (conjunct);
    if (observed) {
      observed->safety_conjuncts = safety_core.size ();
      observed->obligations = obligations.size ();
      observed->reason = observed->largest_global_conjunction_size == 0 ? "no_global_conjunction"
                         : not has_oversized_global_conjunction
                             ? "global_conjunction_threshold_not_met"
                         : safety_core.empty () ? "absent_safety_conjuncts"
                         : obligations.empty () ? "no_non_safety_obligations"
                                                : "eligible";
    }
    if (not has_oversized_global_conjunction or safety_core.empty () or obligations.empty ())
      return {};

    std::vector<spot::formula> witnesses;
    witnesses.reserve (std::min (max_witnesses, obligations.size ()));
    for (spot::formula obligation : obligations) {
      std::vector<spot::formula> parts = safety_core;
      parts.push_back (obligation);
      witnesses.push_back (spot::formula::And (std::move (parts)));
      if (witnesses.size () == max_witnesses)
        break;
    }
    return witnesses;
  }

  template <typename Runner>
  std::optional<bool> try_safety_core_witnesses (const spot::formula& formula, bool unreal,
                                                 bool synthesis, Runner& runner,
                                                 const records& observed) {
    census info;
    if (synthesis || !unreal) {
      info.reason = synthesis ? "synthesis_requested" : "not_unreal_worker";
      observed.eligibility (info.reason, 0, 0, 0, 0, 0);
      observed.finish ("ineligible");
      return std::nullopt;
    }
    auto witnesses = make_safety_core_witnesses (formula, 8, observed ? &info : nullptr);
    observed.eligibility (info.reason, info.oversized_globals,
                          info.largest_global_conjunction_size, info.safety_conjuncts,
                          info.obligations, witnesses.size ());
    if (observed)
      for (unsigned i = 0; i < witnesses.size (); ++i)
        observed.generated (witnesses[i], i);
    try {
      for (unsigned i = 0; i < witnesses.size (); ++i) {
        diagnostics::scoped_attempt diag_attempt;
        records::attempt attempt (observed, i);
        const bool result = runner (witnesses[i]);
        attempt.finish (result);
        if (result) {
          diag_attempt.commit ();
          observed.finish ("proof");
          return std::optional<bool> {true};
        }
      }
    } catch (...) {
      observed.finish ("exception");
      throw;
    }
    observed.finish (witnesses.empty () ? "ineligible" : "inconclusive");
    return std::nullopt;
  }

  struct source_binding {
      std::string source, format, semantics, target, effective_target, normalization;
      std::vector<std::string> inputs, outputs;
      std::string source_sha256;
      bool operator== (const source_binding&) const = default;
      uint64_t hash () const {
        std::string text;
        const auto add = [&] (const std::string& s) {
          text += std::to_string (s.size ()) + ':' + s;
        };
        for (const auto& s : {source, format, semantics, target, effective_target, normalization})
          add (s);
        add (source_sha256);
        for (const auto& partition : {inputs, outputs}) {
          add (std::to_string (partition.size ()));
          for (const auto& ap : partition)
            add (ap);
        }
        return binding_hash (text);
      }
  };

  // Fixed global structural bounds, never chosen using a corpus or verdict:
  // eight incumbent-order singletons, four dependency groups of at most four,
  // 16384 unique AST nodes, 32768 AST edges/support memberships, 8192 guarantees.
  // The caps bound planning as well as search; hitting one safely declines.
  inline constexpr size_t singleton_limit = 8, group_limit = 4, group_size_limit = 4;
  inline constexpr size_t ast_node_limit = 16384, ast_edge_limit = 32768;
  inline constexpr size_t support_limit = 32768, guarantee_limit = 8192;

  inline bool bounded_structure (spot::formula f, uint64_t deadline = 0) {
    std::set<spot::formula> seen;
    std::vector<spot::formula> todo {f};
    size_t edges = 0;
    while (!todo.empty ()) {
      if (deadline && phase_clock (CLOCK_MONOTONIC) >= deadline)
        return false;
      auto node = todo.back ();
      todo.pop_back ();
      if (!seen.insert (node).second)
        continue;
      if (seen.size () > ast_node_limit || (edges += node.size ()) > ast_edge_limit)
        return false;
      for (auto child : node)
        todo.push_back (child);
    }
    return true;
  }

  struct frame {
      spot::formula parent;
      size_t child;
      bool operator== (const frame&) const = default;
  };
  struct plan {
      spot::formula original, target;
      source_binding source;
      std::vector<frame> frames;
      std::vector<spot::formula> guarantees, core;
      bool legacy = false;
      const char* reason = "eligible";
  };

  inline std::vector<spot::formula> conjuncts (spot::formula f, uint64_t deadline = 0) {
    std::vector<spot::formula> result;
    for (auto c : f) {
      if (deadline && phase_clock (CLOCK_MONOTONIC) >= deadline)
        return {};
      if (c.is (spot::op::G) && c[0].is (spot::op::And))
        for (auto nested : c[0])
          result.push_back (spot::formula::G (nested));
      else
        result.push_back (c);
    }
    return result;
  }

  inline plan make_plan (spot::formula original, const source_binding& source,
                         uint64_t deadline = 0) {
    plan p;
    p.original = original;
    p.source = source;
    if (!bounded_structure (original, deadline)) {
      p.reason = "planning_structure_limit";
      return p;
    }
    if (source.format == "tlsf" && source.normalization != source.source) {
      p.reason = "missing_exact_normalization";
      return p;
    }
    auto f = original;
    bool scoped_guarantees = false;
    // Only positive contexts: keep implication antecedents and every sibling
    // verbatim. G and F are monotone; keep their scope around the selected
    // conjunction. Only G distributes over AND, never F.
    // Never traverse negation, disjunction, X/U or implication antecedents.
    while (true) {
      if (deadline && phase_clock (CLOCK_MONOTONIC) >= deadline) {
        p.reason = "planning_budget";
        return p;
      }
      if (f.is (spot::op::Implies)) {
        p.frames.push_back ({f, 1});
        f = f[1];
        continue;
      }
      if ((f.is (spot::op::G) && !f[0].is (spot::op::And)) || f.is (spot::op::F)) {
        p.frames.push_back ({f, 0});
        scoped_guarantees = true;
        f = f[0];
        continue;
      }
      if (!f.is (spot::op::And))
        break;
      // Without TLSF normalization provenance, delete direct conjuncts at
      // this boundary; retain/delete a conditional guarantee as a whole.
      // Inside G/F the terms are scoped guarantees, including conditional
      // guarantees kept/deleted as a whole. The enclosing antecedents stay exact.
      if (source.format != "tlsf" || scoped_guarantees)
        break;
      size_t implications = 0, index = 0;
      for (size_t i = 0; i < f.size (); ++i)
        if (f[i].is (spot::op::Implies)) {
          ++implications;
          index = i;
        }
      // Multiple conditional children do not expose a unique normalization
      // spine. Decline rather than deleting a subtree that may bind assumptions.
      if (implications > 1) {
        p.reason = "ambiguous_normalized_context";
        return p;
      }
      if (implications == 0)
        break;
      p.frames.push_back ({f, index});
      f = f[index];
    }
    // Strict normalization can contain ASSERT W !REQUIRE. This first extension
    // declines that context, instead of treating strict assumptions as plain A.
    // Strict objectives without that context use their exact normalized AST.
    if (source.format == "tlsf" && source.semantics.find ("Strict") != std::string::npos) {
      const auto has_weak_until = [deadline] (spot::formula root) {
        std::set<spot::formula> seen;
        std::vector<spot::formula> todo {root};
        while (!todo.empty ()) {
          if (deadline && phase_clock (CLOCK_MONOTONIC) >= deadline)
            return true;
          auto n = todo.back ();
          todo.pop_back ();
          if (!seen.insert (n).second)
            continue;
          if (n.is (spot::op::W))
            return true;
          for (auto c : n)
            todo.push_back (c);
        }
        return false;
      };
      if (has_weak_until (original)) {
        p.reason = "strict_weak_until_context";
        return p;
      }
    }
    if (!f.is (spot::op::And) && !(f.is (spot::op::G) && f[0].is (spot::op::And))) {
      p.reason = "no_guarantee_conjunction";
      return p;
    }
    p.target = f;
    auto parts = f.is (spot::op::G) ? std::vector<spot::formula> {} : conjuncts (f, deadline);
    // And({G(...)}) canonicalizes to G(...), so distribute explicitly here.
    if (f.is (spot::op::G)) {
      parts.clear ();
      for (auto c : f[0])
        parts.push_back (spot::formula::G (c));
    }
    if (p.frames.empty () && original.is (spot::op::And)) {
      // Same incumbent eligibility, classified without constructing a witness.
      bool oversized = false;
      for (auto c : original)
        oversized =
            oversized || (c.is (spot::op::G) && c[0].is (spot::op::And) && c[0].size () > 64);
      const bool safety =
          std::ranges::any_of (parts, [] (auto c) { return c.is_syntactic_safety (); });
      const bool obligation =
          std::ranges::any_of (parts, [] (auto c) { return !c.is_syntactic_safety (); });
      p.legacy = oversized && safety && obligation;
    }
    for (auto c : parts)
      if (p.legacy && c.is_syntactic_safety ())
        p.core.push_back (c);
      else
        p.guarantees.push_back (c);
    if (parts.size () > guarantee_limit || p.guarantees.size () < 2) {
      p.reason = parts.size () > guarantee_limit ? "guarantee_limit" : "no_proper_subset";
      p.guarantees.clear ();
    }
    return p;
  }

  struct derivation {
      plan premise;
      std::vector<size_t> kept;
      spot::formula objective;
  };

  inline spot::formula derive (const plan& p, const std::vector<size_t>& kept,
                               uint64_t deadline = 0) {
    if (kept.empty () || kept.size () >= p.guarantees.size ())
      return {};
    std::vector<spot::formula> parts = p.core;
    size_t previous = 0;
    for (size_t k = 0; k < kept.size (); ++k) {
      const auto j = kept[k];
      if (j >= p.guarantees.size () || (k && j <= previous))
        return {};
      previous = j;
      parts.push_back (p.guarantees[j]);
    }
    auto result = spot::formula::And (std::move (parts));
    for (auto it = p.frames.rbegin (); it != p.frames.rend (); ++it) {
      if (deadline && phase_clock (CLOCK_MONOTONIC) >= deadline)
        return {};
      if (it->parent.is (spot::op::Implies))
        result = spot::formula::Implies (it->parent[0], result);
      else if (it->parent.is (spot::op::G))
        result = spot::formula::G (result);
      else if (it->parent.is (spot::op::F))
        result = spot::formula::F (result);
      else if (it->parent.is (spot::op::And)) {
        std::vector<spot::formula> children;
        for (size_t i = 0; i < it->parent.size (); ++i)
          children.push_back (i == it->child ? result : it->parent[i]);
        result = spot::formula::And (std::move (children));
      }
      else
        return {};
    }
    return result;
  }

  inline bool verify_derivation (const derivation& d, spot::formula original,
                                 const source_binding& source, uint64_t deadline = 0) {
    if (d.premise.original != original || d.premise.source != source)
      return false;
    const auto replay = make_plan (original, source, deadline);
    if (std::strcmp (replay.reason, "eligible") != 0 || replay.target != d.premise.target ||
        replay.frames != d.premise.frames || replay.guarantees != d.premise.guarantees ||
        replay.core != d.premise.core || replay.legacy != d.premise.legacy)
      return false;
    const auto expected = derive (replay, d.kept, deadline);
    return expected && expected == d.objective;
  }

  inline bool accept (const derivation& d, spot::formula original, const source_binding& source,
                      const checked_game& proof, uint64_t deadline = 0) {
    return verify_derivation (d, original, source, deadline) && proof.valid () &&
           proof.objective == binding_hash (spot::str_psl (d.objective)) &&
           proof.context == source.hash ();
  }

  // Inverted AP support in guarantee order. For each anchor, take the first
  // following guarantees sharing any AP, up to four. Deduplicate groups and
  // stop after four; no pair enumeration, lexical AP ranking or name dispatch.
  inline std::optional<std::vector<std::vector<size_t>>> dependency_groups (
      const plan& p, uint64_t deadline = 0) {
    std::map<spot::formula, std::vector<size_t>> inverted;
    std::vector<std::vector<spot::formula>> support (p.guarantees.size ());
    size_t memberships = 0, visits = 0;
    for (size_t i = 0; i < p.guarantees.size (); ++i) {
      std::set<spot::formula> seen;
      std::vector<spot::formula> todo {p.guarantees[i]};
      while (!todo.empty ()) {
        auto f = todo.back ();
        todo.pop_back ();
        if (deadline && phase_clock (CLOCK_MONOTONIC) >= deadline)
          return std::nullopt;
        if (++visits > ast_edge_limit)
          return std::nullopt;
        if (!seen.insert (f).second)
          continue;
        if (f.is (spot::op::ap)) {
          if (++memberships > support_limit)
            return std::nullopt;
          support[i].push_back (f);
          inverted[f].push_back (i);
        }
        for (auto c : f)
          todo.push_back (c);
      }
    }
    std::vector<std::vector<size_t>> groups;
    for (size_t i = 0; i < p.guarantees.size () && groups.size () < group_limit; ++i) {
      if (deadline && phase_clock (CLOCK_MONOTONIC) >= deadline)
        return std::nullopt;
      std::set<size_t> neighbors;
      for (auto ap : support[i]) {
        const auto& indices = inverted[ap];
        auto at = std::upper_bound (indices.begin (), indices.end (), i);
        for (size_t j = 0; at != indices.end () && j < group_size_limit - 1; ++at, ++j)
          neighbors.insert (*at);
      }
      std::vector<size_t> group {i};
      for (auto neighbor : neighbors) {
        if (group.size () == group_size_limit)
          break;
        group.push_back (neighbor);
      }
      // A full support component is not a proper subset. Keep a smaller
      // structural prefix when there are more than two guarantees.
      if (group.size () == p.guarantees.size ())
        group.pop_back ();
      if (group.size () > 1 && std::ranges::find (groups, group) == groups.end ())
        groups.push_back (std::move (group));
    }
    return groups;
  }

  struct attempt_result {
      checked_game proof;
      bool result = false;
      uint64_t finished_ns = 0;
      enum { inconclusive, completed, cancelled, exception, unavailable } state = inconclusive;
      rusage usage {};
  };

  template <typename Runner>
  attempt_result bounded_attempt (const derivation& d, Runner& runner, uint64_t deadline) {
    static_assert (sizeof (attempt_result) <= PIPE_BUF);
    attempt_result result;
    if (phase_clock (CLOCK_MONOTONIC) >= deadline) {
      result.state = attempt_result::cancelled;
      return result;
    }
    int channel[2];
    if (pipe (channel) != 0) {
      result.state = attempt_result::unavailable;
      return result;
    }
    const auto owner = getpid ();
    const auto pid = fork ();
    if (pid == 0) {
      close (channel[0]);
      // Stay in the existing worker process group and resource scope. The
      // portfolio owner kills the whole group on invocation cancellation.
#ifdef __linux__
      prctl (PR_SET_PDEATHSIG, SIGKILL);
      if (getppid () != owner)
        _exit (2);
#endif
      signal (SIGTERM, SIG_DFL);
      signal (SIGINT, SIG_DFL);
      // Only the supervisor owns attempt telemetry, including killed children.
      phase_record_process_state ().write_fd = -1;
      phase_record_process_state ().enabled = false;
      active_worker_record () = nullptr;
      checked_game proof;
      proof.objective = binding_hash (spot::str_psl (d.objective));
      proof.context = d.premise.source.hash ();
      checking_candidate = &proof;
      try {
        result.result = runner (d.objective);
        result.proof = proof;
        result.state = attempt_result::completed;
      } catch (...) {
        result.state = attempt_result::exception;
      }
      result.finished_ns = phase_clock (CLOCK_MONOTONIC);
      // One small packet, never a certificate or a verdict based on exit status.
      const auto sent = write (channel[1], &result, sizeof result);
      _exit (sent == sizeof result ? 0 : 2);
    }
    close (channel[1]);
    if (pid < 0) {
      close (channel[0]);
      result.state = attempt_result::unavailable;
      return result;
    }
    int status = 0;
    rusage usage {};
    bool cancelled = false;
    while (true) {
      const auto reaped = wait4 (pid, &status, WNOHANG, &usage);
      if (reaped == pid)
        break;
      if (reaped < 0 && errno != EINTR)
        break;
      const auto now = phase_clock (CLOCK_MONOTONIC);
      if (now >= deadline) {
        cancelled = true;
        kill (pid, SIGKILL);
        while (wait4 (pid, &status, 0, &usage) < 0 && errno == EINTR) {
        }
        break;
      }
      // Global polling granularity: at most 1 ms, shortened near the deadline.
      const auto pause_ns = std::min<uint64_t> (1000000, deadline - now);
      timespec pause {0, long (pause_ns)};
      nanosleep (&pause, nullptr);
    }
    if (cancelled)
      result.state = attempt_result::cancelled;
    else if (WIFEXITED (status) && WEXITSTATUS (status) == 0) {
      attempt_result packet;
      if (read (channel[0], &packet, sizeof packet) == sizeof packet)
        result = packet;
      if (result.finished_ns > deadline) {
        result.state = attempt_result::cancelled;
        result.result = false;
      }
    }
    close (channel[0]);
    result.usage = usage;
    return result;
  }

  template <typename Runner>
  std::optional<bool> try_extended_witnesses (spot::formula original, const source_binding& source,
                                              bool enabled, Runner& runner,
                                              const records& observed, uint64_t deadline,
                                              const allowances& options = {}) {
    const auto entry = phase_clock (CLOCK_MONOTONIC);
    const auto [until, per_attempt] = make_budget (entry, deadline, options);
    observed.extended_limits (until, per_attempt, options);
    if (options.attempt_ms == 0 || options.total_ms == 0) {
      observed.eligibility ("zero_allowance", 0, 0, 0, 0, 0);
      observed.finish ("ineligible");
      return std::nullopt;
    }
    auto p = enabled ? make_plan (original, source, until) : plan {};
    if (!enabled)
      p.reason = "unsupported_verified_route";
    if (std::strcmp (p.reason, "eligible") != 0 || p.guarantees.empty ()) {
      observed.eligibility (p.reason, 0, 0, p.core.size (), p.guarantees.size (), 0);
      observed.finish ("ineligible");
      return std::nullopt;
    }
    // Generation is lazy. The final eligibility record reports actual emitted
    // candidates, not a planned list that might never be constructed.
    unsigned generated = 0;
    const auto attempt = [&] (std::vector<size_t> kept) {
      if (phase_clock (CLOCK_MONOTONIC) >= until || !per_attempt)
        return false;
      derivation d {p, std::move (kept), {}};
      d.objective = derive (p, d.kept, until);
      if (!d.objective || !verify_derivation (d, original, source, until))
        return false;
      if (phase_clock (CLOCK_MONOTONIC) >= until)
        return false;
      const auto index = generated++;
      observed.generated (d.objective, index);
      observed.selection (
          index, d.kept,
          p.legacy ? "distribute_G_select_positive_terms" : "positive_guarantee_deletion");
      for (size_t step = 0; step < p.frames.size (); ++step)
        observed.path_step (step, p.frames[step].child,
                            p.frames[step].parent.is (spot::op::Implies) ? "implication_consequent"
                            : p.frames[step].parent.is (spot::op::G) ? "positive_global_operand"
                            : p.frames[step].parent.is (spot::op::F)
                                ? "positive_eventually_operand"
                                : "positive_and_operand");
      const auto start = phase_clock (CLOCK_MONOTONIC);
      if (start >= until)
        return false;
      observed.begin (index);
      // Reserve half the remaining pre-pass slice for reaping and proof replay.
      const auto attempt_deadline = start + std::min ((until - start) / 2, per_attempt);
      observed.attempt_limit (attempt_deadline);
      const auto result = bounded_attempt (d, runner, attempt_deadline);
      const bool proof = result.result && accept (d, original, source, result.proof, until);
      if (proof) {
        observed.checked (result.proof);
        worker_verified ("UNREAL", "REAL");
      }
      observed.bounded_end (proof                                       ? "proof"
                            : result.state == attempt_result::cancelled ? "cancellation"
                                                                        : "inconclusive",
                            result.state == attempt_result::cancelled     ? "attempt_budget"
                            : result.state == attempt_result::exception   ? "candidate_exception"
                            : result.state == attempt_result::unavailable ? "fork_or_pipe_failed"
                                                                          : "none",
                            phase_clock (CLOCK_MONOTONIC) - start, result.usage);
      return proof;
    };
    bool proof = false;
    for (size_t i = 0; i < std::min (singleton_limit, p.guarantees.size ()); ++i) {
      if (phase_clock (CLOCK_MONOTONIC) >= until)
        break;
      if ((proof = attempt ({i})))
        break;
    }
    if (!proof && phase_clock (CLOCK_MONOTONIC) < until) {
      auto groups = dependency_groups (p, until);
      if (groups)
        for (auto& group : *groups) {
          if (phase_clock (CLOCK_MONOTONIC) >= until)
            break;
          if ((proof = attempt (std::move (group))))
            break;
        }
    }
    observed.eligibility ("eligible", 0, 0, p.core.size (), p.guarantees.size (), generated);
    observed.finish (proof ? "proof" : "inconclusive");
    return proof ? std::optional<bool> {true} : std::nullopt;
  }

}  // namespace acacia::unreal_witnesses
