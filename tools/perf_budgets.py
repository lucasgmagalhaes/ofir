#!/usr/bin/env python3
"""Performance budgets (OFIR-75, ADR 0089): compiler memory, cold and warm
self-compile, hello world size and startup, and the generated programs'
runtimes, checked against the ceilings in bench/budgets.json.

Instruction counts, sizes and unit counts are deterministic and gate; wall
times are reported (a few budgets with a wide margin also gate). A metric
whose tool is missing says why instead of passing silently.

    python3 tools/perf_budgets.py --compiler build/stage1/ofir
"""

import argparse
import json
import math
import os
import platform
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "bench"))
import run_bench  # noqa: E402  (input_file, run_once, instructions)

# The budgets were measured here; elsewhere they only report.
REFERENCE = platform.system() == "Linux" and platform.machine() in ("x86_64", "AMD64")


def run(cmd: list, env: dict) -> tuple:
    """Wall milliseconds and peak RSS in KiB; exits when the command fails."""
    with tempfile.TemporaryFile() as errors:
        start = time.perf_counter()
        proc = subprocess.Popen(cmd, env=env, stdout=subprocess.DEVNULL, stderr=errors)
        _, status, usage = os.wait4(proc.pid, 0)
        elapsed = (time.perf_counter() - start) * 1000
        if os.waitstatus_to_exitcode(status) != 0:
            errors.seek(0)
            sys.exit(f"command failed: {' '.join(map(str, cmd))}\n{errors.read().decode()}")
    rss = usage.ru_maxrss if sys.platform != "darwin" else usage.ru_maxrss // 1024
    return elapsed, rss


def objects(home: Path) -> int:
    return sum(1 for _ in home.rglob("*.o"))


def distribution(samples: list) -> dict:
    """min, p50, p90 and max (nearest rank) in milliseconds."""
    s = sorted(samples)
    rank = lambda p: s[max(0, math.ceil(p * len(s)) - 1)]
    return {"min": round(s[0], 2), "p50": round(statistics.median(s), 2),
            "p90": round(rank(0.9), 2), "max": round(s[-1], 2)}


def unavailable(reason: str) -> dict:
    return {"value": None, "unavailable": reason}


def compiler_memory(release: str) -> dict:
    """The largest peak RSS per source byte of check-100k and llvm-100k."""
    if not Path(release).exists():
        return unavailable(f"{release} is not built (make bench)")
    ratios = {}
    for name in ("check-100k", "llvm-100k"):
        lines, profile, args = run_bench.BENCHMARKS[name]
        source = run_bench.input_file(lines, profile)
        _, rss = run_bench.run_once([release, "build", str(source), *args])
        ratios[name] = round(rss * 1024 / source.stat().st_size, 2)
    return {"value": max(ratios.values()), "detail": ratios}


def self_compile(compiler: str, env: dict, work: Path) -> dict:
    """A cold build of compiler/ in an empty cache, then a warm rebuild."""
    home = Path(env["OFIR_HOME"])
    cmd = [compiler, "build", str(ROOT / "compiler" / "main.of"), "-o", str(work / "self")]
    cold, cold_rss = run(cmd, env)
    units = objects(home)
    warm, _ = run(cmd, env)
    return {
        "selfCompileColdMs": {"value": round(cold), "detail": {"units": units,
                                                                "peakRssKib": cold_rss}},
        "selfCompileWarmMs": {"value": round(warm)},
        "warmRebuildUnits": {"value": objects(home) - units},
    }


def hello(compiler: str, env: dict, work: Path, runs: int) -> dict:
    exe = work / "hello"
    run([compiler, "build", str(ROOT / "examples" / "hello.of"), "--release", "-o", str(exe)],
        env)
    results = {}
    if shutil.which("strip"):
        stripped = work / "hello-stripped"
        shutil.copy(exe, stripped)
        subprocess.run(["strip", str(stripped)], check=True)
        results["helloStrippedBytes"] = {"value": stripped.stat().st_size,
                                         "detail": {"unstripped": exe.stat().st_size}}
    else:
        results["helloStrippedBytes"] = unavailable("strip is not installed")
    count = run_bench.instructions([str(exe)])
    results["helloStartupInstructions"] = (
        {"value": count} if count else unavailable("valgrind is not installed"))
    wall = distribution([run([str(exe)], env)[0] for _ in range(max(runs, 20))])
    results["helloStartupP90Ms"] = {"value": wall["p90"], "detail": wall}
    return results


