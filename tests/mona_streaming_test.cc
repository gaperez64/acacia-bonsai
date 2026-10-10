#include "actioners/standard.hh"
#include "input_pickers/critical.hh"
#include "input_pickers/critical_fullrnd.hh"
#include "input_pickers/critical_pq.hh"
#include "input_pickers/critical_rnd.hh"
#include "ios_precomputers/mona.hh"
#include "ios_precomputers/prepare.hh"
#include "ios_precomputers/semantic_mona.hh"
#include "mona_materialized_reference.hh"
#include "solver/forward_k_bounded_safety_aut.hh"
#include "solver/k_bounded_safety_aut.hh"
#include "utils/verbose.hh"
#include <unordered_map>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

#include <posets/downsets.hh>
#include <posets/vectors.hh>
#include <posets/vectors/traits.hh>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}
namespace posets::vectors {
  size_t bool_threshold = 0;
}

namespace {
  using state = posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>;
  using downset = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<state>;
  using transitions = std::vector<std::pair<unsigned, unsigned>>;
  using action_vec = std::vector<std::vector<std::pair<unsigned, bool>>>;
  using table = std::list<std::pair<bdd, std::list<action_vec>>>;

  void check (bool ok, const char* message) {
    if (not ok)
      throw std::runtime_error (message);
  }

  struct game {
      spot::twa_graph_ptr aut = spot::make_twa_graph (spot::make_bdd_dict ());
      bdd inputs = bddtrue, outputs = bddtrue;
  };

  game generated (unsigned seed) {
    game g;
    std::mt19937 rng (seed);
    const unsigned n = 1 + seed % 9, ni = seed % 4, no = (seed / 4) % 4;
    g.aut->new_states (n);
    g.aut->set_init_state (seed % n);
    g.aut->set_acceptance (1, spot::acc_cond::acc_code::buchi ());
    g.aut->prop_state_acc (true);
    std::vector<bdd> aps;
    for (unsigned i = 0; i < ni + no; ++i) {
      auto ap = bdd_ithvar (g.aut->register_ap ("p" + std::to_string (i)));
      aps.push_back (ap);
      (i < ni ? g.inputs : g.outputs) &= ap;
    }
    for (unsigned p = 0; p < n; ++p)
      for (unsigned q = 0; q < n; ++q) {
        bdd guard = bddfalse;
        for (unsigned v = 0; v < (1u << aps.size ()); ++v)
          if (rng () % 3 == 0) {
            bdd cube = bddtrue;
            for (unsigned i = 0; i < aps.size (); ++i)
              cube &= (v & (1u << i)) ? aps[i] : !aps[i];
            guard |= cube;
          }
        if (guard != bddfalse)
          g.aut->new_acc_edge (p, q, guard, p % 3 == 0);
      }
    return g;
  }

  // Many input paths share two dense endpoint relations. Several output paths
  // repeat each relation; the actioner's input deduplication retains two lists.
  game stress (unsigned bits, unsigned states) {
    game g;
    g.aut->new_states (states);
    g.aut->set_init_state (0);
    g.aut->set_acceptance (1, spot::acc_cond::acc_code::buchi ());
    g.aut->prop_state_acc (true);
    bdd parity = bddfalse, any_output = bddfalse;
    for (unsigned i = 0; i < bits; ++i) {
      bdd ap = bdd_ithvar (g.aut->register_ap ("i" + std::to_string (i)));
      g.inputs &= ap;
      parity ^= ap;
    }
    for (unsigned i = 0; i < 3; ++i) {
      bdd ap = bdd_ithvar (g.aut->register_ap ("o" + std::to_string (i)));
      g.outputs &= ap;
      any_output |= ap;
    }
    for (unsigned p = 0; p < states; ++p)
      for (unsigned q = 0; q < states; ++q) {
        bdd guard = q == 0 ? bdd (bddtrue) : ((q % 2 ? parity : !parity) & any_output);
        g.aut->new_acc_edge (p, q, guard, p % 3 == 0);
      }
    return g;
  }

