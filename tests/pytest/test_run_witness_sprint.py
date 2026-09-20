"""Unit tests for the W1 shard/order driver (benchmarking/run-witness-sprint.py).

Runs no solver: `campaign`'s subprocess dispatch is exercised either via
--dry-run or with subprocess.run mocked to write synthetic coverage rows.
"""

import csv
import importlib.util
import subprocess
import sys
from pathlib import Path

import pytest


SCRIPT = Path(__file__).resolve().parents[2] / "benchmarking" / "run-witness-sprint.py"
SPEC = importlib.util.spec_from_file_location("run_witness_sprint", SCRIPT)
driver = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = driver
SPEC.loader.exec_module(driver)


def write_list(path, ids, header=True):
    lines = []
    if header:
        lines.append("# a header comment, like the real corpus lists carry")
        lines.append("")
    lines.extend(ids)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


# ---------- read_instance_ids ----------

def test_read_instance_ids_skips_comments_and_blanks(tmp_path):
    path = write_list(tmp_path / "list.txt", ["a.ltl", "b.ltl", "c.ltl"])
    assert driver.read_instance_ids(path) == ["a.ltl", "b.ltl", "c.ltl"]


def test_read_instance_ids_preserves_order(tmp_path):
    path = write_list(tmp_path / "list.txt", ["z.ltl", "a.ltl", "m.ltl"])
    assert driver.read_instance_ids(path) == ["z.ltl", "a.ltl", "m.ltl"]


# ---------- shard_index ----------

def test_shard_index_in_range():
    for instance_id in ("a.ltl", "arbiter_pb_10_pe_.ltl", ""):
        assert 0 <= driver.shard_index(instance_id, 24) < 24


def test_shard_index_deterministic():
    assert (driver.shard_index("arbiter_pb_10_pe_.ltl", 24) ==
            driver.shard_index("arbiter_pb_10_pe_.ltl", 24))


def test_shard_index_not_trivially_constant():
    # A stable hash should not collapse every ID onto the same shard.
    ids = [f"instance_{i}.ltl" for i in range(200)]
    assert len({driver.shard_index(i, 24) for i in ids}) > 1


# ---------- make-shards ----------

def test_make_shards_partition_is_disjoint_and_complete(tmp_path):
    ids = [f"instance_{i}.ltl" for i in range(1524)]
    write_list(tmp_path / "all.list", ids)
    out_dir = tmp_path / "shards"
    rc = driver.main(["make-shards", "--list", str(tmp_path / "all.list"),
                      "--n-shards", "24", "--out-dir", str(out_dir)])
    assert rc == 0
    shard_paths = sorted(out_dir.glob("shard_*.list"))
    assert len(shard_paths) == 24
    seen = []
    for shard_path in shard_paths:
        seen.extend(driver.read_instance_ids(shard_path))
    assert sorted(seen) == sorted(ids)
    assert len(set(seen)) == len(ids)  # disjoint: no instance in two shards


def test_make_shards_deterministic_across_runs(tmp_path):
    ids = [f"instance_{i}.ltl" for i in range(500)]
    write_list(tmp_path / "all.list", ids)
    out1, out2 = tmp_path / "shards1", tmp_path / "shards2"
    for out_dir in (out1, out2):
        assert driver.main(["make-shards", "--list", str(tmp_path / "all.list"),
                            "--n-shards", "24", "--out-dir", str(out_dir)]) == 0
    for a, b in zip(sorted(out1.glob("shard_*.list")), sorted(out2.glob("shard_*.list"))):
        assert a.read_text() == b.read_text()


def test_make_shards_independent_of_original_order(tmp_path):
    # "Independent of performance and family outcome" in particular means
    # independent of the corpus list's own ordering.
    ids = [f"instance_{i}.ltl" for i in range(300)]
    write_list(tmp_path / "forward.list", ids)
    write_list(tmp_path / "reversed.list", list(reversed(ids)))
    out_fwd, out_rev = tmp_path / "fwd", tmp_path / "rev"
    driver.main(["make-shards", "--list", str(tmp_path / "forward.list"),
                "--n-shards", "24", "--out-dir", str(out_fwd)])
    driver.main(["make-shards", "--list", str(tmp_path / "reversed.list"),
                "--n-shards", "24", "--out-dir", str(out_rev)])
    for i in range(24):
        fwd_members = set(driver.read_instance_ids(out_fwd / f"shard_{i:02d}.list"))
        rev_members = set(driver.read_instance_ids(out_rev / f"shard_{i:02d}.list"))
        assert fwd_members == rev_members


