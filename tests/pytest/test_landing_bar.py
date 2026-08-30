import csv
import importlib.util
import pathlib
import sys
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "benchmarking" / "landing-bar.py"
SPEC = importlib.util.spec_from_file_location("landing_bar", SCRIPT)
landing_bar = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = landing_bar
SPEC.loader.exec_module(landing_bar)


def write_rows(path, rows):
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle, fieldnames=["suite", "instance", "result", "seconds", "exit"]
        )
        writer.writeheader()
        writer.writerows(rows)


def row(instance, result, seconds=1.0, suite="panel"):
    return {
        "suite": suite,
        "instance": instance,
        "result": result,
        "seconds": seconds,
        "exit": 0,
    }


def test_gate_passes_without_lost_answers(tmp_path, capsys):
    baseline = tmp_path / "baseline.csv"
    candidate = tmp_path / "candidate.csv"
    write_rows(baseline, [row("a.ltl", "REALIZABLE"), row("b.ltl", "TIMEOUT", 17)])
    write_rows(candidate, [row("a.ltl", "REALIZABLE", 0.5), row("b.ltl", "UNREALIZABLE")])

    assert landing_bar.main([str(baseline), str(candidate)]) == 0
    assert capsys.readouterr().out.rstrip().endswith("GATE PASS")


def test_gate_rejects_lost_and_flipped_answers(tmp_path, capsys):
    baseline = tmp_path / "baseline.csv"
    candidate = tmp_path / "candidate.csv"
    write_rows(baseline, [row("a.ltl", "REALIZABLE"), row("b.ltl", "UNREALIZABLE")])
    write_rows(candidate, [row("a.ltl", "TIMEOUT", 17), row("b.ltl", "REALIZABLE")])

    assert landing_bar.main([str(baseline), str(candidate)]) == 1
    output = capsys.readouterr().out
    assert "lost panel/a.ltl" in output
    assert "verdict changed panel/b.ltl" in output
    assert output.rstrip().endswith("GATE FAIL: 2 instance comparison failure(s)")


def test_gate_rejects_incomplete_campaigns(tmp_path, capsys):
    baseline = tmp_path / "baseline.csv"
    candidate = tmp_path / "candidate.csv"
    write_rows(baseline, [row("a.ltl", "TIMEOUT", 17)])
    write_rows(candidate, [row("b.ltl", "UNKNOWN")])

    assert landing_bar.main([str(baseline), str(candidate)]) == 1
    output = capsys.readouterr().out
    assert "missing candidate row: panel/a.ltl" in output
    assert "unexpected candidate row: panel/b.ltl" in output


def test_near_cap_timeout_is_rescued_by_extended_remeasurement(
    tmp_path, capsys, monkeypatch
):
    baseline = tmp_path / "baseline.csv"
    candidate = tmp_path / "candidate.csv"
    write_rows(baseline, [row("fragile.ltl", "REALIZABLE", 15.9)])
    write_rows(candidate, [row("fragile.ltl", "TIMEOUT", 17.0)])

    calls = []

    def fake_run_solver(binary, instances_dir, key, timeout, memory_max, memory_swap_max):
        calls.append((binary.name, key, timeout, memory_max, memory_swap_max))
        return (
            landing_bar.Result("REALIZABLE", "solved", 16.2),
            "REALIZABLE\n",
            "",
            [str(binary), "-F", key],
        )

    monkeypatch.setattr(landing_bar, "run_solver", fake_run_solver)
    baseline_bin = tmp_path / "baseline-bin"
    candidate_bin = tmp_path / "candidate-bin"
    assert landing_bar.main(
        [
            str(baseline),
            str(candidate),
            "--baseline-bin",
            str(baseline_bin),
            "--candidate-bin",
            str(candidate_bin),
        ]
    ) == 0
    assert calls == [
        ("baseline-bin", "panel/fragile.ltl", 51.0, "8G", "0"),
        ("candidate-bin", "panel/fragile.ltl", 51.0, "8G", "0"),
    ]
    output = capsys.readouterr().out
    assert "REMEASURE panel/fragile.ltl" in output
    assert output.rstrip().endswith("GATE PASS")


