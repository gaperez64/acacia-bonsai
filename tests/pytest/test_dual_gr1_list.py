"""The recognition cohort comes from observed structural declines only."""

import csv
import json
from pathlib import Path
import runpy

import pytest


MODULE = runpy.run_path(str(Path(__file__).resolve().parents[2] /
                           'scripts/acacia-dual-gr1-list.py'))


def test_observed_prepare_mp_class_only(tmp_path):
    with (tmp_path / 'arm.tsv').open('w') as stream:
        writer = csv.DictWriter(stream, fieldnames=['instance', 'run_index', 'cap_s'],
                                delimiter='\t')
        writer.writeheader()
        for index in range(3):
            instance = f'input{index}.ltl'
            writer.writerow({'instance': instance, 'run_index': index, 'cap_s': '60'})
            directory = tmp_path / 'phases/arm/60' / f'{instance}-{index}'
            directory.mkdir(parents=True)
            record = {'event': 'decline', 'route': 'trusted_prepare',
                      'stage': 'mp-class', 'reason': 'mp-class'}
            if index == 1:
                record['event'] = 'parent_terminal'
            if index == 2:
                record['route'] = 'direct'
            (directory / 'worker.jsonl').write_text(json.dumps(record) + '\n')
    selected, evidence = MODULE['derive'](tmp_path, 'arm')
    assert selected == ['input0.ltl']
    assert evidence[0]['records'] == [str(tmp_path / 'phases/arm/60/input0.ltl-0/'
                                         'worker.jsonl') + ':1']


def test_corrupt_records_are_not_silent_evidence(tmp_path):
    (tmp_path / 'arm.tsv').write_text('instance\trun_index\tcap_s\ninput.ltl\t0\t60\n')
    directory = tmp_path / 'phases/arm/60/input.ltl-0'
    directory.mkdir(parents=True)
    (directory / 'worker.jsonl').write_text('{"event":')
    with pytest.raises(json.JSONDecodeError):
        MODULE['derive'](tmp_path, 'arm')