def test_make_shards_rejects_empty_list(tmp_path, capsys):
    write_list(tmp_path / "empty.list", [], header=True)
    rc = driver.main(["make-shards", "--list", str(tmp_path / "empty.list"),
                      "--n-shards", "24", "--out-dir", str(tmp_path / "shards")])
    assert rc == 1
    assert "no instance IDs" in capsys.readouterr().err


def test_make_shards_writes_none_missing_when_n_shards_exceeds_instances(tmp_path):
    write_list(tmp_path / "small.list", ["a.ltl", "b.ltl"])
    out_dir = tmp_path / "shards"
    rc = driver.main(["make-shards", "--list", str(tmp_path / "small.list"),
                      "--n-shards", "24", "--out-dir", str(out_dir)])
    assert rc == 0
    seen = []
    for shard_path in sorted(out_dir.glob("shard_*.list")):
        seen.extend(driver.read_instance_ids(shard_path))
    assert sorted(seen) == ["a.ltl", "b.ltl"]


# ---------- parse_candidates ----------

def test_parse_candidates_valid():
    assert driver.parse_candidates("B=/bin/b,S=/bin/s") == [("B", "/bin/b"), ("S", "/bin/s")]


@pytest.mark.parametrize("spec", ["B", "B=", "=/bin/b", "B=/bin/b,S"])
def test_parse_candidates_rejects_malformed(spec):
    with pytest.raises(Exception):
        driver.parse_candidates(spec)


# ---------- campaign ordering (dry-run only: no subprocess is invoked) ----------

def make_shards(tmp_path, n_shards=4, per_shard=2):
    shards_dir = tmp_path / "shards"
    shards_dir.mkdir(exist_ok=True)
    ids = [f"i{n}_{k}.ltl" for n in range(n_shards) for k in range(per_shard)]
    for n in range(n_shards):
        write_list(shards_dir / f"shard_{n:02d}.list", ids[n * per_shard:(n + 1) * per_shard],
                  header=True)
    return shards_dir


def run_campaign(tmp_path, cap, epoch, candidates="B=/bin/b,S=/bin/s", n_shards=4, capsys=None):
    shards_dir = make_shards(tmp_path, n_shards=n_shards)
    out_dir = tmp_path / f"out-{cap}-{epoch}"
    rc = driver.main(["campaign", "--cap", str(cap), "--epoch", str(epoch),
                      "--shards-dir", str(shards_dir), "--candidates", candidates,
                      "--tlsf-map", "unused.tsv", "--tlsf-corpus", "unused-corpus",
                      "--preset", "test-preset", "--acacia-sha", "deadbeef",
                      "--out-dir", str(out_dir), "--dry-run"])
    log_lines = (out_dir / f"campaign-cap{cap}-epoch{epoch}.log").read_text().splitlines()
    order = [(line.split("shard=")[1].split(".list")[0], line.split("candidate=")[1].split(" ")[0])
             for line in log_lines if "candidate=" in line]
    return rc, order


def test_campaign_rejects_invalid_cap(tmp_path):
    shards_dir = make_shards(tmp_path)
    with pytest.raises(SystemExit):
        driver.main(["campaign", "--cap", "60", "--epoch", "1",
                    "--shards-dir", str(shards_dir), "--candidates", "B=/bin/b,S=/bin/s",
                    "--tlsf-map", "x", "--tlsf-corpus", "x", "--preset", "p", "--acacia-sha", "s",
                    "--out-dir", str(tmp_path / "out")])


def test_campaign_rejects_missing_shards(tmp_path):
    empty_dir = tmp_path / "empty-shards"
    empty_dir.mkdir()
    rc = driver.main(["campaign", "--cap", "17", "--epoch", "1",
                      "--shards-dir", str(empty_dir), "--candidates", "B=/bin/b,S=/bin/s",
                      "--tlsf-map", "x", "--tlsf-corpus", "x", "--preset", "p", "--acacia-sha", "s",
                      "--out-dir", str(tmp_path / "out")])
    assert rc == 1


