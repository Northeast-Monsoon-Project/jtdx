#!/usr/bin/env python3
"""Paired offline FT8/FT4 PCM benchmark. No radio, sound device, or user data.

The default smoke test checks strong GFSK and noise in both algorithms. Use
--benchmark for a predeclared held-out matrix; it reports, never assumes, gain.
Each frame runs in a fresh process so call history cannot reveal the answer.
"""
import argparse
import collections
import hashlib
import json
import math
import os
import pathlib
import platform
import re
import statistics
import subprocess
import tempfile
import time


def quantile(values, p):
    values = sorted(values)
    return values[min(len(values) - 1, max(0, math.ceil(len(values) * p) - 1))]


def execute(command, cwd, timeout):
    if not hasattr(os, 'wait4'):
        proc = subprocess.run(command, cwd=cwd, capture_output=True, text=True, timeout=timeout)
        return proc, None
    # wait4 returns THIS process's usage, unlike cumulative RUSAGE_CHILDREN.
    # Files avoid a pipe deadlock while polling for timeout without reaping via Popen.
    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        proc = subprocess.Popen(command, cwd=cwd, stdout=out, stderr=err)
        deadline = time.monotonic() + timeout
        while True:
            pid, status, usage = os.wait4(proc.pid, os.WNOHANG)
            if pid:
                proc.returncode = os.waitstatus_to_exitcode(status)
                break
            if time.monotonic() > deadline:
                proc.kill()
                _, status, _ = os.wait4(proc.pid, 0)
                proc.returncode = os.waitstatus_to_exitcode(status)
                raise subprocess.TimeoutExpired(command, timeout)
            time.sleep(0.01)
        out.seek(0); err.seek(0)
        result = subprocess.CompletedProcess(command, proc.returncode, out.read().decode(), err.read().decode())
        return result, usage


def run_one(args, mode, enabled, seed, snr, scenario, threads, cwd):
    command = [str(args.fixture), str(mode), str(enabled), str(seed), str(snr),
               scenario, str(threads), str(args.share), args.messages]
    started = time.perf_counter()
    proc, usage = execute(command, cwd, args.timeout)
    elapsed = time.perf_counter() - started
    if proc.returncode:
        raise RuntimeError(f'{command!r}: exit {proc.returncode}\n{proc.stdout}\n{proc.stderr}')
    truth = set()
    decoded = set()
    decoder_wall = decoder_cpu = None
    for line in proc.stdout.splitlines():
        if line.startswith('@@TRUTH '):
            truth.add(line[8:].strip())
        elif len(line) >= 49 and line[:6].isdigit() and line[21] in '~:':
            decoded.add(line[23:49].strip())
        elif line.startswith('@@TIMING '):
            timing = re.search(r'wall=\s*([0-9.]+) cpu=\s*([0-9.]+)', line)
            if timing:
                decoder_wall, decoder_cpu = map(float, timing.groups())
    if decoder_wall is None or '<DecodeFinished>' not in proc.stdout:
        raise RuntimeError(f'incomplete decoder output: {proc.stdout}\n{proc.stderr}')
    record = dict(mode=mode, modern=enabled, seed=seed, snr_2500=snr, scenario=scenario,
                  requested_threads=threads, message_set=args.messages, truth=sorted(truth), decoded=sorted(decoded),
                  good=sorted(decoded & truth), wrong=sorted(decoded - truth),
                  missed=sorted(truth - decoded), wall_seconds=decoder_wall,
                  cpu_seconds=decoder_cpu, process_wall_seconds=elapsed)
    if usage:
        rss = usage.ru_maxrss / 1024 if platform.system() == 'Darwin' else usage.ru_maxrss
        record.update(process_cpu_seconds=usage.ru_utime + usage.ru_stime, peak_rss_kib=rss)
    return record