def programs(compiler: str, env: dict, work: Path, runs: int, names: list) -> dict:
    results = {}
    for name in names:
        exe = work / name
        run([compiler, "build", str(ROOT / "bench" / "programs" / f"{name}.of"),
             "--release", "-o", str(exe)], env)
        wall = distribution([run([str(exe)], env)[0] for _ in range(runs)])
        count = run_bench.instructions([str(exe)])
        results[name] = ({"value": count, "detail": {"wallMs": wall}} if count
                         else dict(unavailable("valgrind is not installed"), detail={"wallMs": wall}))
    return results


def judge(result: dict, budget: dict, limit_key: str, require: bool) -> str:
    """ok, over, report, or unavailable; sets result["status"]."""
    limit = budget[limit_key]
    result["max"] = limit
    if result["value"] is None:
        status = "unavailable"
        failed = require and budget["gate"]
    else:
        over = result["value"] > limit
        status = ("over" if over else "ok") if budget["gate"] and REFERENCE else (
            "report (over)" if over else "report")
        failed = status == "over"
    result["status"] = status
    return "fail" if failed else status


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--compiler", default=str(ROOT / "build" / "stage1" / "ofir"))
    parser.add_argument("--release-compiler", default=str(ROOT / "build" / "release" / "ofir"),
                        help="stage 0 release build, for the compiler memory budget")
    parser.add_argument("--budgets", type=Path, default=ROOT / "bench" / "budgets.json")
    parser.add_argument("--runs", type=int, default=5, help="runs per program")
    parser.add_argument("--only", help="comma separated programs (default: all)")
    parser.add_argument("--require", action="store_true",
                        help="an unavailable gated metric fails instead of skipping")
    parser.add_argument("--json", type=Path, help="write the results to this file")
    args = parser.parse_args()

    spec = json.loads(args.budgets.read_text())
    names = args.only.split(",") if args.only else list(spec["programs"])
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        env = dict(os.environ, OFIR_HOME=str(work / "home"))
        metrics = {"compilerRssPerSourceByte": compiler_memory(args.release_compiler)}
        metrics.update(self_compile(args.compiler, env, work))
        metrics.update(hello(args.compiler, env, work, args.runs))
        runtimes = programs(args.compiler, env, work, args.runs, names)

    failures = []
    print(f"{'budget':<26} {'value':>16} {'max':>16}  status")
    for name, result in metrics.items():
        if judge(result, spec["budgets"][name], "max", args.require) == "fail":
            failures.append(name)
        value = "-" if result["value"] is None else f"{result['value']:,}"
        why = f" ({result['unavailable']})" if "unavailable" in result else ""
        print(f"{name:<26} {value:>16} {result['max']:>16,}  {result['status']}{why}")
    print(f"\n{'program':<14} {'instructions':>16} {'max':>16}  status   wall ms min/p50/p90/max")
    for name, result in runtimes.items():
        if judge(result, spec["programs"][name], "maxInstructions", args.require) == "fail":
            failures.append(name)
        value = "-" if result["value"] is None else f"{result['value']:,}"
        w = result["detail"]["wallMs"]
        print(f"{name:<14} {value:>16} {result['max']:>16,}  {result['status']:<8} "
              f"{w['min']}/{w['p50']}/{w['p90']}/{w['max']}")
    if not REFERENCE:
        print("\nnot Linux x86_64: the budgets only report here")
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps({"budgets": metrics, "programs": runtimes}, indent=2))
    if failures:
        print(f"\nover budget: {', '.join(failures)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
