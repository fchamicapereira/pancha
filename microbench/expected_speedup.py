#!/usr/bin/env python3

from dataclasses import dataclass
from pathlib import Path

import matplotlib.pyplot as plt
import random
import os
import statistics

SCRIPT_DIR = Path(__file__).parent.absolute()
OUT_DIR = SCRIPT_DIR / "plots"

# FIG_FORMAT = "pdf"
FIG_FORMAT = "png"

SAMPLE_SIZE = 1_000_000
# SAMPLE_SIZE = 10_000
FLOWS = 40_000

EPSILON = 1e-6
ZIPF_PARAMS = [0.0, 0.2, 0.4, 0.6, 0.8, 1.0, 1.2]
# ZIPF_PARAMS = [0.0, 1.2]

MAX_MICE_SPEEDUP = 8
MAX_ELEPHANT_SPEEDUP = 32

CONSERVATIVE_MICE_SPEEDUP = 4
CONSERVATIVE_ELEPHANT_SPEEDUP = 1.5

ELEPHANTS_THRESHOLD_MULTIPLIER_ABOVE_MEDIAN = 100


def plot_cdf(flows_per_experiment: dict[float, list[int]], out_file: str):
    print(f"Plotting CDF to {out_file}...")

    fig, ax = plt.subplots(figsize=(6, 6))

    for zipf_param, flows in flows_per_experiment.items():
        all_flows = set(flows)
        flow_counts = {flow: 0 for flow in all_flows}
        for flow in flows:
            flow_counts[flow] += 1
        sorted_flows = sorted(flow_counts.items(), key=lambda item: item[1], reverse=True)
        total_samples = len(flows)
        total_flows = len(all_flows)

        x = []
        y = []

        cummulative_count = 0
        for i, (flow, count) in enumerate(sorted_flows):
            cummulative_count += count
            x.append(100.0 * (i + 1) / total_flows)
            y.append(cummulative_count / total_samples)

        ax.plot(x, y, label=f"Zipf (s={zipf_param:.1f})")

    ax.set_xlabel("Total flows (%)")
    ax.set_ylabel("CDF")
    ax.set_title("CDF of Flow Distributions")
    ax.legend()
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 1)

    plt.tight_layout()
    plt.savefig(out_file, dpi=300)
    plt.close()


def plot_hist(flows_per_experiment: dict[float, list[int]], out_file: str):
    print(f"Plotting {len(flows_per_experiment)} histograms to {out_file}...")

    fig, axs = plt.subplots(len(flows_per_experiment), 1, figsize=(8, 3 * len(flows_per_experiment)))

    for ax, (zipf_param, flows) in zip(axs, flows_per_experiment.items()):
        ax.hist(flows, bins=100, alpha=0.7)
        ax.set_xlabel("Flow ID")
        ax.set_ylabel("Frequency")
        ax.set_title(f"Zipf (s={zipf_param:.1f})")
        ax.set_xlim(0, FLOWS)

    plt.tight_layout()
    plt.savefig(out_file, dpi=300)
    plt.close()


def uniform_distribution(start: int = 0, end: int = FLOWS - 1, sample_size: int = SAMPLE_SIZE) -> list[int]:
    flows = []
    for i in range(sample_size):
        flow = random.randint(start, end)
        flows.append(flow)
        print(f"Generating uniform flows ({int(100 * (i + 1) / sample_size):3}%)", end="\r")
    print()
    return flows


def zipf_distribution(zipf_param: float, start: int = 0, end: int = FLOWS - 1, sample_size: int = SAMPLE_SIZE) -> list[int]:
    if zipf_param == 0 or zipf_param == 1:
        zipf_param += EPSILON

    flows = []
    # N = FLOWS + 1
    N = end - start + 2
    for i in range(sample_size):
        p = random.random()
        x = N / 2.0

        D = p * (12.0 * (N ** (1.0 - zipf_param) - 1) / (1.0 - zipf_param) + 6.0 - 6.0 * N ** (-zipf_param) + zipf_param - N ** (-1.0 - zipf_param) * zipf_param)

        while True:
            m = x ** (-2 - zipf_param)
            mx = m * x
            mxx = mx * x
            mxxx = mxx * x

            a = 12.0 * (mxxx - 1) / (1.0 - zipf_param) + 6.0 * (1.0 - mxx) + (zipf_param - (mx * zipf_param)) - D
            b = 12.0 * mxx + 6.0 * (zipf_param * mx) + (m * zipf_param * (zipf_param + 1.0))
            newx = max(1.0, x - a / b)

            if abs(newx - x) <= 0.01:
                flow_id = int(newx - 1)
                assert flow_id < FLOWS and "Invalid index"
                flow_id += start
                flows.append(flow_id)
                break

            x = newx

        print(f"Generating zipfian flows s={zipf_param:.1f} ({int(100 * (i + 1) / sample_size):3}%)", end="\r")
    print()
    return flows


