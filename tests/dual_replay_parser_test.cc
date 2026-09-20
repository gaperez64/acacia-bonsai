#include "research/all_input_actions.hh"
#include "research/cpre_event.hh"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {
  using namespace acacia::research;

  int failures = 0;

  void expect (const std::string& name, bool condition) {
    if (condition)
      return;
    std::cerr << "FAIL: " << name << '\n';
    ++failures;
  }

  void write_file (const std::filesystem::path& path, const std::string& contents) {
    std::ofstream out {path};
    out << contents;
  }

  bool rejects (const std::function<void ()>& operation) {
    try {
      operation ();
      return false;
    } catch (const std::exception&) {
      return true;
    }
  }
}

int main () {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path () /
      ("acacia-dual-parser-" + std::to_string (static_cast<long> (::getpid ())));
  std::filesystem::remove_all (root);
  std::filesystem::create_directories (root);

  const std::string valid_event =
      "# schema_version=2 loop=3 k=2 actions=1 before=1 input=x\n"
      "[before]\n"
      "-1\t0\n"
      "[actions]\n"
      "action\t0\n"
      "0\t1\t1\n"
      "[after]\t1\n"
      "-1\t0\n";
  write_file (root / "valid.tsv", valid_event);
  const event parsed = load (root / "valid.tsv", 2, 1);
  expect ("valid event counts", parsed.before.size () == 1 and parsed.actions.size () == 1 and
                                    parsed.after.size () == 1);

  write_file (root / "truncated.tsv", valid_event.substr (0, valid_event.find ("[after]")));
  expect ("truncated event rejected",
          rejects ([&] { (void) load (root / "truncated.tsv", 2, 1); }));

  std::string bad_source = valid_event;
  bad_source.replace (bad_source.find ("0\t1\t1"), 5, "0\t2\t1");
  write_file (root / "bad-source.tsv", bad_source);
  expect ("invalid source index rejected",
          rejects ([&] { (void) load (root / "bad-source.tsv", 2, 1); }));

  std::string bad_increment = valid_event;
  bad_increment.replace (bad_increment.find ("0\t1\t1"), 5, "0\t1\t2");
  write_file (root / "bad-increment.tsv", bad_increment);
  expect ("invalid increment rejected",
          rejects ([&] { (void) load (root / "bad-increment.tsv", 2, 1); }));

  std::string bad_rank = valid_event;
  bad_rank.replace (bad_rank.find ("-1\t0"), 4, "-1\t1");
  write_file (root / "bad-rank.tsv", bad_rank);
  expect ("Boolean overflow rank rejected",
          rejects ([&] { (void) load (root / "bad-rank.tsv", 2, 1); }));

  std::string bad_count = valid_event;
  bad_count.replace (bad_count.find ("before=1"), 8, "before=2");
  write_file (root / "bad-count.tsv", bad_count);
  expect ("declared before count rejected",
          rejects ([&] { (void) load (root / "bad-count.tsv", 2, 1); }));

  const std::string valid_actions =
      "# schema_version=1 inputs=1 actions=1 transitions=1\n"
      "[input\t0]\n"
      "action\t0\n"
      "0\t1\t0\n";
  write_file (root / "all-input-actions.tsv", valid_actions);
  const input_action_table table = load_input_actions (root / "all-input-actions.tsv", 2);
  expect ("valid all-input table counts",
          table.input_count () == 1 and table.action_count () == 1);

  std::string bad_table_count = valid_actions;
  bad_table_count.replace (bad_table_count.find ("transitions=1"), 13, "transitions=2");
  write_file (root / "bad-table-count.tsv", bad_table_count);
  expect ("all-input count mismatch rejected",
          rejects ([&] { (void) load_input_actions (root / "bad-table-count.tsv", 2); }));

  std::string bad_table_destination = valid_actions;
  bad_table_destination.replace (bad_table_destination.find ("0\t1\t0"), 5, "2\t1\t0");
  write_file (root / "bad-table-destination.tsv", bad_table_destination);
  expect ("all-input destination index rejected",
          rejects ([&] { (void) load_input_actions (root / "bad-table-destination.tsv", 2); }));

  std::filesystem::remove_all (root);
  return failures == 0 ? 0 : 1;
}