def test_campaign_epoch1_alternates_starting_with_base_order(tmp_path):
    rc, order = run_campaign(tmp_path, 17, 1)
    assert rc == 0
    assert order == [
        ("shard_00", "B"), ("shard_00", "S"),
        ("shard_01", "S"), ("shard_01", "B"),
        ("shard_02", "B"), ("shard_02", "S"),
        ("shard_03", "S"), ("shard_03", "B"),
    ]


def test_campaign_epoch2_starts_from_the_reversed_base_order(tmp_path):
    rc, order = run_campaign(tmp_path, 17, 2)
    assert rc == 0
    assert order == [
        ("shard_00", "S"), ("shard_00", "B"),
        ("shard_01", "B"), ("shard_01", "S"),
        ("shard_02", "S"), ("shard_02", "B"),
        ("shard_03", "B"), ("shard_03", "S"),
    ]


def test_epoch1_and_epoch2_are_exact_mirror_images_per_shard(tmp_path):
    _, order1 = run_campaign(tmp_path, 17, 1)
    _, order2 = run_campaign(tmp_path, 120, 2)
    by_shard_1 = {}
    for shard, candidate in order1:
        by_shard_1.setdefault(shard, []).append(candidate)
    by_shard_2 = {}
    for shard, candidate in order2:
        by_shard_2.setdefault(shard, []).append(candidate)
    assert by_shard_1.keys() == by_shard_2.keys()
    for shard in by_shard_1:
        assert by_shard_1[shard] == list(reversed(by_shard_2[shard]))


def test_two_caps_in_the_same_out_dir_never_collide(tmp_path):
    # Even a careless caller reusing one --out-dir for both caps must not
    # have the 120s and 17s legs overwrite each other's rows: the cap is
    # embedded in the output filename itself, not just the directory path.
    shards_dir = make_shards(tmp_path)
    shared_out = tmp_path / "shared-out"
    for cap in (17, 120):
        rc = driver.main(["campaign", "--cap", str(cap), "--epoch", "1",
                          "--shards-dir", str(shards_dir), "--candidates", "B=/bin/b,S=/bin/s",
                          "--tlsf-map", "x", "--tlsf-corpus", "x", "--preset", "p",
                          "--acacia-sha", "s", "--out-dir", str(shared_out), "--dry-run"])
        assert rc == 0
    log_names = {p.name for p in shared_out.glob("campaign-cap*.log")}
    assert log_names == {"campaign-cap17-epoch1.log", "campaign-cap120-epoch1.log"}


def test_campaign_dry_run_never_invokes_a_subprocess(tmp_path, monkeypatch):
    calls = []
    monkeypatch.setattr(subprocess, "run", lambda *a, **k: calls.append((a, k)))
    rc, _ = run_campaign(tmp_path, 17, 1)
    assert rc == 0
    assert calls == []


# ---------- complete-series summary regeneration ----------

def test_regenerate_series_summary_uses_all_shard_instances(tmp_path):
    shards_dir = tmp_path / "shards"
    shards_dir.mkdir()
    write_list(shards_dir / "shard_00.list", ["alpha.ltl", "beta.ltl"])
    write_list(shards_dir / "shard_01.list", ["gamma.ltl"])
    instances = driver.read_instance_union(sorted(shards_dir.glob("shard_*.list")))

    coverage = driver.load_coverage_module()
    output = tmp_path / "B-cap17-epoch1.tsv"
    rows = []
    for instance, result, seconds in (
        ("alpha.ltl", "REALIZABLE", "0.25"),
        ("beta.ltl", "TIMEOUT", "17.01"),
        ("gamma.ltl", "UNREALIZABLE", "0.75"),
    ):
        row = dict.fromkeys(coverage.OUTPUT_COLUMNS, "")
        row.update(
            solver_label="B-cap17-epoch1",
            instance=instance,
            cap_s="17",
            result=result,
            seconds=seconds,
            expectation_source="none",
        )
        rows.append(row)
    coverage.atomic_write_tsv(output, coverage.OUTPUT_COLUMNS, rows)

    summary_path = driver.regenerate_series_summary(
        output, "B-cap17-epoch1", instances, 17
    )
    with summary_path.open(encoding="utf-8", newline="") as stream:
        summary_rows = list(csv.DictReader(stream, delimiter="\t"))

    assert [row["instance"] for row in summary_rows] == [
        "alpha.ltl", "beta.ltl", "gamma.ltl",
    ]
    assert [row["failure_kind_at_max_cap"] for row in summary_rows] == [
        "REALIZABLE", "TIMEOUT", "UNREALIZABLE",
    ]


