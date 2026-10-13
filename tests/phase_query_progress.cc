#include "solver/phase_observation.hh"

#include <array>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <unistd.h>

struct Rank {
    mutable size_t reads = 0;
    size_t size () const { return 3; }
    int operator[] (size_t q) const {
      ++reads;
      return std::array<int, 3> {-1, 2, 0}[q];
    }
};

size_t occurrences (const std::string& text, const char* needle) {
  size_t count = 0, offset = 0;
  while ((offset = text.find (needle, offset)) != std::string::npos) {
    ++count;
    offset += strlen (needle);
  }
  return count;
}

int main () {
  auto& transport = acacia::phase_record_process_state ();
  transport.write_fd = -1;
  Rank rank;
  acacia::legacy_query_progress disabled;
  for (size_t i = 0; i < 1024; ++i)
    disabled.completed (rank);
  if (rank.reads)
    return 1;

  int descriptors[2];
  if (pipe2 (descriptors, O_NONBLOCK) != 0)
    return 1;
  transport.write_fd = descriptors[1];
  acacia::worker_record worker;
  worker.pid = getpid ();
  worker.stage_id = 1;
  acacia::worker_record_text (worker.observed_stage, "search");
  acacia::active_worker_record () = &worker;
  acacia::legacy_query_progress progress;
  std::string records;
  auto drain = [&] {
    char bytes[4096];
    ssize_t length;
    while ((length = read (descriptors[0], bytes, sizeof bytes)) > 0)
      records.append (bytes, size_t (length));
  };
  for (size_t i = 0; i < 1024; ++i) {
    progress.completed (rank);
    drain ();
  }
  if (occurrences (records, "first_useful_action_query_ns") != 1 ||
      occurrences (records, "query_snapshot_ns") != 12 || rank.reads != 12 * 5 ||
      records.find ("\"key\":\"query_rank_support\",\"value\":\"2\"") == std::string::npos ||
      records.find ("\"key\":\"query_rank_maximum\",\"value\":\"2\"") == std::string::npos)
    return 1;
  acacia::worker_record_text (worker.observed_stage, "verification");
  progress.completed (rank);
  drain ();
  if (occurrences (records, "query_snapshot_ns") != 12)
    return 1;
  acacia::worker_record_text (worker.observed_stage, "search");
  ++worker.stage_id;
  progress.completed (rank);
  drain ();
  if (occurrences (records, "first_useful_action_query_ns") != 2 || worker.dropped)
    return 1;
  acacia::active_worker_record () = nullptr;
  transport.write_fd = -1;
  close (descriptors[0]);
  close (descriptors[1]);
  std::cout << "bounded query samples, stage reset and observation-off passed\n";
}