  void number (std::string& bytes, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i)
      bytes.push_back (static_cast<char> (value >> (8 * i)));
  }

  void serialize (std::string& bytes, const table& actions) {
    number (bytes, actions.size ());
    for (const auto& [input, outputs] : actions) {
      number (bytes, input.id ());
      number (bytes, outputs.size ());
      for (const auto& action : outputs) {
        number (bytes, action.size ());
        for (const auto& row : action) {
          number (bytes, row.size ());
          for (const auto& [src, increment] : row) {
            number (bytes, src);
            number (bytes, increment);
          }
        }
      }
    }
  }

  struct recording_actioner {
      std::function<state (const state&, const action_vec&, actioners::direction)> apply_action;
      std::string& bytes;
      std::unordered_map<const void*, size_t> ids;
      template <typename Actioner>
      recording_actioner (Actioner& actioner, std::string& bytes)
        : apply_action {[&] (const state& s, const action_vec& a, actioners::direction d) {
            return actioner.apply (s, a, d);
          }},
          bytes {bytes} {
        size_t id = 0;
        for (const auto& [input, outputs] : actioner.actions ())
          for (const auto& action : outputs)
            ids.emplace (&action, id++);
      }
      auto apply (const state& s, const action_vec& action, actioners::direction direction) {
        number (bytes, ids.at (&action));
        number (bytes, direction == actioners::direction::forward);
        for (auto x : s)
          number (bytes, x);
        auto result = apply_action (s, action, direction);
        for (auto x : result)
          number (bytes, x);
        return result;
      }
  };

  // Explicit template instantiation can name private members. Read the actual
  // engines in this test without adding an access or mutation hook to the solver.
  using random_picker = input_pickers::detail::critical_rnd<table, recording_actioner>;
  using full_random_picker = input_pickers::detail::critical_fullrnd<table, recording_actioner>;
  using priority_picker = input_pickers::detail::critical_pq<table, recording_actioner>;
  struct random_rng {
      using member = std::mt19937 random_picker::*;
      friend member rng_member (random_rng);
  };
  struct full_random_rng {
      using member = std::mt19937 full_random_picker::*;
      friend member rng_member (full_random_rng);
  };
  struct priority_rng {
      using member = std::mt19937 priority_picker::*;
      friend member rng_member (priority_rng);
  };
  template <typename Tag, typename Tag::member Member>
  struct rng_access {
      friend typename Tag::member rng_member (Tag) { return Member; }
  };
  template struct rng_access<random_rng, &random_picker::gen>;
  template struct rng_access<full_random_rng, &full_random_picker::gen>;
  template struct rng_access<priority_rng, &priority_picker::gen>;

  template <typename Picker, typename Actioner>
  auto trace (const game& g, Actioner& actioner) {
    std::string bytes;
    recording_actioner recorder (actioner, bytes);
    auto picker = Picker::make (actioner.actions (), recorder);
    std::mt19937 rng (871);
    // Repeated calls preserve the pickers' generator state and action splices.
    for (unsigned i = 0; i < 128; ++i) {
      const VECTOR_ELT_T k = 1 + i % 4;
      actioner.setK (k);
      posets::utils::vector_mm<VECTOR_ELT_T> v (g.aut->num_states ());
      for (unsigned p = 0; p < v.size (); ++p)
        v[p] = p < posets::vectors::bool_threshold ? rng () % (k + 1) - 1 : rng () % 2 - 1;
      downset region {state (v)};
      auto selected = picker (region);
      number (bytes, selected.has_value ());
      if (selected) {
        number (bytes, selected->get ().first.id ());
        for (const auto& action : selected->get ().second)
          recorder.apply (state (v), action, actioners::direction::backward);
      }
      serialize (bytes, actioner.actions ());
      if constexpr (not std::is_same_v<Picker, input_pickers::critical>) {
        std::ostringstream engine;
        using tag = std::conditional_t<
            std::is_same_v<Picker, input_pickers::critical_rnd>, random_rng,
            std::conditional_t<std::is_same_v<Picker, input_pickers::critical_pq>, priority_rng,
                               full_random_rng>>;
        engine << (picker.*rng_member (tag {}));
        bytes += engine.str ();
      }
    }
    return bytes;
  }

  template <typename Picker, typename Old, typename New>
  void compare_picker (const game& g, const Old& old, const New& streamed) {
    auto a = old;
    auto b = streamed;
    check (trace<Picker> (g, a) == trace<Picker> (g, b), "picker/action ID trace differs");
  }

  template <bool Quotient>
  void compare (const game& g) {
    const auto old_inputs =
        mona_reference::ios_precomputers::detail::mona<spot::twa_graph_ptr, transitions,
                                                       Quotient> (g.aut, g.inputs, g.outputs) ();
    auto old = mona_reference::actioners::standard<state>::make (g.aut, old_inputs, 3);
    auto precomputer = ios_precomputers::detail::mona<spot::twa_graph_ptr, transitions, Quotient> (
        g.aut, g.inputs, g.outputs);
    auto streamed = actioners::standard<state>::make (g.aut, precomputer, 3);
    std::string a, b;
    serialize (a, old.actions ());
    serialize (b, streamed.actions ());
    check (a == b, "action table differs byte-for-byte");
    compare_picker<input_pickers::critical> (g, old, streamed);
    compare_picker<input_pickers::critical_pq> (g, old, streamed);
    compare_picker<input_pickers::critical_rnd> (g, old, streamed);
    compare_picker<input_pickers::critical_fullrnd> (g, old, streamed);

    const bdd selected = old_inputs.empty () ? bddfalse : old_inputs.front ().first;
    auto filtered = old_inputs;
    filtered.remove_if ([&] (const auto& input) { return (input.first & selected) == bddfalse; });
    auto old_filtered = mona_reference::actioners::standard<state>::make (g.aut, filtered, 3);
    precomputer.restrict_inputs (selected);
    auto new_filtered = actioners::standard<state>::make (g.aut, precomputer, 3);
    a.clear ();
    b.clear ();
    serialize (a, old_filtered.actions ());
    serialize (b, new_filtered.actions ());
    check (a == b, "representative input filtering differs");
  }

  struct counted_transitions : transitions {
      inline static size_t live = 0, peak = 0;
      counted_transitions () = default;
      counted_transitions (counted_transitions&& other) noexcept
        : transitions (std::move (other)) {}
      ~counted_transitions () { live -= size () * sizeof (value_type); }
      void emplace_back (unsigned from, unsigned to) {
        transitions::emplace_back (from, to);
        live += sizeof (value_type);
        peak = std::max (peak, live);
      }
  };

  void memory_stress (const char* mode, unsigned bits, unsigned states,
                      bool report_memory = true) {
    auto g = stress (bits, states);
    posets::vectors::bool_threshold = states;
    auto report = [&] (auto& actioner) {
      if (not report_memory)
        return;
      rusage usage {};
      getrusage (RUSAGE_SELF, &usage);
      std::cout << "mode=" << mode << " input_bits=" << bits << " states=" << states
                << " decoded_peak_endpoint_bytes=" << counted_transitions::peak
                << " decoded_live_endpoint_bytes=" << counted_transitions::live
                << " action_table_payload_bytes="
                << acacia::legacy_action_bytes (actioner.actions ())
                << " peak_rss_kb=" << usage.ru_maxrss << std::endl;
    };
    if (std::string (mode) == "materialized") {
      auto decoded = mona_reference::ios_precomputers::detail::mona<spot::twa_graph_ptr,
                                                                    counted_transitions> (
          g.aut, g.inputs, g.outputs) ();
      auto actioner = mona_reference::actioners::standard<state>::make (g.aut, decoded, 3);
      report (actioner);
    }
    else {
      check (std::string (mode) == "streamed", "unknown memory diagnostic mode");
      auto precomputer = ios_precomputers::detail::mona<spot::twa_graph_ptr, counted_transitions> (
          g.aut, g.inputs, g.outputs);
      auto actioner = actioners::standard<state>::make (g.aut, precomputer, 3);
      check (counted_transitions::live == 0, "stream retains decoded endpoints");
      // At most four sets for one input: three output paths plus the all-false path.
      check (counted_transitions::peak <= 4 * states * states * sizeof (transitions::value_type),
             "decoded memory grows beyond one input class");
      report (actioner);
    }
    check (counted_transitions::live == 0, "decoded endpoints leaked");
  }

  template <typename Precomputer, typename Actioner, bool Forward = false>
  auto solve (const game& g) {
    using solver_type =
        std::conditional_t<Forward,
                           acacia::solver_detail::forward_k_bounded_safety_aut_detail<
                               downset, Precomputer, Actioner, input_pickers::critical_fullrnd>,
                           k_bounded_safety_aut_detail<downset, Precomputer, Actioner,
                                                       input_pickers::critical_fullrnd>>;
    Precomputer precomputer;
    Actioner actioner;
    input_pickers::critical_fullrnd picker;
    solver_type solver (g.aut, 1, 4, 1, g.inputs, g.outputs, precomputer, actioner, picker);
    std::ostringstream execution;
    struct capture {
        std::streambuf* previous;
        unsigned verbose;
        explicit capture (std::ostream& out)
          : previous {utils::vout.rdbuf (out.rdbuf ())},
            verbose {utils::verbose} {
          utils::verbose = 3;
        }
        ~capture () {
          utils::vout.rdbuf (previous);
          utils::verbose = verbose;
        }
    } captured (execution);
    auto result = solver.solve ();
    std::string bytes = execution.str ();
    number (bytes, result.has_value ());
    if (result) {
      number (bytes, result->first);
      for (const auto& maximum : result->second)
        for (auto x : maximum)
          number (bytes, x);
    }
    return bytes;
  }
}