@pytest.mark.parametrize(("cap", "epoch"), [(17, 1), (120, 2)])
def test_campaign_regenerates_complete_summaries_after_all_shards(
    tmp_path, monkeypatch, cap, epoch
):
    shards_dir = make_shards(tmp_path, n_shards=3, per_shard=2)
    shard_paths = sorted(shards_dir.glob("shard_*.list"))
    campaign_instances = driver.read_instance_union(shard_paths)
    shard_instances = [driver.read_instance_ids(path) for path in shard_paths]
    out_dir = tmp_path / f"out-{cap}-{epoch}"
    coverage = driver.load_coverage_module()

    subprocess_calls = []

    def fake_run(cmd, **kwargs):
        subprocess_calls.append((cmd, kwargs))
        output = Path(cmd[cmd.index("--output") + 1])
        solver_label = cmd[cmd.index("--solver-label") + 1]
        instance_list = Path(cmd[cmd.index("--list") + 1])
        rows = coverage.load_output(output) if output.exists() else []
        for instance in driver.read_instance_ids(instance_list):
            row = dict.fromkeys(coverage.OUTPUT_COLUMNS, "")
            row.update(
                solver_label=solver_label,
                instance=instance,
                cap_s=str(cap),
                result="REALIZABLE",
                seconds="0.01",
                expectation_source="none",
            )
            rows.append(row)
        coverage.atomic_write_tsv(output, coverage.OUTPUT_COLUMNS, rows)
        return subprocess.CompletedProcess(cmd, 0)

    monkeypatch.setattr(subprocess, "run", fake_run)

    regeneration_calls = []
    real_regenerate = driver.regenerate_series_summary

    def spy_regenerate(output, solver_label, instances, largest_cap):
        regeneration_calls.append(
            (output, solver_label, list(instances), largest_cap)
        )
        return real_regenerate(output, solver_label, instances, largest_cap)

    monkeypatch.setattr(driver, "regenerate_series_summary", spy_regenerate)

    rc = driver.main([
        "campaign",
        "--cap", str(cap),
        "--epoch", str(epoch),
        "--shards-dir", str(shards_dir),
        "--candidates", "B=/bin/b,S=/bin/s",
        "--tlsf-map", "unused.tsv",
        "--tlsf-corpus", "unused-corpus",
        "--preset", "test-preset",
        "--acacia-sha", "deadbeef",
        "--out-dir", str(out_dir),
    ])

    assert rc == 0
    assert len(subprocess_calls) == 2 * len(shard_paths)
    assert regeneration_calls == [
        (
            out_dir / f"B-cap{cap}-epoch{epoch}.tsv",
            f"B-cap{cap}-epoch{epoch}",
            campaign_instances,
            cap,
        ),
        (
            out_dir / f"S-cap{cap}-epoch{epoch}.tsv",
            f"S-cap{cap}-epoch{epoch}",
            campaign_instances,
            cap,
        ),
    ]
    assert all(campaign_instances != instances for instances in shard_instances)

    for label in ("B", "S"):
        solver_label = f"{label}-cap{cap}-epoch{epoch}"
        summary = out_dir / f"{solver_label}-summary.tsv"
        assert summary.is_file()
        with summary.open(encoding="utf-8", newline="") as stream:
            summary_rows = list(csv.DictReader(stream, delimiter="\t"))
        assert [row["instance"] for row in summary_rows] == campaign_instances
        assert {row["solver_label"] for row in summary_rows} == {solver_label}
        assert {row["failure_kind_at_max_cap"] for row in summary_rows} == {
            "REALIZABLE"
        }
