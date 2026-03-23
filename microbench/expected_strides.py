#!/usr/bin/env python3

from dataclasses import dataclass

import matplotlib.pyplot as plt
import random
import os
import statistics

SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))
# FIG_FORMAT = "pdf"
FIG_FORMAT = "png"

SAMPLE_SIZE = 1_000_000
# SAMPLE_SIZE = 10_000
FLOWS = 80_000

EPSILON = 1e-6
ZIPF_PARAMS = [0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0]
# ZIPF_PARAMS = [0.0, 1.2]

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

def plot_hist_unsorted(flows_per_experiment: dict[float, list[int]], exp_name: str, sort=False):
    out_file = f"{exp_name}.{FIG_FORMAT}"
    print(f"Plotting {len(flows_per_experiment)} histograms to {out_file}...")

    fig, axs = plt.subplots(len(flows_per_experiment), 1, figsize=(8, 3 * len(flows_per_experiment)))

    for ax, (zipf_param, flows) in zip(axs, flows_per_experiment.items()):
        batches = [flows[i:i+32] for i in range(0, len(flows), 32)]
        stride_sizes = { i+1: 0 for i in range(32) }
        
        for batch in batches:
            if sort:
                batch.sort()
            strides = [1]
            for i in range(len(batch) - 1):
                if batch[i] == batch[i+1]:
                    strides[-1] += 1
                else:
                    strides.append(1)
            for stride in strides:
                stride_sizes[stride] += 1
        
        total_strides_count = sum(stride_sizes.values())
        rel_stride_sizes = [ 100.0 * stride_sizes[i] / total_strides_count for i in range(1, 33) ]
        
        ax.bar(stride_sizes.keys(), rel_stride_sizes)
        ax.set_xticks([ x for x in stride_sizes.keys() if x % 2 == 0 ])
        ax.set_ylim(0, 100)

        ax.set_ylabel('Traffic (%)')
        ax.set_xlabel('Stride Size')
        ax.set_title(f's={zipf_param:.1f}')

    plt.tight_layout()
    plt.savefig(out_file, dpi=300)
    plt.close()

if __name__ == "__main__":
    experiments = {s: zipf_distribution(s, 0, FLOWS - 1, SAMPLE_SIZE) for s in ZIPF_PARAMS}
    plot_hist_unsorted(experiments, "stride_sizes_unsorted", sort=False)
    plot_hist_unsorted(experiments, "stride_sizes_sorted", sort=True)