int main (int argc, char** argv) {
  try {
    if (argc == 5 and std::string (argv[1]) == "--memory-stress") {
      memory_stress (argv[2], std::stoul (argv[3]), std::stoul (argv[4]));
      return 0;
    }
    for (unsigned seed = 0; seed < 64; ++seed) {
      auto g = generated (seed);
      posets::vectors::bool_threshold = seed % (g.aut->num_states () + 1);
      compare<false> (g);
      compare<true> (g);
      check ((solve<mona_reference::ios_precomputers::mona,
                    mona_reference::actioners::standard<state>> (g) ==
              solve<ios_precomputers::mona, actioners::standard<state>> (g)),
             "winning bound/region differs byte-for-byte");
      check ((solve<mona_reference::ios_precomputers::mona,
                    mona_reference::actioners::standard<state>, true> (g) ==
              solve<ios_precomputers::mona, actioners::standard<state>, true> (g)),
             "forward winning bound/certificate differs byte-for-byte");
    }
    for (unsigned bits : {4u, 7u}) {
      auto g = stress (bits, 8);
      posets::vectors::bool_threshold = 8;
      compare<false> (g);
      compare<true> (g);
    }
    memory_stress ("streamed", 8, 16, false);
    std::cout
        << "mona-streaming: byte-identical tables, picker/action ID traces and solve results\n";
  } catch (const std::exception& e) {
    std::cerr << e.what () << '\n';
    return 1;
  }
}
