#!/usr/bin/env python3

from dataclasses import dataclass

import matplotlib.pyplot as plt
import random
import os
import statistics

SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))
# FIG_FORMAT = "pdf"
FIG_FORMAT = "png"

def plot_preliminary_1flow(exp_name: str):
    out_file = os.path.join(SCRIPT_DIR, f"{exp_name}.{FIG_FORMAT}")
    print(f"Plotting to {out_file}...")

    fig, ax = plt.subplots(figsize=(10, 6))

    nfs = [
        'nop', 'batched\nnop\nstraw', 'batched\nnop\ngreedy',
        'fw', 'batched\nfw\nstraw', 'batched\nfw\ngreedy',
        'telemetry', 'batched\ntelemetry\nstraw', 'batched\ntelemetry\ngreedy'
    ]
    mpps = [
        25, 55, 51,
        4, 41, 10,
        2, 35, 12
    ]

    colors = [
        '#19B2FF', '#19B2FF', '#19B2FF',
        '#FF7F00', '#FF7F00', '#FF7F00',
        '#654CFF', '#654CFF', '#654CFF'
    ]

    bars = ax.bar(nfs, mpps, color=colors)
    ax.bar_label(bars, padding=3)

    ax.set_ylabel('Throughput (Mpps)')
    ax.set_title('Stride processing: 1 flow, 1 core, 0 churn')

    plt.tight_layout()
    plt.savefig(out_file, dpi=300)
    plt.close()

def plot_preliminary_zipfian_internet(exp_name: str):
    out_file = os.path.join(SCRIPT_DIR, f"{exp_name}.{FIG_FORMAT}")
    print(f"Plotting to {out_file}...")

    fig, ax = plt.subplots(figsize=(10, 6))

    nfs = [
        'nop', 'batched\nnop\ngreedy', 'batched\nnop\nsorted',
        'fw', 'batched\nfw\ngreedy', 'batched\nfw\nsorted',
        'telemetry', 'batched\ntelemetry\ngreedy', 'batched\ntelemetry\nsorted',
    ]

    mpps = [
        25, 52, 49,
        2, 2, 2,
        2, 4, 4,
    ]

    colors = [
        '#19B2FF', '#19B2FF', '#19B2FF',
        '#FF7F00', '#FF7F00', '#FF7F00',
        '#654CFF', '#654CFF', '#654CFF'
    ]

    bars = ax.bar(nfs, mpps, color=colors)
    ax.bar_label(bars, padding=3)

    ax.set_ylabel('Throughput (Mpps)')
    ax.set_title('Stride processing: s=1.2, 40k flows, 1 core, 0 churn')

    plt.tight_layout()
    plt.savefig(out_file, dpi=300)
    plt.close()

def plot_fw_zipfian_internet_greedy_stride_sizes(exp_name: str):
    stride_sizes = [
        167019991,
        6935419,
        995842,
        175275,
        32163,
        6167,
        1245,
        252,
        37,
        23,
        0,
        0,
        1,
        1,
        0,
        0,
        3,
        0,
        0,
        1,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
    ]

    out_file = os.path.join(SCRIPT_DIR, f"{exp_name}.{FIG_FORMAT}")
    print(f"Plotting to {out_file}...")

    fig, ax = plt.subplots()

    strides = [ i+1 for i in range(len(stride_sizes)) ]
    rel_stride_sizes = [ 100.0 * s/sum(stride_sizes) for s in stride_sizes ]

    ax.bar(strides, rel_stride_sizes)

    ax.set_xticks([ x for x in strides if x % 2 == 0 ])
    ax.set_ylim(0, 100)

    ax.set_ylabel('Relative Size (%)')
    ax.set_title('FW greedy stride sizes: s=1.2, 40k flows, 1 core, 0 churn')

    plt.tight_layout()
    plt.savefig(out_file, dpi=300)
    plt.close()

def plot_fw_zipfian_internet_sorted_stride_sizes(exp_name: str):
    stride_sizes = [
        109440146,
        10522647,
        4015231,
        2224391,
        1558064,
        1235186,
        972768,
        698027,
        445190,
        247793,
        120394,
        51714,
        19505,
        6440,
        1850,
        510,
        118,
        18,
        8,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
    ]

    out_file = os.path.join(SCRIPT_DIR, f"{exp_name}.{FIG_FORMAT}")
    print(f"Plotting to {out_file}...")

    fig, ax = plt.subplots()

    strides = [ i+1 for i in range(len(stride_sizes)) ]
    rel_stride_sizes = [ 100.0 * s/sum(stride_sizes) for s in stride_sizes ]

    ax.bar(strides, rel_stride_sizes)

    ax.set_xticks([ x for x in strides if x % 2 == 0 ])
    ax.set_ylim(0, 100)

    ax.set_ylabel('Relative Size (%)')
    ax.set_title('FW sorted stride sizes: s=1.2, 40k flows, 1 core, 0 churn')

    plt.tight_layout()
    plt.savefig(out_file, dpi=300)
    plt.close()

if __name__ == "__main__":
    plot_preliminary_1flow("uniform_1_flow")
    plot_preliminary_zipfian_internet("zipfian_internet_40k_flows")
    plot_fw_zipfian_internet_greedy_stride_sizes("fw_greedy_stride_sizes")
    plot_fw_zipfian_internet_sorted_stride_sizes("fw_sorted_stride_sizes")


