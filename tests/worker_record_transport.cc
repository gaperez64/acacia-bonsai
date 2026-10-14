#include "record_transport_controls.hh"

#include <cassert>
#include <cstdio>
#include <poll.h>

namespace {
  // A test-only FIFO barrier acknowledges all earlier appends. The actual records
  // still use the production nonblocking transport, including deliberate losses.
  void drain () {
    auto& state = acacia::phase_record_process_state ();
    static unsigned sequence = 0;
    acacia::phase_packet packet {};
    packet.pid = getpid ();
    packet.length = snprintf (packet.line, sizeof packet.line,
                               "{\"event\":\"test_drain\",\"id\":%u}\n", ++sequence);
    const size_t bytes = offsetof (acacia::phase_packet, line) + packet.length;
    ssize_t written;
    while ((written = write (state.write_fd, &packet, bytes)) < 0 &&
           (errno == EAGAIN || errno == EINTR)) {
      pollfd writable {state.write_fd, POLLOUT, 0};
      poll (&writable, 1, -1);
    }
    if (written != ssize_t (bytes))
      std::abort ();
    char path[4096];
    snprintf (path, sizeof path, "%s/%ld.jsonl", getenv ("ACACIA_PHASE_RECORDS"),
               long (getpid ()));
    while (true) {
      const int fd = open (path, O_RDONLY | O_CLOEXEC);
      struct stat file {};
      char tail[128];
      const bool appended = fd >= 0 && fstat (fd, &file) == 0 &&
                            file.st_size >= packet.length &&
                            pread (fd, tail, packet.length, file.st_size - packet.length) ==
                                packet.length &&
                            std::memcmp (tail, packet.line, packet.length) == 0;
      if (fd >= 0)
        close (fd);
      if (appended)
        return;
      poll (nullptr, 0, 1);
    }
  }

  void finish () {
    auto& state = acacia::phase_record_process_state ();
    acacia::phase_records_summary ("legacy_parent");
    close (state.write_fd);
    state.write_fd = -1;
    // This fixture checks writer results, so wait for them explicitly. The slow
    // destination case below still exercises the production bounded shutdown.
    while (waitpid (state.writer, nullptr, 0) < 0 && errno == EINTR) {}
    state.writer = -1;
  }
}

int main () {
  acacia::record_transport_test_start ();
  acacia::phase_records_init ();
  const int command = std::getchar ();
  if (command == EOF)
    return 3;
  const bool reliable = getenv ("ACACIA_TEST_RECORD_DRAIN");
  acacia::worker_record record;
  record.pid = getpid ();
  acacia::active_worker_record () = &record;
  acacia::worker_record_text (record.requested_backend, "spot-guarded-sparse");
  acacia::worker_record_text (record.effective_backend, "spot-guarded-sparse");
  acacia::worker_record_text (record.original_polarity, "UNREAL");
  acacia::worker_record_text (record.proof_polarity, "REAL");
  acacia::worker_event (record, "worker_start");
  if (reliable)
    drain ();
  acacia::worker_route ("unreal_automaton");
  if (reliable)
    drain ();
  record.subjob = 1;
  record.subjobs = 2;
  record.k = 3;
  {
    acacia::legacy_phase translating ("translation");
    acacia::legacy_count ("states", 7);
  }
  if (reliable)
    drain ();
  if (command == 'f') {
    constexpr char flood[] = "{\"phase\":\"flood\"}\n";
    for (int i = 0; i < 20000; ++i)
      acacia::phase_records_send (flood, sizeof flood - 1);
    assert (record.dropped > 0);
    std::puts ("flooded");
    std::fflush (stdout);
    // Prove that a terminal record is also best effort while the writer is gated.
    if (std::getchar () != 'l')
      return 3;
    const auto dropped = record.dropped;
    acacia::worker_event (record, "parent_terminal", "exit", 2, 0, "dropped");
    assert (record.dropped > dropped);
    std::puts ("terminal_dropped");
    std::fflush (stdout);
    if (std::getchar () == EOF)
      return 3;
    // A visible flood row does not acknowledge the backlog. Drain it before
    // emitting the recovery records that the harness requires to be delivered.
    drain ();
  }
  acacia::worker_decline ("translation-acceptance-set-limit");
  if (reliable || command == 'f')
    drain ();
  acacia::worker_terminal (2);
  if (reliable || command == 'f')
    drain ();
  acacia::worker_event (record, "parent_terminal", "exit", 2, 0,
                        record.dropped ? "dropped" : "complete");
  if (reliable || command == 'f')
    drain ();
  acacia::phase_records_summary ("transport_test");
  acacia::active_worker_record () = nullptr;
  if (!getenv ("ACACIA_TEST_RECORD_WRITER_STALL"))
    finish ();
}
