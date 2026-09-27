"""Offline integration tests for the evidence tools and pruning protection."""

import hashlib
import importlib.util
import fcntl
import io
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import time
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / "build_scratch"
EVIDENCE = ROOT / "scripts/acacia-evidence.py"
GUARD = ROOT / "scripts/check-evidence-growth.py"
HELD_GENERIC_SELECTION = (
    "B-obfuscated-30.list", "admission-census.tsv", "admitted-17.list",
    "admitted-60.list", "all-obfuscated.list", "master-seed.txt",
    "obfuscation-verification.tsv", "sealed-mapping.jsonl", "sealed-mapping.sha256",
    "tlsf-sources-obfuscated.tsv",
)


def run(*args, cwd=ROOT, ok=True):
    result = subprocess.run(args, cwd=cwd, text=True, capture_output=True, check=False)
    if ok and result.returncode:
        raise AssertionError(f"{args}: {result.stdout}\n{result.stderr}")
    return result


def fast_import_commit(repo, message, changes, first=False):
    header = (f"commit refs/heads/master\ncommitter Test <test@example.org> "
              f"0 +0000\ndata {len(message)}\n{message}\n")
    if not first:
        header += f"from {run('git', 'rev-parse', 'HEAD', cwd=repo).stdout.strip()}\n"
    stream = bytearray(header.encode())
    for name, data in changes.items():
        if data is None:
            stream.extend(f"D {name}\n".encode())
        else:
            stream.extend(f"M 100644 inline {name}\ndata {len(data)}\n".encode())
            stream.extend(data + b"\n")
    process = subprocess.run(["git", "fast-import", "--quiet"], input=bytes(stream),
                             cwd=repo, capture_output=True, check=False)
    if process.returncode:
        raise AssertionError(process.stderr.decode())


