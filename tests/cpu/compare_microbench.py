#!/usr/bin/env python3
"""Run identical cases in paired AB/BA order and retain all observations.

Uses only the Python standard library. Positive delta means current is slower.
Classification is a conservative screening rule, not a significance test:
at least 3%, consistent direction in every round, greater than three combined
relative MADs, and pooled CV at most 5% for both versions.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import random
import statistics as stats
import subprocess
import time


def read_result(path):
    lines = path.read_text(encoding="utf-8").splitlines()
    rows = list(csv.DictReader(line for line in lines if not line.startswith("#")))
    if len(rows) != 1:
        raise ValueError(f"Expected one case in {path}")
    row = rows[0]
    samples = [float(row[f"sample_{i + 1}_ns"]) for i in range(int(row["samples"]))]
    if not all(math.isfinite(x) and x > 0 for x in samples):
        raise ValueError(f"Invalid samples in {path}")
    if not math.isclose(stats.median(samples), float(row["median_ns"]), abs_tol=2e-9):
        raise ValueError(f"Median mismatch in {path}")
    if not math.isclose(stats.stdev(samples), float(row["stddev_ns"]), abs_tol=2e-9):
        raise ValueError(f"Standard deviation mismatch in {path}")
    return row, samples


def summarize(values):
    median = stats.median(values)
    mean = stats.mean(values)
    deviation = stats.stdev(values)
    return dict(samples=len(values), median_ns=median, mean_ns=mean,
                min_ns=min(values), p95_ns=sorted(values)[math.ceil(.95 * len(values)) - 1],
                max_ns=max(values), stddev_ns=deviation, cv_pct=100 * deviation / mean,
                mad_ns=stats.median(abs(x - median) for x in values))


def write_csv(path, rows):
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def collect(output, jobs, rounds, labels):
    summaries = {label: [] for label in labels}
    comparison, observations = [], []
    for index, (name, block_ops) in enumerate(jobs):
        pooled, per_round, source = {}, {}, {}
        for label in labels:
            values, medians, sources = [], [], []
            for round_index in range(rounds):
                path = output / "runs" / f"{index:04d}-{block_ops}-r{round_index + 1}-{label}.csv"
                row, samples = read_result(path)
                if row["case"] != name:
                    raise ValueError(f"Case mismatch in {path}")
                sources.append(row)
                medians.append(stats.median(samples))
                values.extend(samples)
                for s, value in enumerate(samples):
                    observations.append(dict(case=name, block_ops=block_ops, version=label,
                                             round=round_index + 1, sample=s + 1, ns_per_op=value,
                                             iterations=int(row["iterations"]),
                                             ops_per_block=int(row["ops_per_block"]), source=str(path.relative_to(output))))
            for key in ("ops_per_block", "jit_bytes", "helpers_per_block", "body_ops", "unroll_copies"):
                if len({row[key] for row in sources}) != 1:
                    raise ValueError(f"Generated work changed between rounds: {name} {key}")
            pooled[label] = summarize(values)
            per_round[label] = medians
            source[label] = sources[0]
            summary = dict(case=name, block_ops=block_ops, **pooled[label])
            summary.update({key: int(sources[0][key]) for key in
                            ("ops_per_block", "jit_bytes", "helpers_per_block", "body_ops", "unroll_copies")})
            summary.update(rounds=rounds, iterations_per_run=json.dumps([int(r["iterations"]) for r in sources]),
                           measured_ms=sum(float(r["measured_ms"]) for r in sources))
            summary.update({f"sample_{s + 1}_ns": value for s, value in enumerate(values)})
            summaries[label].append(summary)
        before, after = pooled["baseline"], pooled["current"]
        change = 100 * (after["median_ns"] / before["median_ns"] - 1)
        deltas = [100 * (a / b - 1) for b, a in zip(per_round["baseline"], per_round["current"])]
        noise = 300 * math.hypot(before["mad_ns"] / before["median_ns"], after["mad_ns"] / after["median_ns"])
        threshold = max(3, noise)
        if max(before["cv_pct"], after["cv_pct"]) > 5:
            decision = "noisy"
        elif rounds < 2:
            decision = "needs_repeat"
        elif change < -threshold and all(d < -3 for d in deltas):
            decision = "gain"
        elif change > threshold and all(d > 3 for d in deltas):
            decision = "regression"
        else:
            decision = "no_clear_change"
        comparison.append(dict(case=name, block_ops=block_ops, baseline_ns=before["median_ns"],
                               current_ns=after["median_ns"], delta_pct=change,
                               speedup=before["median_ns"] / after["median_ns"], decision=decision,
                               baseline_cv_pct=before["cv_pct"], current_cv_pct=after["cv_pct"],
                               baseline_mad_ns=before["mad_ns"], current_mad_ns=after["mad_ns"],
                               threshold_pct=threshold, round_deltas_pct=json.dumps(deltas),
                               baseline_jit_bytes=int(source["baseline"]["jit_bytes"]),
                               current_jit_bytes=int(source["current"]["jit_bytes"]),
                               baseline_unroll_copies=int(source["baseline"]["unroll_copies"]),
                               current_unroll_copies=int(source["current"]["unroll_copies"])))
    for label, rows in summaries.items():
        write_csv(output / f"{label}.csv", rows)
    write_csv(output / "samples.csv", observations)
    write_csv(output / "comparison.csv", comparison)
    return comparison


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--current", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--baseline-label", default="baseline")
    parser.add_argument("--current-label", default="current")
    parser.add_argument("--samples", type=int, default=11)
    parser.add_argument("--sample-ms", type=int, default=75)
    parser.add_argument("--warmup-ms", type=int, default=100)
    parser.add_argument("--rounds", type=int, default=2)
    parser.add_argument("--block-ops", type=int, nargs="+", default=[32])
    parser.add_argument("--cpu", type=int)
    parser.add_argument("--filter", default="")
    parser.add_argument("--case-file", type=Path, help="Optional newline-separated exact case names")
    parser.add_argument("--seed", type=int, default=20261005)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if not (3 <= args.samples <= 99 and 1 <= args.sample_ms <= 1000 and args.rounds >= 1
            and 0 <= args.warmup_ms <= 5000 and all(1 <= n <= 64 for n in args.block_ops)):
        parser.error("invalid sample, warmup, round or block settings")
    executables = {label: getattr(args, label).resolve() for label in ("baseline", "current")}
    inventories = {label: subprocess.check_output([str(exe), "--list"], text=True).splitlines()
                   for label, exe in executables.items()}
    if inventories["baseline"] != inventories["current"]:
        raise ValueError("Baseline and current case inventories differ; backport the same harness")
    names = [name for name in inventories["baseline"] if args.filter in name]
    if args.case_file:
        wanted = set(args.case_file.read_text().splitlines())
        if wanted - set(names):
            raise ValueError(f"Unknown/filtered cases: {wanted - set(names)}")
        names = [name for name in names if name in wanted]
    jobs = [(name, n) for n in args.block_ops for name in names]
    if not jobs:
        raise ValueError("No cases selected")
    output = args.output.resolve()
    (output / "runs").mkdir(parents=True, exist_ok=True)
    config = dict(samples=args.samples, sample_ms=args.sample_ms, warmup_ms=args.warmup_ms,
                  rounds=args.rounds, cpu=args.cpu, seed=args.seed, jobs=jobs,
                  labels=dict(baseline=args.baseline_label, current=args.current_label),
                  executables={label: dict(path=str(exe), sha256=hashlib.sha256(exe.read_bytes()).hexdigest())
                               for label, exe in executables.items()})
    manifest = output / "manifest.json"
    canonical = json.dumps(config, indent=2)
    if manifest.exists():
        if not args.resume or manifest.read_text() != canonical:
            raise ValueError("Output already exists; use --resume with exactly the same configuration and binaries")
    else:
        manifest.write_text(canonical, encoding="utf-8")
    rng = random.Random(args.seed)
    started = time.monotonic()
    completed = 0
    for round_index in range(args.rounds):
        order = list(range(len(jobs)))
        rng.shuffle(order)
        for index in order:
            name, block_ops = jobs[index]
            # Counterbalance each pair across rounds, independently of case order.
            labels = ("baseline", "current") if (index + round_index) % 2 == 0 else ("current", "baseline")
            for label in labels:
                path = output / "runs" / f"{index:04d}-{block_ops}-r{round_index + 1}-{label}.csv"
                log = path.with_suffix(".log")
                if path.exists() and log.exists() and "All selected cases validated." in log.read_text():
                    read_result(path)
                    continue
                command = [str(executables[label]), "--case", name, "--block-ops", str(block_ops),
                           "--samples", str(args.samples), "--sample-ms", str(args.sample_ms),
                           "--warmup-ms", str(args.warmup_ms), "--csv", str(path)]
                if args.cpu is not None:
                    command.extend(["--cpu", str(args.cpu)])
                with log.open("w", encoding="utf-8") as stream:
                    subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, check=True, timeout=180)
                read_result(path)
            completed += 1
            if completed % 10 == 0 or completed == len(jobs) * args.rounds:
                print(f"{completed}/{len(jobs) * args.rounds} pairs, {(time.monotonic() - started) / 60:.1f} min: {name}", flush=True)
    comparison = collect(output, jobs, args.rounds, executables)
    counts = {decision: sum(row["decision"] == decision for row in comparison)
              for decision in sorted({row["decision"] for row in comparison})}
    print(json.dumps(counts, indent=2))
    print(f"Results: {output / 'comparison.csv'}", flush=True)


if __name__ == "__main__":
    main()
