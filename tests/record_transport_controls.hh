#pragma once

#include "../src/phase_records.hh"

namespace acacia {
  inline void record_transport_test_setup () {
    if (getenv ("ACACIA_TEST_RECORD_WRITER_STALL"))
      while (true)
        pause ();
    if (getenv ("ACACIA_TEST_RECORD_WRITER_DELAY")) {
      timespec delay {0, 100000000};
      nanosleep (&delay, nullptr);
    }
  }
  inline void record_transport_test_start () {
    const size_t packet_cap =
        getenv ("ACACIA_TEST_RECORD_PIPE_512") ? size_t {512} : sizeof (phase_packet);
    phase_records_start_writer (packet_cap, record_transport_test_setup);
  }
}
