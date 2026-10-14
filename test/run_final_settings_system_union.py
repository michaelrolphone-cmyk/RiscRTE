#!/usr/bin/env python3
"""Run all nine explicit-SD controller groups against the exact final System union."""
import argparse
import concurrent.futures
import datetime
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
for name in ('x4', 'drivers', 'reference-reader', 'system', 'utilities', 'output'):
    parser.add_argument('--' + name, type=lambda value: pathlib.Path(value).resolve(), required=True)
parser.add_argument('--jobs', type=int, choices=(1, 2), default=2,
                    help='Host qualification only; no target firmware build is started')
parser.add_argument('--sparse-finished-media', action='store_true',
                    help='On Linux, reclaim zero-block allocation in completed synthetic images without changing bytes')
args = parser.parse_args()
if args.output.exists():
    raise SystemExit('Use a new matrix output directory')
head = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
if subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain', '--untracked-files=no'], text=True).strip():
    raise SystemExit('Commit the Runtime/test sources before qualifying the matrix')
args.output.mkdir(parents=True)
groups = ('settings', 'quick', 'home-low-battery', 'home-rules', 'home-preset',
          'utility-battery', 'utility-ble', 'utility-contexts', 'utility-contexts-automatic')
common = ['--x4', str(args.x4), '--drivers', str(args.drivers),
          '--reference-reader', str(args.reference_reader), '--settings-system', str(args.system),
          '--wifi-system', str(args.system), '--system-cohort', 'final-union']
environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:abort_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
if args.sparse_finished_media and not shutil.which('fallocate'):
    raise SystemExit('--sparse-finished-media requires fallocate')


def campaign(group, sanitizer):
    label = group + ('-normal' if sanitizer == 'none' else '-asan-ubsan')
    output = args.output / label
    log = args.output / (label + '.log')
    runner = 'run_native_settings_sd_roundtrip_test.py' if group == 'settings' else 'run_native_extra_settings_sd_roundtrip_test.py'
    command = [sys.executable, str(ROOT / 'test' / runner), *common,
               '--sanitize', sanitizer, '--output', str(output)]
    if group != 'settings':
        command += ['--controller', group, '--utilities', str(args.utilities)]
    started = time.monotonic()
    with log.open('w') as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, env=environment)
    manifest = output / 'source-manifest.json'
    media = []
    if result.returncode == 0 and args.sparse_finished_media:
        for card in sorted(output.glob('*/card.img')):
            before = hashlib.sha256(card.read_bytes()).hexdigest()
            allocated_before = card.stat().st_blocks * 512
            subprocess.run(['fallocate', '--dig-holes', str(card)], check=True)
            after = hashlib.sha256(card.read_bytes()).hexdigest()
            if after != before:
                raise RuntimeError('Synthetic image bytes changed while making sparse: ' + str(card))
            media.append(dict(path=str(card), sha256=after, bytes=card.stat().st_size,
                              allocated_before=allocated_before, allocated_after=card.stat().st_blocks * 512))
    return dict(group=group, sanitizers=sanitizer, return_code=result.returncode,
                elapsed_seconds=round(time.monotonic() - started, 3), command=command,
                log=str(log), log_sha256=hashlib.sha256(log.read_bytes()).hexdigest(),
                manifest=str(manifest) if manifest.is_file() else None,
                manifest_sha256=hashlib.sha256(manifest.read_bytes()).hexdigest() if manifest.is_file() else None,
                sparse_media=media)


records = []
with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
    futures = [executor.submit(campaign, group, sanitizer)
               for group in groups for sanitizer in ('none', 'address,undefined')]
    for future in concurrent.futures.as_completed(futures):
        record = future.result()
        records.append(record)
        print(record['group'], record['sanitizers'], 'PASS' if record['return_code'] == 0 else 'FAIL', flush=True)
        (args.output / 'progress.json').write_text(json.dumps(records, indent=2) + '\n')
unchanged = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip() == head
unchanged = unchanged and not subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain', '--untracked-files=no'], text=True).strip()
passed = unchanged and all(record['return_code'] == 0 and record['manifest'] for record in records)
summary = dict(runtime_commit=head, runtime_sources_unchanged=unchanged,
               matrix_runner_sha256=hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
               finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
               result='passed' if passed else 'failed', builds=len(records), campaigns=2 * len(records),
               attached_automatic_maintenance_cases=0,
               limits='Explicit real synchronization after actual controller saves; automatic lifecycle maintenance is qualified separately. Host sectors, NVS/RTOS and UI/radio/RTC shims. No physical firmware or hardware qualification. LeakSanitizer disabled.',
               records=sorted(records, key=lambda record: (record['group'], record['sanitizers'])))
(args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
raise SystemExit(0 if passed else 1)
