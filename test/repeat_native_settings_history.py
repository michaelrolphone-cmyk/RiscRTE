#!/usr/bin/env python3
"""Repeat the bounded first-history probe from a verified maintenance runner build."""
import argparse
import hashlib
import json
import pathlib
import statistics
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=lambda s: pathlib.Path(s).resolve(), required=True)
p.add_argument('--output', type=lambda s: pathlib.Path(s).resolve(), required=True)
p.add_argument('--repetitions', type=int, default=5)
p.add_argument('--case-timeout', type=float, default=30)
a = p.parse_args()
if not 1 <= a.repetitions <= 10 or not 0 < a.case_timeout <= 300:
    raise SystemExit('Use 1..10 repetitions and a timeout in (0,300] seconds')
a.output.mkdir(parents=True, exist_ok=True)
manifest_path = a.build / 'source-manifest.json'
manifest = json.loads(manifest_path.read_text())

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def verify():
    for record in manifest['runtime_sources']:
        if digest(ROOT / record['path']) != record['sha256']:
            raise SystemExit('Runtime/test input changed: ' + record['path'])
    for record in manifest['external_sources']:
        if digest(pathlib.Path(record['root']) / record['path']) != record['sha256']:
            raise SystemExit('External input changed: ' + record['path'])
    for name, expected in manifest['binaries'].items():
        if digest(a.build / name) != expected:
            raise SystemExit('Binary changed: ' + name)

verify()
runs = []
commands = []
for case in ('first-history', 'first-history-single'):
    for i in range(a.repetitions):
        command = [str(a.build / 'test'), str(a.build), case,
                   str(a.build / ('native-' + case + '-repeat-' + str(i)))]
        commands.append(command)
        before = time.perf_counter_ns()
        result = subprocess.run(command, timeout=a.case_timeout, capture_output=True, text=True)
        elapsed = time.perf_counter_ns() - before
        (a.output / (case + '-repeat-' + str(i) + '.log')).write_text(result.stdout + result.stderr)
        result.check_returncode()
        rows = [json.loads(line[len('HISTORY_METRIC '):]) for line in result.stdout.splitlines()
                if line.startswith('HISTORY_METRIC ')]
        expected = [100] if case.endswith('-single') else [1, 10, 100]
        assert [row['retained_generations_per_namespace'] for row in rows] == expected
        runs.append(dict(case=case, host_process_wall_ns=elapsed, checkpoints=rows))
verify()
summary = []
for case, n in (("first-history", 1), ("first-history", 10), ("first-history", 100), ("first-history-single", 100)):
    rows = [next(m for m in run['checkpoints'] if m['retained_generations_per_namespace'] == n)
            for run in runs if run['case'] == case]
    deterministic = {k: rows[0][k] for k in rows[0] if k != 'host_checkpoint_wall_ns'}
    assert all({k: v for k, v in row.items() if k != 'host_checkpoint_wall_ns'} == deterministic
               for row in rows)
    walls = [row['host_checkpoint_wall_ns'] for row in rows]
    summary.append(dict(**deterministic, host_checkpoint_wall_ns_min=min(walls),
                        host_checkpoint_wall_ns_median=statistics.median(walls),
                        host_checkpoint_wall_ns_max=max(walls)))
report = dict(baseline_source_commit='c441eaf6d421e2cf690c7e04ce0f53733612fc90',
              scope='Real Runtime/CpuPort/X4/FatFs and checked-store host fixture. Paired and single-namespace dirty checkpoints; warm existing volume. No product selection or hardware timing.',
              repetitions_per_case=a.repetitions, repetition_case_timeout_seconds=a.case_timeout,
              summary=summary, runs=runs, repeat_commands=commands,
              source_manifest_sha256=digest(manifest_path),
              repeat_script_sha256=digest(pathlib.Path(__file__)),
              binary_sha256=digest(a.build / 'test'))
(a.output / 'normal-results.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(summary, indent=2))