@dataclass
class ElephantsAndMice:
    elephants: set[int]
    mice: set[int]
    elephants_traffic_representation: float


def get_elephants_and_mice(flows: list[int]) -> ElephantsAndMice:
    flow_counts = {}
    for flow in flows:
        if flow not in flow_counts:
            flow_counts[flow] = 0
        flow_counts[flow] += 1

    median_flow_count = statistics.median(flow_counts.values())
    threshold_flow_count = ELEPHANTS_THRESHOLD_MULTIPLIER_ABOVE_MEDIAN * median_flow_count

    elephants = set()
    mice = set()

    for flow, count in flow_counts.items():
        if count >= threshold_flow_count:
            elephants.add(flow)
        else:
            mice.add(flow)

    elephant_traffic_representation = sum(flow_counts[flow] for flow in elephants) / len(flows) if flows else 0

    print(f"Median flow count: {median_flow_count:,.0f}")
    print(f"Total elephants: {len(elephants)}")
    print(f"Elephants represent {elephant_traffic_representation:.2%} of the traffic")

    return ElephantsAndMice(elephants=elephants, mice=mice, elephants_traffic_representation=elephant_traffic_representation)


def plot_expected_speedups(experiments: dict[float, ElephantsAndMice], title: str, mice_speedup: float, elephant_speedup: float, out_file: str):
    print("Plotting expected speedups...")

    fig, ax = plt.subplots()

    x_labels = [f"{zipf_param:.1f}" for zipf_param in experiments.keys()]

    mice_speedups = []
    elephant_speedups = []
    for elephants_and_mice in experiments.values():
        elephant_speedup_component = elephants_and_mice.elephants_traffic_representation * elephant_speedup
        mice_speedup_component = (1 - elephants_and_mice.elephants_traffic_representation) * mice_speedup
        mice_speedups.append(mice_speedup_component)
        elephant_speedups.append(elephant_speedup_component)

    speedup_components = {
        "Mice": mice_speedups,
        "Elephants": elephant_speedups,
    }

    cummulative_speedups = [0 for _ in experiments.keys()]
    width = 0.5

    for component, speedups in speedup_components.items():
        ax.bar(x_labels, speedups, width, label=component, bottom=cummulative_speedups)
        cummulative_speedups = [c + s for c, s in zip(cummulative_speedups, speedups)]

    ax.set_xlabel("Zipf param")
    ax.set_ylabel("Speedup")
    ax.set_title(title)
    ax.legend()
    ax.set_ylim(0, max(cummulative_speedups) + 1)

    plt.tight_layout()
    plt.savefig(out_file, dpi=300)
    plt.close()


if __name__ == "__main__":
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    hist_out_file = OUT_DIR / f"uniform_vs_zipfian_hist.{FIG_FORMAT}"
    cdf_out_file = OUT_DIR / f"uniform_vs_zipfian_cdf.{FIG_FORMAT}"

    experiments = {s: zipf_distribution(s, 0, FLOWS - 1, SAMPLE_SIZE) for s in ZIPF_PARAMS}

    plot_hist(experiments, hist_out_file)
    plot_cdf(experiments, cdf_out_file)

    elephants_mice_per_experiment = {zipf_param: get_elephants_and_mice(flows) for zipf_param, flows in experiments.items()}

    plot_expected_speedups(
        elephants_mice_per_experiment,
        f"Optimistic Speedups for Mice (x{MAX_MICE_SPEEDUP}) and Elephants (x{MAX_ELEPHANT_SPEEDUP})",
        MAX_MICE_SPEEDUP,
        MAX_ELEPHANT_SPEEDUP,
        OUT_DIR / f"optimistic_speedups.{FIG_FORMAT}",
    )

    plot_expected_speedups(
        elephants_mice_per_experiment,
        f"Conservative Speedups for Mice (x{CONSERVATIVE_MICE_SPEEDUP}) and Elephants (x{CONSERVATIVE_ELEPHANT_SPEEDUP})",
        CONSERVATIVE_MICE_SPEEDUP,
        CONSERVATIVE_ELEPHANT_SPEEDUP,
        OUT_DIR / f"conservative_speedups.{FIG_FORMAT}",
    )