def summarize(records):
    groups = collections.defaultdict(list)
    for record in records:
        key = (record['mode'], record['modern'], record['snr_2500'],
               record['scenario'], record['requested_threads'])
        groups[key].append(record)
    results = []
    for (mode, modern, snr, scenario, threads), rows in sorted(groups.items()):
        wall = [r['wall_seconds'] for r in rows]
        cpu = [r['cpu_seconds'] for r in rows]
        rss = [r['peak_rss_kib'] for r in rows if 'peak_rss_kib' in r]
        truth = sum(len(r['truth']) for r in rows)
        result = dict(mode=mode, modern=modern, snr_2500=snr, scenario=scenario,
                      requested_threads=threads, frames=len(rows), messages=truth,
                      good=sum(len(r['good']) for r in rows),
                      wrong=sum(len(r['wrong']) for r in rows),
                      wall_median=statistics.median(wall), wall_p95=quantile(wall, 0.95),
                      wall_max=max(wall), cpu_median=statistics.median(cpu),
                      cpu_p95=quantile(cpu, 0.95), cpu_max=max(cpu),
                      average_used_cores=sum(cpu) / sum(wall),
                      over_period=sum(t >= (15.0 if mode == 8 else 7.5) for t in wall))
        if rss:
            result['peak_rss_kib_max'] = max(rss)
        results.append(result)
    return results


