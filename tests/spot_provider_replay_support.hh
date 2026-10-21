#pragma once
// Correctness-only provider reference and diagnostic fixtures; no CLI.
// Independent provider/game arithmetic and observed-counter adapters for tests.
#include "solver/k_schedule.hh"
#include "solver/spot_lazy_worker.hh"
#include <spot/twaalgos/hoa.hh>
#include "solver/spot_lazy_game.hh"
#include "solver/spot_guarded_forward_safety.hh"
#include "solver/spot_lazy_buchi_view.hh"
#include "utils/verbose.hh"
#include <unordered_map>

#include <algorithm>
#include <cerrno>
#include <iomanip>
#include <iostream>
#include <spot/misc/version.hh>
#include <spot/tl/parse.hh>
#include <spot/tl/apcollect.hh>
#include <spot/tl/print.hh>
#include <sstream>
#include <unistd.h>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}
namespace posets::vectors {
  size_t bool_threshold = 0;
}

namespace replay {
  using namespace acacia::spot_lazy_game;
  using Fields = std::map<std::string, std::string>;


  struct InvalidInput : std::runtime_error {
      using std::runtime_error::runtime_error;
  };
  Reporter pipe_reporter (int fd) {
    return {[fd] (const std::string& k, const std::string& v) {
      const auto s = k + '\t' + cell (v) + '\n';
      size_t offset = 0;
      while (offset < s.size ()) {
        const auto n = write (fd, s.data () + offset, s.size () - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) std::_Exit (2);
        offset += size_t (n);
      }
    }};
  }
} // namespace replay

namespace replay {
  struct Options {
      std::optional<std::string> partition;
  };
  letters::WorkerAlphabet alphabet (const spot::const_twa_ptr& p,
                                    const Options& o, Reporter report) {
    std::vector<std::pair<std::string, int>> aps;
    for (auto ap : p->ap ())
      aps.emplace_back (ap.ap_name (), p->get_dict ()->varnum (ap));
    std::sort (aps.begin (), aps.end ());
    const std::string partition = o.partition.value_or (std::string (aps.size (), 'c'));
    if (partition.size () != aps.size ())
      throw InvalidInput ("--partition requires one u/c per lexical AP");
    letters::WorkerAlphabet a {p->ap_vars (), bddtrue, bddtrue, {}};
    std::string names;
    for (size_t i = 0; i < aps.size (); ++i) {
      auto [name, var] = aps[i];
      if (partition[i] != 'u' && partition[i] != 'c')
        throw InvalidInput ("partition accepts only u/c");
      (partition[i] == 'u' ? a.inputs : a.outputs) &= bdd_ithvar (var);
      a.order.push_back (var);
      // Length-prefix AP names: commas and other punctuation are unambiguous.
      names += std::to_string (name.size ()) + ':' + name;
    }
    report.put ("ap_order", names);
    report.put ("partition", partition);
    return a;
  }
  // C4 has already completed every row. Export only that cache; in particular,
  // do not call Provider::materialize or any successor iterator a second time.
  spot::twa_graph_ptr export_graph (RowStore& store) {
    require (store.frozen && store.cache->complete_rows () == store.cache->state_count ());
    auto graph = spot::make_twa_graph (store.provider->get_dict ());
    graph->copy_ap_of (store.provider);
    graph->set_buchi ();
    graph->prop_state_acc (false);
    graph->new_states (store.cache->state_count ());
    graph->set_init_state (store.cache->initial_id ());
    for (size_t q = 0; q < store.cache->state_count (); ++q)
      for (const auto& edge : store.get (q).edges)
        graph->new_edge (q, edge.destination, edge.condition,
                         edge.increment ? spot::acc_cond::mark_t {0} : spot::acc_cond::mark_t {});
    return graph;
  }
  void rank_metrics (const SolveResult& r, Reporter report) {
    std::map<size_t, size_t> histogram;
    size_t entries = 0, bytes = 0, maximum = 0;
    for (const auto& n : r.nodes) {
      const auto count = n.rank.entries ().size ();
      ++histogram[count];
      entries += count;
      maximum = std::max (maximum, count);
      bytes += rank_bytes (n.rank);
    }
    std::string distribution;
    for (auto [size, count] : histogram)
      distribution += (distribution.empty () ? "" : ",") + std::to_string (size) + ':' +
                      std::to_string (count);
    report.put ("rank_support_distribution", distribution);
    report.count ("rank_support_sum", entries);
    report.count ("rank_support_max", maximum);
    report.count ("game_node_rank_bytes", bytes);
    size_t certificate_bytes = bytes;
    for (const auto& p : r.proofs)
      certificate_bytes += rank_bytes (p.rank);
    for (const auto& g : r.generators)
      certificate_bytes += rank_bytes (g);
    report.count ("certificate_rank_bytes", certificate_bytes);
    report.put ("rank_bytes_scope",
                "sum_of_retained_component_payload_snapshots_excludes_allocator_and_map_overhead");
    report.count ("game_states", r.nodes.size ());
    report.count ("guarded_choices", r.choices_created);
    report.count ("expansions", r.expansions);
    report.count ("losing_proofs", r.proofs.size ());
    report.count ("strategy_generators", r.generators.size ());
    report.count ("reopened_sources", r.reopened_sources);
    report.count ("reopen_enqueues", r.reopen_enqueues);
    report.count ("subsumption_scans", r.subsumption_scans);
    report.count ("subsumption_nodes_checked", r.subsumption_nodes_checked);
    report.count ("subsumption_nodes_invalidated", r.subsumption_nodes_invalidated);
    report.count ("scan_tombstones", r.scan_tombstones);
    report.count ("scan_prefilter_rejects", r.scan_prefilter_rejects);
    report.count ("scan_exact_compares", r.scan_exact_compares);
    report.count ("subsumption_queries", r.subsumption_queries);
    report.count ("subsumption_hits", r.subsumption_hits);
    report.count ("subsumption_prefilter_skips", r.subsumption_prefilter_skips);
    report.count ("losing_insertions", r.losing_insertions);
    report.count ("losing_removals", r.losing_removals);
    report.count ("losing_antichain_size", r.losing_antichain_size);
    report.count ("losing_antichain_peak", r.losing_antichain_peak);
    report.count ("proofs_total", r.proofs_total);
    report.count ("proofs_in_initial_cone", r.proofs_in_initial_cone.value ());
    report.count ("dependency_list_len_sum", r.dependency_list_len_sum.value ());
    report.count ("dependency_list_len_max", r.dependency_list_len_max.value ());
  }
}  // namespace replay