class EvidenceToolTests(unittest.TestCase):
    def setUp(self):
        SCRATCH.mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="evidence-test-", dir=SCRATCH)
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)

    def test_growth_guard_skips_submodule_oids_missing_from_parent(self):
        spec = importlib.util.spec_from_file_location("evidence_growth_guard", GUARD)
        assert spec and spec.loader
        guard = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(guard)
        blob, gitlink = "a" * 40, "b" * 40
        with mock.patch.object(guard, "git", return_value=f"{blob} blob 5\n{gitlink} missing\n"):
            self.assertEqual(guard.sizes({blob, gitlink}), {blob: 5})

    def test_deterministic_pack_verify_fetch_and_write_once_index(self):
        source = self.work / "report.txt"
        source.write_text("original report\n")
        member = source.relative_to(ROOT).as_posix()
        archives = []
        for number in (1, 2):
            out = self.work / f"out-{number}"
            run("python3", "-s", str(EVIDENCE), "pack", "--campaign", "case-1",
                "--status", "closed", "--members", member, "--out", str(out),
                "--source-revision", "abc", "--binary-sha256", "a" * 64)
            archives.append(out / "case-1.tar.gz")
        self.assertEqual(archives[0].read_bytes(), archives[1].read_bytes())
        sha = hashlib.sha256(archives[0].read_bytes()).hexdigest()
        size = archives[0].stat().st_size
        run("python3", "-s", str(EVIDENCE), "verify", str(archives[0]), "--sha256", sha)
        index = self.work / "index.tsv"
        index.write_text((ROOT / "benchmarking/evidence-index.tsv").read_text())
        add = ("python3", "-s", str(EVIDENCE), "index-add", "--index", str(index),
               "--campaign", "case-1", "--status", "closed", "--asset-name", "case-1.tar.gz",
               "--url", archives[0].as_uri(), "--sha256", sha, "--bytes", str(size),
               "--supports", "test conclusion", "--source-revisions", "abc",
               "--binary-sha256s", "a" * 64)
        run(*add)
        self.assertNotEqual(run(*add, ok=False).returncode, 0)
        amended = ["case-2" if value == "case-1" else value for value in add]
        run(*amended, "--amends", "case-1")
        self.assertIn("amends case-1", index.read_text())
        dest = self.work / "restore"
        run("python3", "-s", str(EVIDENCE), "fetch", "--index", str(index),
            "--campaign", "case-1", "--dest", str(dest), "--from-file", str(archives[0]))
        self.assertEqual((dest / member).read_bytes(), source.read_bytes())
        self.assertTrue((dest / "MANIFEST.tsv").is_file())
        self.assertTrue((dest / "CAMPAIGN.json").is_file())
        file_url_dest = self.work / "file-url-restore"
        run("python3", "-s", str(EVIDENCE), "fetch", "--index", str(index),
            "--campaign", "case-1", "--dest", str(file_url_dest))
        self.assertEqual((file_url_dest / member).read_bytes(), source.read_bytes())
        bad = self.work / "tampered.tar.gz"
        bad.write_bytes(archives[0].read_bytes() + b"changed")
        result = run("python3", "-s", str(EVIDENCE), "fetch", "--index", str(index),
                     "--campaign", "case-1", "--dest", str(self.work / "bad-restore"),
                     "--from-file", str(bad), ok=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.work / "bad-restore" / member).exists())
        changed = self.work / "changed-member.tar.gz"
        with tarfile.open(archives[0], "r:gz") as source_tar:
            with tarfile.open(changed, "w:gz") as changed_tar:
                for info in source_tar:
                    content = source_tar.extractfile(info).read()
                    if info.name == member:
                        content = b"Original report\n"
                        info.size = len(content)
                    changed_tar.addfile(info, io.BytesIO(content))
        result = run("python3", "-s", str(EVIDENCE), "verify", str(changed), ok=False)
        self.assertIn("member SHA-256 mismatch", result.stderr)

    def test_reject_symlink_sealed_path_and_unsafe_archive(self):
        link = self.work / "link.txt"
        link.symlink_to(self.work / "target")
        result = run("python3", "-s", str(EVIDENCE), "pack", "--campaign", "bad",
                     "--status", "partial", "--members", link.relative_to(ROOT).as_posix(),
                     "--out", str(self.work / "out"), ok=False)
        self.assertNotEqual(result.returncode, 0)
        sealed = self.work / "sealed-mapping.jsonl"
        sealed.write_text("secret")
        result = run("python3", "-s", str(EVIDENCE), "pack", "--campaign", "bad",
                     "--status", "partial", "--members", sealed.relative_to(ROOT).as_posix(),
                     "--out", str(self.work / "out"), ok=False)
        self.assertNotEqual(result.returncode, 0)
        archive = self.work / "unsafe.tar.gz"
        with tarfile.open(archive, "w:gz") as tar:
            info = tarfile.TarInfo("../escape")
            info.size = 1
            tar.addfile(info, io.BytesIO(b"x"))
        result = run("python3", "-s", str(EVIDENCE), "verify", str(archive), ok=False)
        self.assertNotEqual(result.returncode, 0)
        link_archive = self.work / "link.tar.gz"
        with tarfile.open(link_archive, "w:gz") as tar:
            info = tarfile.TarInfo("bad-link")
            info.type = tarfile.SYMTYPE
            info.linkname = "../escape"
            tar.addfile(info)
        result = run("python3", "-s", str(EVIDENCE), "verify", str(link_archive), ok=False)
        self.assertIn("link, device", result.stderr)

    def test_publication_hold_covers_audit_paths_and_relocated_mapping(self):
        paths = ["benchmarking/gr1-par2-20260923/campaign/generic-selection/" + name
                 for name in HELD_GENERIC_SELECTION]
        for number, path in enumerate(paths):
            with self.subTest(path=path):
                result = run("python3", "-s", str(EVIDENCE), "pack", "--campaign",
                             f"held-{number}", "--status", "partial", "--members", path,
                             "--out", str(self.work / "held"), ok=False)
                self.assertIn("do-not-publish", result.stderr)
        result = run("python3", "-s", str(EVIDENCE), "pack", "--campaign", "held-nested",
                     "--status", "partial", "--members",
                     "benchmarking/gr1-par2-20260923/campaign/generic-selection/nested/other.txt",
                     "--out", str(self.work / "held"), ok=False)
        self.assertIn("do-not-publish", result.stderr)
        for suffix, content in (("tsv", "original_id\tobfuscated_id\nold\tnew\n"),
                                ("csv", "original_id,obfuscated_id\nold,new\n"),
                                ("jsonl", '{"original_id":"old","obfuscated_id":"new"}\n'),
                                ("json", '{\n"original_id": "old",\n"obfuscated_id": "new"\n}\n')):
            mapping = self.work / f"relocated.{suffix}"
            mapping.write_text(content)
            result = run("python3", "-s", str(EVIDENCE), "pack", "--campaign",
                         f"relocated-{suffix}", "--status", "partial", "--members",
                         mapping.relative_to(ROOT).as_posix(), "--out", str(self.work / "held"),
                         ok=False)
            self.assertIn("do-not-publish", result.stderr)

    def test_fetch_checks_index_provenance_and_canonical_lists(self):
        source = self.work / "report.txt"
        source.write_text("report\n")
        out = self.work / "archives"
        run("python3", "-s", str(EVIDENCE), "pack", "--campaign", "provenance",
            "--status", "closed", "--members", source.relative_to(ROOT).as_posix(),
            "--out", str(out), "--source-revision", "z-rev", "--source-revision", "a-rev",
            "--binary-sha256", "b" * 64, "--binary-sha256", "a" * 64)
        archive = out / "provenance.tar.gz"
        index = self.work / "provenance-index.tsv"
        index.write_text((ROOT / "benchmarking/evidence-index.tsv").read_text())
        add = ["python3", "-s", str(EVIDENCE), "index-add", "--index", str(index),
               "--campaign", "provenance", "--status", "closed", "--asset-name",
               "provenance.tar.gz", "--url", archive.as_uri(), "--sha256",
               hashlib.sha256(archive.read_bytes()).hexdigest(), "--bytes", str(archive.stat().st_size),
               "--supports", "test", "--source-revisions", "z-rev,a-rev",
               "--binary-sha256s", f"{'b' * 64},{'a' * 64}"]
        run(*add)
        lines = index.read_text().splitlines()
        row_index = next(i for i, line in enumerate(lines)
                         if line.startswith("provenance\t"))
        row = lines[row_index].split("\t")
        self.assertEqual(row[7], "a-rev,z-rev")
        self.assertEqual(row[8], f"{'a' * 64},{'b' * 64}")
        for column, wrong in ((7, "different-rev"), (8, "c" * 64)):
            with self.subTest(column=column):
                lines = index.read_text().splitlines()
                cells = lines[row_index].split("\t")
                cells[column] = wrong
                lines[row_index] = "\t".join(cells)
                index.write_text("\n".join(lines) + "\n")
                result = run("python3", "-s", str(EVIDENCE), "fetch", "--index", str(index),
                             "--campaign", "provenance", "--dest",
                             str(self.work / f"wrong-{column}"), "--from-file", str(archive), ok=False)
                self.assertIn("provenance", result.stderr)
                lines[row_index] = "\t".join(row)
                index.write_text("\n".join(lines) + "\n")

    def test_index_add_waits_for_lock_file(self):
        index = self.work / "locked-index.tsv"
        index.write_text((ROOT / "benchmarking/evidence-index.tsv").read_text())
        command = ["python3", "-s", str(EVIDENCE), "index-add", "--index", str(index),
                   "--campaign", "locked", "--status", "closed", "--asset-name", "locked.tar.gz",
                   "--url", "file:///archive", "--sha256", "a" * 64, "--bytes", "1",
                   "--supports", "test", "--source-revisions", "rev", "--binary-sha256s", ""]
        with (index.parent / (index.name + ".lock")).open("a+") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, text=True)
            try:
                time.sleep(0.3)
                self.assertIsNone(process.poll(), "index-add ignored the index lock")
            finally:
                fcntl.flock(lock, fcntl.LOCK_UN)
                stdout, stderr = process.communicate(timeout=10)
        self.assertEqual(process.returncode, 0, stdout + stderr)

    def test_history_guard_raw_compressed_rename_deleted_and_allowlisted(self):
        repo = self.work / "history"
        repo.mkdir()
        (repo / "scripts").mkdir()
        shutil.copy2(GUARD, repo / "scripts/check-evidence-growth.py")
        (repo / "benchmarking").mkdir()
        allowlist = repo / "benchmarking/evidence-growth-allowlist.tsv"
        allowlist.write_text("path\treason\nbenchmarking/fixture.tsv\tSmall focused fixture\n")
        run("git", "init", "-q", "-b", "master", cwd=repo)

        def commit(message, changes, first=False):
            # Fast-import builds throwaway history without staging the workspace.
            header = (f"commit refs/heads/master\ncommitter Test <test@example.org> "
                      f"0 +0000\ndata {len(message)}\n{message}\n")
            if not first:
                parent = run("git", "rev-parse", "HEAD", cwd=repo).stdout.strip()
                header += f"from {parent}\n"
            stream = bytearray(header.encode())
            for name, data in changes.items():
                if data is None:
                    stream.extend(f"D {name}\n".encode())
                else:
                    stream.extend(f"M 100644 inline {name}\ndata {len(data)}\n".encode())
                    stream.extend(data + b"\n")
            process = subprocess.run(["git", "fast-import", "--quiet"], input=bytes(stream),
                                     cwd=repo, capture_output=True, check=False)
            if process.returncode:
                raise AssertionError(process.stderr.decode())

        commit("base", {
            "scripts/check-evidence-growth.py": GUARD.read_bytes(),
            "benchmarking/evidence-growth-allowlist.tsv": allowlist.read_bytes(),
        }, first=True)
        base = run("git", "rev-parse", "HEAD", cwd=repo).stdout.strip()
        guard = repo / "scripts/check-evidence-growth.py"

        def check(expected):
            result = run("python3", "-s", str(guard), "--range", f"{base}..HEAD",
                         ok=False)
            self.assertEqual(result.returncode == 0, expected, result.stdout + result.stderr)
            return result.stdout

        fixture = repo / "benchmarking/fixture.tsv"
        fixture.write_text("small fixture\n")
        commit("allowlisted fixture", {"benchmarking/fixture.tsv": fixture.read_bytes()})
        check(True)
        raw = repo / "benchmarking/raw.tsv"
        raw.write_text("raw campaign\n")
        commit("raw", {"benchmarking/raw.tsv": raw.read_bytes()})
        self.assertIn("raw evidence", check(False))
        raw.unlink()
        commit("delete raw", {"benchmarking/raw.tsv": None})
        self.assertIn("raw evidence", check(False))
        compressed = repo / "benchmarking/campaign.tar.gz"
        compressed.write_bytes(b"compressed")
        commit("archive", {"benchmarking/campaign.tar.gz": compressed.read_bytes()})
        self.assertIn("campaign.tar.gz", check(False))
        compressed.rename(repo / "benchmarking/renamed.tar.gz")
        commit("rename archive", {"benchmarking/campaign.tar.gz": None,
                                  "benchmarking/renamed.tar.gz":
                                  (repo / "benchmarking/renamed.tar.gz").read_bytes()})
        self.assertIn("renamed.tar.gz", check(False))
        large = repo / "benchmarking/report-large.json"
        large.write_bytes(b"L" * (270 * 1024))
        commit("large generated report", {"benchmarking/report-large.json": large.read_bytes()})
        self.assertIn("over 256 KiB", check(False))
        presentations = {}
        for number in range(11):
            name = f"benchmarking/report-{number}.json"
            data = bytes([number]) * (200 * 1024)
            (repo / name).write_bytes(data)
            presentations[name] = data
        commit("presentation budget", presentations)
        self.assertIn("exceeds 2 MiB", check(False))

    def test_guard_classifies_inherited_renames_copies_and_raw_json(self):
        repo = self.work / "inherited"
        (repo / "scripts").mkdir(parents=True)
        (repo / "benchmarking").mkdir()
        shutil.copy2(GUARD, repo / "scripts/check-evidence-growth.py")
        (repo / "benchmarking/evidence-growth-allowlist.tsv").write_text("path\treason\n")
        run("git", "init", "-q", "-b", "master", cwd=repo)
        inherited = b"inherited raw bytes\n"
        fast_import_commit(repo, "base", {
            "scripts/check-evidence-growth.py": GUARD.read_bytes(),
            "benchmarking/evidence-growth-allowlist.tsv": b"path\treason\n",
            "inherited/data.tsv": inherited,
        }, first=True)
        base = run("git", "rev-parse", "HEAD", cwd=repo).stdout.strip()
        fast_import_commit(repo, "rename and copy inherited blob", {
            "inherited/data.tsv": None,
            "benchmarking/new-campaign/raw.tsv": inherited,
            "benchmarking/new-campaign/copy.csv": inherited,
        })
        result = run("python3", "-s", str(repo / "scripts/check-evidence-growth.py"),
                     "--range", f"{base}..HEAD", ok=False)
        self.assertIn("raw.tsv", result.stdout)
        self.assertIn("copy.csv", result.stdout)
        self.assertNotEqual(result.returncode, 0)

        raw_json = b"x" * (300 * 1024)
        fast_import_commit(repo, "raw JSON trace", {
            "benchmarking/gr1-par2-20260923/s0/raw-case/trace.json": raw_json,
            "benchmarking/gr1-par2-20260923/campaign/trace.json": b"{}\n",
            "benchmarking/gr1-par2-20260923/campaign/diagnostics/row.json": b"{}\n",
            "benchmarking/gr1-par2-20260923/campaign/schema.json": b"{}\n",
            "benchmarking/gr1-par2-20260923/s0/raw-case/schema.json": b"{}\n",
            "benchmarking/gr1-par2-20260923/s0/raw-case/config.json": b"{}\n",
        })
        result = run("python3", "-s", str(repo / "scripts/check-evidence-growth.py"),
                     "--range", f"{base}..HEAD", ok=False)
        self.assertIn("raw evidence: benchmarking/gr1-par2-20260923/s0/raw-case/trace.json",
                      result.stdout)
        self.assertIn("raw evidence: benchmarking/gr1-par2-20260923/campaign/trace.json",
                      result.stdout)
        self.assertIn("raw evidence: benchmarking/gr1-par2-20260923/campaign/diagnostics/row.json",
                      result.stdout)
        self.assertNotIn("raw evidence: benchmarking/gr1-par2-20260923/campaign/schema.json",
                         result.stdout)
        self.assertNotIn("raw evidence: benchmarking/gr1-par2-20260923/s0/raw-case/schema.json",
                         result.stdout)
        self.assertNotIn("raw evidence: benchmarking/gr1-par2-20260923/s0/raw-case/config.json",
                         result.stdout)

    def test_guard_ignores_evidence_words_in_source_names(self):
        repo = self.work / "source-names"
        (repo / "scripts").mkdir(parents=True)
        (repo / "benchmarking").mkdir()
        shutil.copy2(GUARD, repo / "scripts/check-evidence-growth.py")
        (repo / "benchmarking/evidence-growth-allowlist.tsv").write_text("path\treason\n")
        run("git", "init", "-q", "-b", "master", cwd=repo)
        fast_import_commit(repo, "base", {
            "scripts/check-evidence-growth.py": GUARD.read_bytes(),
            "benchmarking/evidence-growth-allowlist.tsv": b"path\treason\n",
        }, first=True)
        base = run("git", "rev-parse", "HEAD", cwd=repo).stdout.strip()
        source_names = (
            "benchmarking/tools/thermal-annotate.py",
            "benchmarking/tools/thermal-check.sh",
            "benchmarking/thermal/proof-bundle.cc",
            "benchmarking/proofs/thermal-helper.hh",
            "benchmarking/proof/thermal.c",
            "benchmarking/tools/thermal.cpp",
            "benchmarking/tools/thermal.rs",
            "benchmarking/tools/thermal.toml",
            "benchmarking/tools/thermal.hpp",
            "benchmarking/tools/thermal.yaml",
        )
        fast_import_commit(repo, "source files", {name: b"source\n" for name in source_names})
        guard = repo / "scripts/check-evidence-growth.py"
        result = run("python3", "-s", str(guard), "--range", f"{base}..HEAD", ok=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS no new evidence growth violations", result.stdout)

        raw_names = (
            "benchmarking/thermal-samples.tsv",
            "benchmarking/tools/thermal-annotate.py.log",
            "benchmarking/thermal/proof-bundle.zip",
        )
        fast_import_commit(repo, "raw thermal samples", {name: b"samples\n" for name in raw_names})
        result = run("python3", "-s", str(guard), "--range", f"{base}..HEAD", ok=False)
        self.assertNotEqual(result.returncode, 0)
        for name in raw_names:
            with self.subTest(path=name):
                self.assertIn(f"FAIL raw evidence: {name} ", result.stdout)

    def test_guard_accepts_complete_tooling_commit(self):
        repo = self.work / "clean-tooling"
        (repo / "scripts").mkdir(parents=True)
        (repo / "benchmarking").mkdir()
        shutil.copy2(GUARD, repo / "scripts/check-evidence-growth.py")
        shutil.copy2(ROOT / "benchmarking/evidence-growth-allowlist.tsv",
                     repo / "benchmarking/evidence-growth-allowlist.tsv")
        run("git", "init", "-q", "-b", "master", cwd=repo)
        fast_import_commit(repo, "base", {"README.md": b"base\n"}, first=True)
        base = run("git", "rev-parse", "HEAD", cwd=repo).stdout.strip()
        tooling = {name: (ROOT / name).read_bytes() for name in (
            "scripts/acacia-evidence.py", "scripts/check-evidence-growth.py",
            "scripts/prune-artifacts.sh", "tests/pytest/test_evidence_tools.py",
            ".github/workflows/main.yml", "benchmarking/evidence-index.tsv",
            "benchmarking/baselines.tsv", "benchmarking/evidence-do-not-publish.txt",
            "benchmarking/evidence-growth-allowlist.tsv")}
        fast_import_commit(repo, "tooling", tooling)
        result = run("python3", "-s", str(repo / "scripts/check-evidence-growth.py"),
                     "--range", f"{base}..HEAD", ok=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_ci_uses_pr_base_and_push_fallback(self):
        workflow = (ROOT / ".github/workflows/main.yml").read_text()
        self.assertIn("github.event.pull_request.base.sha", workflow)
        self.assertIn("git merge-base", workflow)
        self.assertIn("DEFAULT_BRANCH", workflow)
        self.assertNotIn("pull_request:\n    branches:", workflow)

    def test_prune_protects_registry_paths_and_hashes(self):
        repo = self.work / "prune"
        (repo / "scripts").mkdir(parents=True)
        (repo / "benchmarking").mkdir()
        shutil.copy2(ROOT / "scripts/prune-artifacts.sh", repo / "scripts/prune-artifacts.sh")
        (repo / "scripts/acacia-config.py").write_text("print('')\n")
        run("git", "init", "-q", cwd=repo)
        binary = repo / "build_hash/src/acacia-bonsai"
        binary.parent.mkdir(parents=True)
        binary.write_bytes(b"frozen binary")
        sha = hashlib.sha256(binary.read_bytes()).hexdigest()
        (repo / "build_named").mkdir()
        (repo / "build_disposable").mkdir()
        (repo / "benchmarking/baselines.tsv").write_text(
            f"name\tpath\tsha256\trole\nB\tbuild_named\t{sha}\tbaseline\n")
        (repo / "benchmarking/evidence-index.tsv").write_text(
            "campaign_id\tstatus\tasset_name\turl\tsha256\tbytes\tsupports\t"
            f"source_revisions\tbinary_sha256s\nC\tclosed\tx.tar.gz\tfile:///x\t{sha}\t1\t"
            "build_named\tx\t" + sha + "\n")
        output = run("bash", "scripts/prune-artifacts.sh", cwd=repo).stdout
        self.assertIn("kept:  2 directories", output)
        self.assertIn("prune: 1 directories", output)
        self.assertTrue(binary.is_file())


if __name__ == "__main__":
    unittest.main()
