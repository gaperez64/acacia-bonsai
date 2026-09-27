#include "../src/phase_records.hh"
#include <cstdio>

int main () {
  acacia::phase_records_start_writer ();
  acacia::phase_records_init ();
  const int command = std::getchar ();
  if (command == EOF) return 3;
  const auto start = acacia::phase_start ();
  acacia::phase_finish ("real:gr1:oxidd", "record_failure_test", start);
  if (command == 'f') {
    static const char flood[] = "{\"arm\":\"real:gr1:oxidd\",\"phase\":\"flood\"}\n";
    for (int i = 0; i < 20000; ++i)
      acacia::phase_records_send (flood, sizeof flood - 1);
  }
  if (command == 'f' && acacia::phase_records_enabled () &&
      acacia::phase_record_process_state ().dropped == 0) return 4;
  acacia::phase_records_summary ("real:gr1:oxidd");
  std::puts ("REALIZABLE");
  return 0;
}
