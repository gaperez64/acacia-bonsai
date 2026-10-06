#include "record_transport_controls.hh"

#include <cassert>
#include <cstdio>

int main () {
  acacia::record_transport_test_start ();
  acacia::phase_records_init ();
  acacia::worker_record record;
  record.pid = getpid ();
  acacia::active_worker_record () = &record;
  acacia::worker_record_text (record.requested_backend, "spot-guarded-sparse");
  acacia::worker_record_text (record.effective_backend, "spot-guarded-sparse");
  acacia::worker_record_text (record.original_polarity, "UNREAL");
  acacia::worker_record_text (record.proof_polarity, "REAL");
  acacia::worker_event (record, "worker_start");
  acacia::worker_route ("unreal_automaton");
  const int command = std::getchar ();
  if (command == 'f') {
    constexpr char flood[] = "{\"phase\":\"flood\"}\n";
    for (int i = 0; i < 20000; ++i)
      acacia::phase_records_send (flood, sizeof flood - 1);
    assert (record.dropped > 0);
    std::puts ("flooded");
    std::fflush (stdout);
    // The harness waits for the independent writer to recover, then releases us.
    if (std::getchar () == EOF)
      return 3;
  }
  acacia::worker_decline ("translation-acceptance-set-limit");
  acacia::worker_terminal (2);
  acacia::worker_event (record, "parent_terminal", "exit", 2, 0,
                        record.dropped ? "dropped" : "complete");
  acacia::phase_records_summary ("transport_test");
  acacia::active_worker_record () = nullptr;
}
