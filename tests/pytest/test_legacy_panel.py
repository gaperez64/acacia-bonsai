from __future__ import annotations

import importlib.util
import hashlib
import json
import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'benchmarking'))


def module():
    spec = importlib.util.spec_from_file_location(
        'legacy_panel', ROOT / 'benchmarking/legacy-panel.py')
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def synthetic_rows():
    rows = []
    for index in range(48):
        cohort = ('ltlsynt-only', 'Acacia-only', 'common-solved', 'both-unsolved')[index % 4]
        rows.append(dict(instance=f'input-{index}', set=cohort,
                         source_structural_sha256=hashlib.sha256(str(index).encode()).hexdigest(),
                         polarity='REALIZABLE' if index % 8 < 4 else 'UNREALIZABLE',
                         tlsf_bytes=str(256 << (index % 3)), inputs='2', outputs='3',
                         parameter_dimension=str(index % 2),
                         native_rejections=['budget-structure' if index % 2 else 'mp-class'],
                         legacy_last_stages=['translation' if index % 3 else 'search'],
                         near_cap=cohort == 'common-solved', equivariance_gain=index == 1,
                         comparator_result=('TIMEOUT' if cohort == 'both-unsolved'
                                            else 'REALIZABLE')))
    return rows


def test_seeded_panel_covers_evidence_and_preservation():
    tool = module()
    rows = synthetic_rows()
    panel, strata = tool.select(rows, 28, 207)
    assert panel == tool.select(list(reversed(rows)), 28, 207)[0]
    assert len({r['instance'] for r in panel}) == 28
    assert set(strata) <= set().union(*(tool.features(r) for r in panel))
    assert sum(r['set'] == 'both-unsolved' for r in panel) == 4
    assert sum(r['set'] == 'Acacia-only' for r in panel) >= 3
    assert sum(r['near_cap'] for r in panel) >= 3
    assert any(r['equivariance_gain'] for r in panel)
    assert all(r['selection_reasons'] for r in panel)


def test_panel_selection_survives_renamed_sources(tmp_path):
    tool = module()
    rows = synthetic_rows()
    names = list(range(len(rows)))
    random.Random(912).shuffle(names)
    renamed = []
    for row, name in zip(rows, names):
        source = ('INFO { TITLE: "source" } MAIN { GUARANTEES { '
                  f'G(p{row["instance"][6:]}); }} }}')
        original = tmp_path / (row['instance'] + '.tlsf')
        copy = tmp_path / f'renamed-{name}.tlsf'
        original.write_text(source)
        copy.write_text(source)
        row['tlsf_file'] = original.name
        row['source_sha256'] = hashlib.sha256(original.read_bytes()).hexdigest()
        renamed.append({**row, 'instance': f'renamed-{name}', 'tlsf_file': copy.name,
                        'origin': f'direct:{copy.name}'})
    if hasattr(tool, 'add_structural_digests'):
        tool.add_structural_digests(rows, tmp_path)
        tool.add_structural_digests(renamed, tmp_path)
    first = tool.select(rows, 28, 207)[0]
    second = tool.select(list(reversed(renamed)), 28, 207)[0]
    def structural(panel):
        return [{k: v for k, v in row.items() if k not in {'instance', 'tlsf_file', 'origin'}}
                for row in panel]
    assert structural(first) == structural(second)


def test_last_stage_uses_parent_child_chronology(tmp_path, monkeypatch):
    tool = module()
    monkeypatch.setattr(tool, 'phase_record_dir', lambda *args: tmp_path)
    parent = dict(event='stage_censored', worker_pid=20, seq=4, mono_ns=210,
                  stage_id=2, stage='verification', entry_ns=180)
    child = dict(event='stage_entry', worker_pid=20, seq=3, mono_ns=130,
                 requested_backend='forward', stage_id=1, stage='search')
    (tmp_path / '10.jsonl').write_text(json.dumps(parent) + '\n')
    (tmp_path / '20.jsonl').write_text(json.dumps(child) + '\n')
    row = dict(instance='input', native_rejections=[], legacy_last_stages=[])
    evidence = dict(solver_label='A', cap_s=17, run_index=0)
    tool.add_phase_evidence([row], {'input': evidence}, tmp_path)
    assert row['legacy_last_stages'] == ['forward:verification']
    assert row['phase_delivery'] == 'partial'


def test_panel_propagates_delivery_evidence(tmp_path, monkeypatch):
    tool = module()
    monkeypatch.setattr(tool, 'phase_record_dir', lambda *args: tmp_path)
    evidence = dict(solver_label='A', cap_s=17, run_index=0)
    entry = dict(event='stage_entry', worker_pid=20, seq=1, mono_ns=100,
                 requested_backend='forward', stage_id=1, stage='search')
    writer = dict(event='writer_summary', incomplete_packet=False, failed_records=0)
    for events, expected in [([entry], 'unconfirmed'),
                             ([entry, writer], 'observed'),
                             ([entry, {**writer, 'failed_records': 1}], 'partial'),
                             ([{**entry, 'seq': 2}, writer], 'partial'),
                             ([{**entry, 'dropped_records': 1}, writer], 'partial')]:
        (tmp_path / '20.jsonl').write_text(''.join(json.dumps(e) + '\n' for e in events))
        row = dict(instance='input', native_rejections=[], legacy_last_stages=[])
        tool.add_phase_evidence([row], {'input': evidence}, tmp_path)
        assert row['phase_delivery'] == expected


def test_source_digest_ignores_descriptions_comments_and_whitespace(tmp_path):
    source = 'INFO { TITLE: "original" } MAIN { GUARANTEES { G(p); } }'
    renamed = 'INFO { TITLE: "renamed" } // file name is output metadata\nMAIN{GUARANTEES{G(p);}}'
    rows = []
    for index, content in enumerate((source, renamed)):
        path = tmp_path / f'{index}.tlsf'
        path.write_text(content)
        rows.append(dict(tlsf_file=path.name,
                         source_sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    module().add_structural_digests(rows, tmp_path)
    assert rows[0]['source_structural_sha256'] == rows[1]['source_structural_sha256']