def paired_summary(records):
    pairs = collections.defaultdict(dict)
    for row in records:
        key = (row['mode'], row['scenario'], row['snr_2500'], row['requested_threads'], row['seed'])
        pairs[key][row['modern']] = row
    grouped = collections.defaultdict(list)
    for key, pair in pairs.items():
        old, new = pair[0], pair[1]
        old_good, new_good = set(old['good']), set(new['good'])
        old_wrong, new_wrong = set(old['wrong']), set(new['wrong'])
        grouped[key[:4]].append(dict(added=len(new_good - old_good), lost=len(old_good - new_good),
                                    added_wrong=len(new_wrong - old_wrong), removed_wrong=len(old_wrong - new_wrong)))
    return [dict(mode=key[0], scenario=key[1], snr_2500=key[2], requested_threads=key[3],
                 paired_frames=len(rows), **{name: sum(r[name] for r in rows)
                                            for name in ('added', 'lost', 'added_wrong', 'removed_wrong')})
            for key, rows in sorted(grouped.items())]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('fixture', type=pathlib.Path)
    parser.add_argument('share', type=pathlib.Path, help='directory containing ALLCALL7.TXT')
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--trials', type=int, default=20)
    parser.add_argument('--seed', type=int, default=20261030,
                        help='held-out seed fixed before observing waveform results')
    parser.add_argument('--threads', type=int, nargs='+', default=[1])
    parser.add_argument('--modes', type=int, nargs='+', default=[8, 4])
    parser.add_argument('--messages', choices=['cq', 'mixed', 'portable', 'rr73', 'rr73_legacy'], default='cq',
                        help='mixed includes reports, acknowledgements and free text without CQ AP benefit')
    parser.add_argument('--scenarios', nargs='+', default=['awgn', 'impulse', 'tone', 'crowded', 'noise', 'noise_tone'])
    parser.add_argument('--snr8', type=float, nargs='+', default=[-20., -22., -24.])
    parser.add_argument('--snr4', type=float, nargs='+', default=[-16., -18., -20.])
    parser.add_argument('--timeout', type=float, default=60.)
    parser.add_argument('--output', type=pathlib.Path)
    args = parser.parse_args()
    args.fixture = args.fixture.resolve()
    args.share = args.share.resolve()
    if not args.fixture.exists() or not (args.share / 'ALLCALL7.TXT').exists():
        parser.error('fixture and ALLCALL7.TXT must exist')
    if args.trials < 1 or any(t < 1 or t > 24 for t in args.threads):
        parser.error('invalid trial/thread count')
    if any(m not in (4, 8) for m in args.modes):
        parser.error('modes must be 4 or 8')
    if args.messages == 'rr73_legacy' and args.modes != [8]:
        parser.error('legacy RR73 PCM is an FT8 regression; use --modes 8')
    known = {'awgn', 'impulse', 'tone', 'crowded', 'noise', 'noise_tone'}
    if any(s not in known for s in args.scenarios):
        parser.error('unknown scenario')
    records = []
    with tempfile.TemporaryDirectory(prefix='jtdxf-waveform-') as temp:
        for mode in args.modes:
            snrs = (args.snr8 if mode == 8 else args.snr4) if args.benchmark else [-10. if mode == 8 else -6.]
            scenarios = args.scenarios if args.benchmark else ['awgn', 'crowded', 'noise']
            trials = args.trials if args.benchmark else 1
            for threads in args.threads:
                # FT4 currently executes serially; a second requested-thread
                # count would repeat the same work, not measure scaling.
                if mode == 4 and threads != args.threads[0]:
                    continue
                for scenario in scenarios:
                    for snr in (snrs[:1] if scenario.startswith('noise') else snrs):
                        for trial in range(trials):
                            seed = args.seed + trial * 104729
                            pair = []
                            # Alternate execution order to limit systematic thermal/load bias.
                            for enabled in ((0, 1) if trial % 2 == 0 else (1, 0)):
                                record = run_one(args, mode, enabled, seed, snr, scenario, threads, temp)
                                records.append(record)
                                pair.append(record)
                                if not args.benchmark:
                                    assert not record['wrong'], record
                                    assert not record['missed'], record
                            assert pair[0]['truth'] == pair[1]['truth'], 'paired waveforms differ'
                        print(f'FT{mode} {scenario} SNR={snr:g} threads={threads}: {trials} paired frames', flush=True)
        if not args.benchmark:
            # Development-discovered integration regression, not an independent
            # sensitivity measurement: the third FT4 metric must reach rescue.
            previous_messages = args.messages
            args.messages = 'mixed'
            try:
                for enabled in (0, 1):
                    record = run_one(args, 4, enabled, 20365759, -18., 'awgn', 1, temp)
                    record['regression_case'] = 'ft4_third_metric_rescue'
                    records.append(record)
                    assert not record['wrong'], record
                    if enabled:
                        assert 'W9XYZ K1ABC R-09' in record['good'], record
                args.messages = 'portable'
                for scenario in ('awgn', 'crowded'):
                    for enabled in (0, 1):
                        record = run_one(args, 8, enabled, 3702026, -10., scenario, 4, temp)
                        record['regression_case'] = 'ft8_portable_ap_and_suffixes'
                        records.append(record)
                        assert not record['wrong'] and not record['missed'], record
                # Both wire encodings must retain weak known-QSO reception.
                # The old encoding is generated independently of receiver code.
                for message_set in ('rr73', 'rr73_legacy'):
                    args.messages = message_set
                    for enabled in (0, 1):
                        record = run_one(args, 8, enabled, 3702026, -24., 'awgn', 4, temp)
                        record['regression_case'] = 'ft8_rr73_wire_compatibility'
                        records.append(record)
                        assert not record['wrong'] and not record['missed'], record
                args.messages = 'rr73'
                for scenario in ('noise', 'noise_tone'):
                    for enabled in (0, 1):
                        record = run_one(args, 8, enabled, 3702026, -24., scenario, 4, temp)
                        record['regression_case'] = 'ft8_known_qso_noise'
                        records.append(record)
                        assert not record['decoded'], record
            finally:
                args.messages = previous_messages
    cpu_model = platform.processor()
    if pathlib.Path('/proc/cpuinfo').exists():
        match = re.search(r'^model name\s*:\s*(.*)$', pathlib.Path('/proc/cpuinfo').read_text(), re.MULTILINE)
        if match:
            cpu_model = match.group(1)
    report = dict(metadata=dict(platform=platform.platform(), machine=platform.machine(), cpu=cpu_model,
                                logical_cpus=os.cpu_count(), fixture=str(args.fixture),
                                fixture_sha256=hashlib.sha256(args.fixture.read_bytes()).hexdigest(),
                                seed=args.seed, benchmark=args.benchmark, message_set=args.messages,
                                note='Synthetic PCM; fixed messages, varying frequency/time/noise. No RF validation. '
                                     'CPU time includes all decoder threads; FT4 is serial. '
                                     'No prior-frame history is supplied; portable and RR73 regressions set known QSO calls. '
                                     'Timing includes decoder initialization; RSS is decoder process, not full GUI. '
                                     'Zero observed errors does not establish zero error probability.'),
                  summary=summarize(records), paired=paired_summary(records), frames=records)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(report['summary'], ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
