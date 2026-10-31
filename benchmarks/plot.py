#!/usr/bin/env python3
"""Plots the results of pool_benchmark, one PNG per benchmark.

Usage:
  ./build/release/benchmarks/pool_benchmark \\
      --benchmark_out=results.json --benchmark_out_format=json
  python3 benchmarks/plot.py results.json [-o plots/]

Needs matplotlib. With --benchmark_repetitions the median of the
repetitions is plotted.
"""

import argparse
import json
import statistics
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")  # render to files, no display needed
import matplotlib.pyplot as plt
from matplotlib.ticker import ScalarFormatter

TO_NS = {"ns": 1.0, "us": 1e3, "ms": 1e6, "s": 1e9}


def load(path):
    """Returns ({family: [point]}, context).

    A point is a dict of the run's arguments ("nThreads": 4, ...; unnamed
    arguments become "arg0", "arg1", ...) plus "time_ns" and, if the run has
    the counter, "per_task_ns".
    """
    data = json.loads(Path(path).read_text())
    samples = defaultdict(list)
    medians = {}
    for run in data["benchmarks"]:
        name = run.get("run_name", run["name"])
        if run.get("run_type") == "aggregate":
            # Only present with repetitions; used if the single runs were
            # dropped by --benchmark_report_aggregates_only.
            if run.get("aggregate_name") == "median":
                medians[name] = run
            continue
        samples[name].append(run)

    families = defaultdict(list)
    # dict.fromkeys keeps the order of the JSON, so pools come in the order
    # they were registered.
    for name in dict.fromkeys([*samples, *medians]):
        runs = samples.get(name) or [medians[name]]
        family, *parts = name.split("/")
        point = {}
        for i, part in enumerate(p for p in parts if p != "real_time"):
            key, sep, value = part.partition(":")
            if not sep:
                key, value = f"arg{i}", key
            point[key] = int(value) if value.isdigit() else value  # pool:asio
        point["time_ns"] = statistics.median(
            r["real_time"] * TO_NS[r["time_unit"]] for r in runs)
        if "per_task" in runs[0]:
            # The counter is inverted (time per task), so it is in seconds.
            point["per_task_ns"] = statistics.median(
                r["per_task"] * 1e9 for r in runs)
        families[family].append(point)
    return families, data.get("context", {})


def lines(points, x, by):
    """Splits points into lines by the value of `by`, each sorted by `x`."""
    groups = defaultdict(list)
    for p in points:
        groups[p.get(by)].append(p)
    return {k: sorted(v, key=lambda p: p[x]) for k, v in sorted(groups.items())}


def ns_per_unit(families):
    """Cost of one unit of burn() from BM_SimpleCycle, or None."""
    points = families.get("BM_SimpleCycle")
    if not points:
        return None
    return statistics.median(p["time_ns"] / p["arg0"] for p in points)


def mark_cpus(ax, context):
    num_cpus = context.get("num_cpus")
    if num_cpus:
        ax.axvline(num_cpus, color="gray", linestyle=":", linewidth=1)
        ax.text(num_cpus, ax.get_ylim()[1], f" {num_cpus} CPUs",
                color="gray", va="top", fontsize=8)


def plot_speedup(ax, points, by, context):
    """Speedup over the 1-thread run of the same line."""
    max_threads = 1
    for key, line in lines(points, "nThreads", by).items():
        base = line[0]
        if base["nThreads"] != 1:
            continue
        xs = [p["nThreads"] for p in line]
        ys = [base["time_ns"] / p["time_ns"] for p in line]
        ax.plot(xs, ys, marker="o", label=f"{by}={key}" if by else None)
        max_threads = max(max_threads, xs[-1])
    ax.plot([1, max_threads], [1, max_threads], color="gray",
            linestyle="--", linewidth=1, label="ideal")
    ax.set_xlabel("nThreads")
    ax.set_ylabel("speedup over 1 thread")
    mark_cpus(ax, context)