def test_near_cap_timeout_still_fails_when_extended_run_does_not_answer(
    tmp_path, capsys, monkeypatch
):
    baseline = tmp_path / "baseline.csv"
    candidate = tmp_path / "candidate.csv"
    write_rows(baseline, [row("fragile.ltl", "REALIZABLE", 15.9)])
    write_rows(candidate, [row("fragile.ltl", "TIMEOUT", 17.0)])

    answers = iter(
        [
            landing_bar.Result("REALIZABLE", "solved", 16.1),
            landing_bar.Result(None, "timeout", 51.0),
        ]
    )

    def fake_run_solver(binary, instances_dir, key, timeout, memory_max, memory_swap_max):
        return next(answers), "", "", [str(binary), "-F", key]

    monkeypatch.setattr(landing_bar, "run_solver", fake_run_solver)
    assert landing_bar.main(
        [
            str(baseline),
            str(candidate),
            "--baseline-bin",
            str(tmp_path / "baseline-bin"),
            "--candidate-bin",
            str(tmp_path / "candidate-bin"),
        ]
    ) == 1
    output = capsys.readouterr().out
    assert "lost panel/fragile.ltl after 51s remeasurement" in output
    assert output.rstrip().endswith("GATE FAIL: 1 instance comparison failure(s)")


def test_multi_suite_source_maps_resolve_empty_partition_side(
    tmp_path, monkeypatch
):
    ltl_root = tmp_path / "ltl"
    ltl_root.mkdir()
    ltl = ltl_root / "content.ltl"
    ltl.write_text("G i\n")
    ltl.with_suffix(".part").write_text(".inputs i\n.outputs\n")
    source_map = tmp_path / "sources.tsv"
    source_map.write_text("instance\tsource\nfragile.ltl\tcontent.ltl\n")

    solver = tmp_path / "solver"
    solver.write_text(
        "#!/bin/sh\n"
        "test \"$1\" = -F && test \"$3\" = -i && test \"$4\" = i && "
        "test \"$5\" = -o && test \"$6\" = \"\" || exit 9\n"
        "printf 'UNREALIZABLE\\n'\n"
        "exit 1\n"
    )
    solver.chmod(0o755)
    monkeypatch.setenv("ACACIA_OUTER_CGROUP", "1")
    monkeypatch.setattr(
        landing_bar,
        "load_source_map",
        lambda path: {"fragile.ltl": ltl} if path == source_map else {},
    )

    result, stdout, stderr, command = landing_bar.run_solver(
        solver,
        {"panel": source_map},
        "panel/fragile.ltl",
        1.0,
        "8G",
        "0",
    )

    assert result.verdict == "UNREALIZABLE"
    assert result.kind == "solved"
    assert stdout == "UNREALIZABLE\n"
    assert stderr == ""
    assert command[-2:] == ["-o", ""]


def test_run_solver_builds_tlsf_command(tmp_path):
    tlsf = tmp_path / "example.tlsf"
    tlsf.write_text("INFO {}\nMAIN {}\n")
    solver = tmp_path / "solver"
    solver.write_text("#!/bin/sh\nprintf 'REALIZABLE\\n'\n")
    solver.chmod(0o755)

    with mock.patch.dict("os.environ", {"ACACIA_OUTER_CGROUP": "1"}):
        result, stdout, stderr, command = landing_bar.run_solver(
            solver,
            tmp_path,
            tlsf.name,
            1.0,
            "8G",
            "0",
        )

    assert result.verdict == "REALIZABLE"
    assert stdout == "REALIZABLE\n"
    assert stderr == ""
    assert command == [str(solver), "-T", str(tlsf)]


