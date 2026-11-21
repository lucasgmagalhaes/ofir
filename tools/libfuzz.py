#!/usr/bin/env python3
"""libFuzzer on the stage 0 front end (roadmap 6.33i, ADR 0103).

Builds the targets of bootstrap/fuzz/ (`make -C bootstrap fuzz`: the lexer,
the parser and the checker, each with `-fsanitize=fuzzer,address,undefined`),
seeds a corpus with the sources of tests/ and std/ and the regression seeds
of tests/fuzz/frontend/, and runs each target for a while. A crash, a
sanitizer report, a leak or a timeout is a finding: its input is minimized
(`-minimize_crash=1`) and, with `--keep`, written to tests/fuzz/frontend/
as a regression seed that tools/fuzz_frontend.py compiles first.

    python3 tools/libfuzz.py [--targets lex,parse,check] [--seconds 60] [--jobs 1]
                             [--out build/fuzz] [--keep]
"""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SEEDS = ROOT / "tests" / "fuzz" / "frontend"
MAX_LEN = 16384   # a larger file gives no new paths, and slows each run
FINDINGS = ("crash-", "leak-", "timeout-", "oom-")


def build(out):
    subprocess.run(["make", "-s", "-C", str(ROOT / "bootstrap"), "fuzz",
                    f"FUZZ_BUILD={out}"], check=True)


def seed_corpus(corpus):
    """Each source of tests/, std/ and the regression seeds, once by content."""
    corpus.mkdir(parents=True, exist_ok=True)
    sources = [SEEDS, ROOT / "tests", ROOT / "std"]
    count = 0
    for top in sources:
        for path in sorted(top.rglob("*.of")):
            data = path.read_bytes()
            if len(data) > MAX_LEN:
                continue
            name = hashlib.sha1(data).hexdigest()
            target = corpus / name
            if not target.exists():
                target.write_bytes(data)
                count += 1
    return count


def run_target(binary, corpus, artifacts, seconds, jobs, env):
    """Runs one target; returns the finding files it wrote."""
    before = set(artifacts.iterdir())
    args = [str(binary), str(corpus), f"-max_total_time={seconds}", "-timeout=10",
            "-rss_limit_mb=4096", f"-max_len={MAX_LEN}", "-print_final_stats=1",
            f"-artifact_prefix={artifacts}/"]
    if jobs > 1:
        args += [f"-jobs={jobs}", f"-workers={jobs}"]
    log = artifacts / f"{binary.name}.log"
    with open(log, "w") as out:
        subprocess.run(args, stdout=out, stderr=subprocess.STDOUT, env=env, cwd=artifacts)
    return sorted(p for p in set(artifacts.iterdir()) - before if p.name.startswith(FINDINGS))


def minimize(binary, finding, env):
    """The smallest input that still fails, next to the finding."""
    small = finding.with_name(finding.name + ".min")
    subprocess.run([str(binary), "-minimize_crash=1", "-runs=2000", "-timeout=10",
                    f"-exact_artifact_path={small}", str(finding)],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
    return small if small.exists() else finding


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--targets", default="lex,parse,check")
    parser.add_argument("--seconds", type=int, default=60, help="the time for each target")
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--out", type=Path, default=ROOT / "build" / "fuzz")
    parser.add_argument("--keep", action="store_true",
                        help="write each minimized finding to tests/fuzz/frontend/")
    opts = parser.parse_args()
    out = opts.out.resolve()
    build(out)
    corpus = out / "corpus"
    added = seed_corpus(corpus)
    artifacts = out / "findings"
    artifacts.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, OFIR_STD=str(ROOT / "std"),
               ASAN_OPTIONS="detect_leaks=1:abort_on_error=1",
               UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1")
    print(f"libfuzz: corpus {corpus} ({added} new seeds)")
    failures = 0
    for name in opts.targets.split(","):
        binary = out / name
        findings = run_target(binary, corpus, artifacts, opts.seconds, opts.jobs, env)
        print(f"libfuzz: {name}: {len(findings)} findings (log {artifacts / (name + '.log')})")
        for finding in findings:
            failures += 1
            small = minimize(binary, finding, env)
            print(f"  {finding.name}: minimized to {small.stat().st_size} bytes, {small}")
            if opts.keep:
                seed = SEEDS / f"libfuzz_{name}_{finding.name.split('-')[1][:12]}.of"
                shutil.copyfile(small, seed)
                print(f"  kept as {seed.relative_to(ROOT)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
