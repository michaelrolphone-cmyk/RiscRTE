#!/usr/bin/env python3
"""Run the complete host command manifest in independent, fully reported shards."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
SHARDS = ('normal', 'asan-1', 'asan-2', 'tools')


def assignments(commands):
    sanitized = 0
    for command in commands:
        if 'SANITIZE=1' in command:
            # Contiguous halves retain reuse within closely related test groups.
            shard = 'asan-1' if sanitized < sum('SANITIZE=1' in c for c in commands) // 2 else 'asan-2'
            sanitized += 1
        elif 'TSAN=1' in command or command.startswith(('python ', 'python3 ', 'NVS_GENERATOR=')):
            shard = 'tools'
        else:
            shard = 'normal'
        yield shard, command


def run(commands, shard, output):
    results = []
    output.parent.mkdir(parents=True, exist_ok=True)
    for selected, command in assignments(commands):
        if shard not in ('all', selected):
            continue
        print(f'::group::{selected}: {command}', flush=True)
        started = time.monotonic()
        result = subprocess.run(['bash', '-eo', 'pipefail', '-c', command], cwd=ROOT)
        elapsed = round(time.monotonic() - started, 3)
        results.append({'shard': selected, 'command': command, 'seconds': elapsed, 'exit_code': result.returncode})
        output.write_text(json.dumps(results, indent=2) + '\n')
        print(f'[{elapsed:.3f}s] exit={result.returncode}: {command}', flush=True)
        print('::endgroup::', flush=True)
        if result.returncode:
            print(f'::error::{command} failed with exit code {result.returncode}', flush=True)
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(os.environ['GITHUB_STEP_SUMMARY'], 'a') as handle:
            handle.write(f'## Host {shard}\n\n{len(results)} commands, '
                         f'{sum(r["exit_code"] != 0 for r in results)} failures, '
                         f'{sum(r["seconds"] for r in results):.1f}s total.\n\n')
            handle.write('| Command | Seconds | Exit |\n|---|---:|---:|\n')
            for row in results:
                handle.write(f'| `{row["command"]}` | {row["seconds"]:.3f} | {row["exit_code"]} |\n')
    return int(any(row['exit_code'] != 0 for row in results))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--shard', choices=(*SHARDS, 'all'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    raise SystemExit(run(json.loads((ROOT / 'test/host_suite.json').read_text()), args.shard, args.output))