def test_run_solver_keeps_ltl_partition_command(tmp_path):
    suite_dir = tmp_path / "tests" / "suites" / "benchmarks" / "panel"
    suite_dir.mkdir(parents=True)
    ltl_root = tmp_path / "tests" / "ltl"
    ltl_root.mkdir(parents=True)
    ltl = ltl_root / "content.ltl"
    ltl.write_text("G request\n")
    ltl.with_suffix(".part").write_text(".inputs request\n.outputs grant\n")
    source_map = suite_dir / "sources.tsv"
    source_map.write_text("instance\tsource\nexample.ltl\tcontent.ltl\n")
    tlsf_source_map = suite_dir / "tlsf-sources.tsv"
    tlsf_source_map.write_text("instance\ttlsf\nexample.ltl\texample.tlsf\n")
    tlsf_corpus = tmp_path / "corpus"
    tlsf_corpus.mkdir()
    (tlsf_corpus / "example.tlsf").write_text("INFO {}\nMAIN {}\n")
    solver = tmp_path / "solver"
    solver.write_text("#!/bin/sh\nprintf 'REALIZABLE\\n'\n")
    solver.chmod(0o755)

    with mock.patch.dict("os.environ", {"ACACIA_OUTER_CGROUP": "1"}):
        result, stdout, stderr, command = landing_bar.run_solver(
            solver,
            {"panel": source_map},
            "panel/example.ltl",
            1.0,
            "8G",
            "0",
            tlsf_source_maps={"panel": tlsf_source_map},
            tlsf_corpus=tlsf_corpus,
        )

    assert result.verdict == "REALIZABLE"
    assert stdout == "REALIZABLE\n"
    assert stderr == ""
    assert command == [
        str(solver),
        "-F",
        str(ltl),
        "-i",
        "request",
        "-o",
        "grant",
    ]


def test_tlsf_source_map_resolves_through_cli_corpus(tmp_path):
    baseline = tmp_path / "baseline.csv"
    candidate = tmp_path / "candidate.csv"
    write_rows(baseline, [row("fragile.ltl", "REALIZABLE", 15.9)])
    write_rows(candidate, [row("fragile.ltl", "TIMEOUT", 17.0)])

    corpus = tmp_path / "corpus"
    nested = corpus / "nested"
    nested.mkdir(parents=True)
    tlsf = nested / "fragile.tlsf"
    tlsf.write_text("INFO {}\nMAIN {}\n")
    tlsf_source_map = tmp_path / "tlsf-sources.tsv"
    tlsf_source_map.write_text(
        "instance\ttlsf\nfragile.ltl\tnested/fragile.tlsf\n"
    )
    solver = tmp_path / "solver"
    solver.write_text("#!/bin/sh\nprintf 'REALIZABLE\\n'\n")
    solver.chmod(0o755)

    with mock.patch.dict("os.environ", {"ACACIA_OUTER_CGROUP": "1"}):
        status = landing_bar.main(
            [
                str(baseline),
                str(candidate),
                "--baseline-bin",
                str(solver),
                "--candidate-bin",
                str(solver),
                "--tlsf-source-map",
                f"panel={tlsf_source_map}",
                "--tlsf-corpus",
                str(corpus),
            ]
        )

    assert status == 0


def test_tlsf_fallback_reaches_bare_instance_keys(tmp_path):
    """The campaign keys rows by bare instance name, with no suite prefix.

    A TLSF-only instance that lands near the cap has to be remeasurable through
    that call shape; requiring `suite/instance` made the fallback unreachable
    and turned the remeasurement into a reported loss.
    """
    suite_dir = tmp_path / "tests" / "suites" / "benchmarks" / "panel"
    suite_dir.mkdir(parents=True)
    source_map = suite_dir / "sources.tsv"
    source_map.write_text("instance\tsource\nother.ltl\tother.ltl\n")
    tlsf_source_map = suite_dir / "tlsf-sources.tsv"
    tlsf_source_map.write_text("instance\ttlsf\ntlsf-only.ltl\ttlsf-only.tlsf\n")
    tlsf_corpus = tmp_path / "corpus"
    tlsf_corpus.mkdir()
    (tlsf_corpus / "tlsf-only.tlsf").write_text("INFO {}\nMAIN {}\n")
    solver = tmp_path / "solver"
    solver.write_text("#!/bin/sh\nprintf 'REALIZABLE\\n'\n")
    solver.chmod(0o755)

    with mock.patch.dict("os.environ", {"ACACIA_OUTER_CGROUP": "1"}):
        result, _stdout, _stderr, command = landing_bar.run_solver(
            solver,
            source_map,
            "tlsf-only.ltl",
            1.0,
            "8G",
            "0",
            tlsf_source_maps={"panel": tlsf_source_map},
            tlsf_corpus=tlsf_corpus,
        )

    assert result.verdict == "REALIZABLE"
    assert command == [str(solver), "-T", str(tlsf_corpus / "tlsf-only.tlsf")]