def plot_per_task(ax, points, x, by):
    for key, line in lines(points, x, by).items():
        ax.plot([p[x] for p in line], [p["per_task_ns"] for p in line],
                marker="o", label=f"{by}={key}")
    ax.set_xscale("log")
    ax.set_yscale("log")  # nTasks=1 is several times slower than the rest
    # The range is under two decades, so label every tick with a plain number.
    ax.yaxis.set_major_formatter(ScalarFormatter())
    ax.yaxis.set_minor_formatter(ScalarFormatter())
    ax.grid(True, which="minor", axis="y", alpha=0.3)  # minor ticks are labeled
    ax.set_xlabel(x)
    ax.set_ylabel("wall time per task, ns")


def plot_task_size(ax, points, unit_ns):
    """Time per task vs task size; dashed lines are pure work / nThreads."""
    for key, line in lines(points, "taskSize", "nThreads").items():
        xs = [p["taskSize"] for p in line]
        (drawn,) = ax.plot(xs, [p["per_task_ns"] for p in line],
                           marker="o", label=f"nThreads={key}")
        if unit_ns:
            ideal = [(x, x * unit_ns / key) for x in xs if x > 0]
            ax.plot(*zip(*ideal), color=drawn.get_color(), linestyle="--",
                    linewidth=1)
    if unit_ns:
        # Legend entry only: explains the dashed lines.
        ax.plot([], [], color="gray", linestyle="--", linewidth=1,
                label="work only, no overhead")
    ax.set_xscale("symlog", linthresh=100)  # taskSize 0 stays on the axis
    ax.set_yscale("log")
    # The work-only lines go far below the measured points; cut them off.
    ax.set_ylim(bottom=min(p["per_task_ns"] for p in points) / 4)
    ax.set_xlabel("taskSize, units of burn()")
    ax.set_ylabel("wall time per task, ns")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("results", help="JSON file from --benchmark_out")
    parser.add_argument("-o", "--out", help="output directory "
                        "(default: next to the results file)")
    args = parser.parse_args()

    families, context = load(args.results)
    out = Path(args.out) if args.out else Path(args.results).parent
    out.mkdir(parents=True, exist_ok=True)
    unit_ns = ns_per_unit(families)

    plots = {
        "BM_SimpleCycleInPool": lambda ax, pts:
            plot_speedup(ax, pts, "nTasks", context),
        "BM_StrongScaling": lambda ax, pts:
            plot_speedup(ax, pts, None, context),
        "BM_FixedTaskSize": lambda ax, pts:
            plot_speedup(ax, pts, "nTasks", context),
        "BM_EmptyTasks": lambda ax, pts:
            plot_per_task(ax, pts, "nTasks", "nThreads"),
        "BM_TaskSizeSweep": lambda ax, pts:
            plot_task_size(ax, pts, unit_ns),
    }
    for family, points in sorted(families.items()):
        plot = plots.get(family)
        if plot is None:
            print(f"skipped {family}: no plot for it")
            continue
        # One subplot per pool, side by side with a shared Y axis, so the
        # same line can be compared across pools at a glance.
        pools = list(dict.fromkeys(p.get("pool") for p in points))
        fig, axes = plt.subplots(1, len(pools), figsize=(7 * len(pools), 5),
                                 sharey=True, squeeze=False)
        for ax, pool in zip(axes[0], pools):
            plot(ax, [p for p in points if p.get("pool") == pool])
            ax.set_title(pool or "")
            # Major ticks only: on log axes minor lines (2..9 per decade) are
            # uneven and clutter the plot.
            ax.grid(True, which="major", alpha=0.3)
            if ax.get_legend_handles_labels()[0]:
                ax.legend(fontsize=8)
        fig.suptitle(family)
        fig.tight_layout()
        path = out / f"{family}.png"
        fig.savefig(path, dpi=120)
        plt.close(fig)
        print(f"wrote {path}")


if __name__ == "__main__":
    main()
