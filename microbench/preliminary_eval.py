#!/usr/bin/env python3

from pathlib import Path

import matplotlib.pyplot as plt

SCRIPT_DIR = Path(__file__).parent.absolute()
OUT_DIR = SCRIPT_DIR / "plots"

# FIG_FORMAT = "pdf"
FIG_FORMAT = "png"


def plot_1flow():
    out_file = OUT_DIR / f"tput_uniform_1_flow.{FIG_FORMAT}"
    print(f"Plotting to {out_file}...")

    _, ax = plt.subplots(figsize=(10, 6))

    data = [
        ("nop", 30),
        ("batched\nnop\nstraw", 31),
        ("batched\nnop\ngreedy", 31),
        ("fw", 13),
        ("batched\nfw\nstraw", 31),
        ("batched\nfw\ngreedy", 31),
        ("telemetry", 5),
        ("batched\ntelemetry\nstraw", 31),
        ("batched\ntelemetry\ngreedy", 31),
    ]

    colors = [
        "#19B2FF",
        "#19B2FF",
        "#19B2FF",
        "#FF7F00",
        "#FF7F00",
        "#FF7F00",
        "#654CFF",
        "#654CFF",
        "#654CFF",
    ]

    nfs = [d[0] for d in data]
    mpps = [d[1] for d in data]

    bars = ax.bar(nfs, mpps, color=colors)
    ax.bar_label(bars, padding=3)

    ax.set_ylabel("Throughput (Mpps)")
    ax.set_title("Stride processing: 1 flow, 1 core, 0 churn")

    ax.set_ylim(0, 35)

    plt.tight_layout()
    plt.savefig(out_file, dpi=300)
    plt.close()


def plot_zipfian_s1_5():
    out_file = OUT_DIR / f"tput_zipfian_s1_5_40k_flows.{FIG_FORMAT}"
    print(f"Plotting to {out_file}...")

    _, ax = plt.subplots(figsize=(10, 6))

    data = [
        ("nop", 30),
        ("batched\nnop\ngreedy", 33),
        ("batched\nnop\nsorted", 23),
        ("fw", 12),
        ("batched\nfw\ngreedy", 19),
        ("batched\nfw\nsorted", 17),
        ("telemetry", 4),
        ("batched\ntelemetry\ngreedy", 6),
        ("batched\ntelemetry\nsorted", 8),
    ]

    nfs = [d[0] for d in data]
    mpps = [d[1] for d in data]

    colors = [
        "#19B2FF",
        "#19B2FF",
        "#19B2FF",
        "#FF7F00",
        "#FF7F00",
        "#FF7F00",
        "#654CFF",
        "#654CFF",
        "#654CFF",
    ]

    bars = ax.bar(nfs, mpps, color=colors)
    ax.bar_label(bars, padding=3)

    ax.set_ylabel("Throughput (Mpps)")
    ax.set_title("Stride processing: s=1.5, 40k flows, 1 core, 0 churn")

    ax.set_ylim(0, 35)

    plt.tight_layout()
    plt.savefig(out_file, dpi=300)
    plt.close()


def plot_zipfian_s1_26():
    out_file = OUT_DIR / f"tput_zipfian_s1_26_40k_flows.{FIG_FORMAT}"
    print(f"Plotting to {out_file}...")

    _, ax = plt.subplots(figsize=(10, 6))

    data = [
        ("nop", 30),
        ("batched\nnop\ngreedy", 33),
        ("batched\nnop\nsorted", 23),
        ("fw", 12),
        ("batched\nfw\ngreedy", 17),
        ("batched\nfw\nsorted", 14),
        ("telemetry", 4),
        ("batched\ntelemetry\ngreedy", 5),
        ("batched\ntelemetry\nsorted", 6),
    ]

    nfs = [d[0] for d in data]
    mpps = [d[1] for d in data]

    colors = [
        "#19B2FF",
        "#19B2FF",
        "#19B2FF",
        "#FF7F00",
        "#FF7F00",
        "#FF7F00",
        "#654CFF",
        "#654CFF",
        "#654CFF",
    ]

    bars = ax.bar(nfs, mpps, color=colors)
    ax.bar_label(bars, padding=3)

    ax.set_ylabel("Throughput (Mpps)")
    ax.set_title("Stride processing: s=1.26, 40k flows, 1 core, 0 churn")

    ax.set_ylim(0, 35)

    plt.tight_layout()
    plt.savefig(out_file, dpi=300)
    plt.close()


def plot_imc_univ2():
    out_file = OUT_DIR / f"tput_imc_univ2.{FIG_FORMAT}"
    print(f"Plotting to {out_file}...")

    _, ax = plt.subplots(figsize=(10, 6))

    data = [
        ("nop", 30),
        ("batched\nnop\ngreedy", 32),
        ("batched\nnop\nsorted", 22),
        ("fw", 12),
        ("batched\nfw\ngreedy", 18),
        ("batched\nfw\nsorted", 14),
        ("telemetry", 5),
        ("batched\ntelemetry\ngreedy", 5),
        ("batched\ntelemetry\nsorted", 5),
    ]

    nfs = [d[0] for d in data]
    mpps = [d[1] for d in data]

    colors = [
        "#19B2FF",
        "#19B2FF",
        "#19B2FF",
        "#FF7F00",
        "#FF7F00",
        "#FF7F00",
        "#654CFF",
        "#654CFF",
        "#654CFF",
    ]

    bars = ax.bar(nfs, mpps, color=colors)
    ax.bar_label(bars, padding=3)

    ax.set_ylabel("Throughput (Mpps)")
    ax.set_title("Stride processing: IMC Univ2, 1 core, 0 churn")

    ax.set_ylim(0, 35)

    plt.tight_layout()
    plt.savefig(out_file, dpi=300)
    plt.close()


def plot_orchestrator_zipfian_1_5():
    out_file = OUT_DIR / f"tput_orchestrator_zipfian_s1_5_40k_flows.{FIG_FORMAT}"
    print(f"Plotting to {out_file}...")

    _, ax = plt.subplots(figsize=(10, 6))

    data = [
        ("fw\n1c", 12),
        ("fw\nshared\nnothing\n2c", 15),
        ("fw\ne+m\nstraw\n(1f,39%)", 18),
        ("fw\ne+m\ngreedy\n(8f,75%)", 29),
        ("fw\ne+m\nsorted\n(8f,75%)", 29),
        ("telemetry\n1c", 4),
        ("telemetry\nshared\nnothing\n2c", 6),
        ("telemetry\ne+m\nstraw\n(1f,39%)", 7),
        ("telemetry\ne+m\ngreedy\n(8f,75%)", 11),
        ("telemetry\ne+m\nsorted\n(8f,75%)", 8),
    ]

    nfs = [d[0] for d in data]
    mpps = [d[1] for d in data]

    colors = [
        "#FF7F00",
        "#FF7F00",
        "#FF7F00",
        "#FF7F00",
        "#FF7F00",
        "#654CFF",
        "#654CFF",
        "#654CFF",
        "#654CFF",
        "#654CFF",
    ]

    bars = ax.bar(nfs, mpps, color=colors)
    ax.bar_label(bars, padding=3)

    ax.set_ylabel("Throughput (Mpps)")
    ax.set_title("Stride processing: s=1.5, 40k flows, 1 core, 0 churn")

    ax.set_ylim(0, 35)

    plt.tight_layout()
    plt.savefig(out_file, dpi=300)
    plt.close()


def plot_orchestrator_zipfian_1_26():
    out_file = OUT_DIR / f"tput_orchestrator_zipfian_s1_26_40k_flows.{FIG_FORMAT}"
    print(f"Plotting to {out_file}...")

    _, ax = plt.subplots(figsize=(10, 6))

    data = [
        ("fw\n1c", 12),
        ("fw\nshared\nnothing\n2c", 16),
        ("fw\ne+m\nstraw\n(1f,24%)", 15),
        ("fw\ne+m\ngreedy\n(32f,69%)", 29),
        ("fw\ne+m\nsorted\n(32f,69%)", 26),
        ("telemetry\n1c", 4),
        ("telemetry\nshared\nnothing\n2c", 7),
        ("telemetry\ne+m\nstraw\n(1f,24%)", 5),
        ("telemetry\ne+m\ngreedy\n(32f,69%)", 9),
        ("telemetry\ne+m\nsorted\n(32f,69%)", 9),
    ]

    nfs = [d[0] for d in data]
    mpps = [d[1] for d in data]

    colors = [
        "#FF7F00",
        "#FF7F00",
        "#FF7F00",
        "#FF7F00",
        "#FF7F00",
        "#654CFF",
        "#654CFF",
        "#654CFF",
        "#654CFF",
        "#654CFF",
    ]

    bars = ax.bar(nfs, mpps, color=colors)
    ax.bar_label(bars, padding=3)

    ax.set_ylabel("Throughput (Mpps)")
    ax.set_title("Stride processing: s=1.26, 40k flows, 1 core, 0 churn")

    ax.set_ylim(0, 35)

    plt.tight_layout()
    plt.savefig(out_file, dpi=300)
    plt.close()


def plot_orchestrator_imc_univ2():
    out_file = OUT_DIR / f"tput_orchestrator_imc_univ2.{FIG_FORMAT}"
    print(f"Plotting to {out_file}...")

    _, ax = plt.subplots(figsize=(10, 6))

    data = [
        ("fw\n1c", 12),
        ("fw\nshared\nnothing\n2c", 14),
        ("fw\ne+m\nstraw\n(1f,15%)", 12),
        ("fw\ne+m\ngreedy\n(16f,58%)", 13),
        ("fw\ne+m\nsorted\n(16f,58%)", 13),
        ("telemetry\n1c", 5),
        ("telemetry\nshared\nnothing\n2c", 5),
        ("telemetry\ne+m\nstraw\n(1f,15%)", 5),
        ("telemetry\ne+m\ngreedy\n(8f,45%)", 5),
        ("telemetry\ne+m\nsorted\n(8f,45%)", 5),
    ]

    nfs = [d[0] for d in data]
    mpps = [d[1] for d in data]

    colors = [
        "#FF7F00",
        "#FF7F00",
        "#FF7F00",
        "#FF7F00",
        "#FF7F00",
        "#654CFF",
        "#654CFF",
        "#654CFF",
        "#654CFF",
        "#654CFF",
    ]

    bars = ax.bar(nfs, mpps, color=colors)
    ax.bar_label(bars, padding=3)

    ax.set_ylabel("Throughput (Mpps)")
    ax.set_title("Stride processing: IMC Univ2, 1 core, 0 churn")

    ax.set_ylim(0, 35)

    plt.tight_layout()
    plt.savefig(out_file, dpi=300)
    plt.close()


def plot_strides_sizes(exp_name: str, fname: str, stride_sizes: list[int]):
    out_file = OUT_DIR / f"{fname}.{FIG_FORMAT}"
    print(f"Plotting to {out_file}...")

    _, ax = plt.subplots()

    strides = [i + 1 for i in range(len(stride_sizes))]
    rel = [100.0 * s / sum(stride_sizes) for s in stride_sizes]

    ax.bar(strides, rel)

    # Let's use log scale for the y axis, but we need to set the ticks manually to show the percentages.
    ax.set_yscale("log")
    ax.set_yticks([0.01, 0.1, 1, 10, 100])
    ax.set_yticklabels(["0.01", "0.1", "1", "10", "100"])
    ax.set_ylim(0.005, 100)

    ax.set_xticks([x for x in strides if x % 2 == 0])
    # ax.set_yticks(list(range(0, 101, 10)))

    ax.set_ylabel("Relative Size (%)")
    ax.set_title(exp_name)

    plt.tight_layout()
    plt.savefig(out_file, dpi=300)
    plt.close()


def plot_strides_sizes_zipfian_s1_26_greedy():
    title = "Greedy stride sizes: s=1.26, 40k flows, 1 core, 0 churn"
    fname = "stride_sizes_zipfian_s1_26_unsorted"
    stride_sizes = [
        1359304147,
        75167187,
        13208882,
        2818944,
        620694,
        140240,
        31382,
        6956,
        1601,
        294,
        101,
        45,
        7,
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
        0,
        0,
        0,
        0,
        0,
        0,
    ]

    plot_strides_sizes(title, fname, stride_sizes)


def plot_strides_sizes_zipfian_s1_26_sorted():
    title = "Sorted stride sizes: s=1.26, 40k flows, 1 core, 0 churn"
    fname = "stride_sizes_zipfian_s1_26_sorted"
    stride_sizes = [
        670250008,
        75632309,
        29850925,
        16037228,
        10608595,
        8614899,
        7792855,
        6903790,
        5540669,
        3918150,
        2431624,
        1323803,
        634361,
        267563,
        99664,
        32889,
        9769,
        2519,
        606,
        105,
        33,
        2,
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
    ]
    plot_strides_sizes(title, fname, stride_sizes)


def plot_strides_sizes_zipfian_s1_5_greedy():
    title = "Greedy stride sizes: s=1.5, 40k flows, 1 core, 0 churn"
    fname = "stride_sizes_zipfian_s1_5_unsorted"
    stride_sizes = [
        1378385121,
        155109397,
        45595822,
        15954249,
        5830202,
        2165485,
        803290,
        302050,
        111577,
        41462,
        15505,
        5601,
        2076,
        708,
        241,
        96,
        20,
        9,
        4,
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

    plot_strides_sizes(title, fname, stride_sizes)


def plot_strides_sizes_zipfian_s1_5_sorted():
    title = "Sorted stride sizes: s=1.5, 40k flows, 1 core, 0 churn"
    fname = "stride_sizes_zipfian_s1_5_sorted"
    stride_sizes = [
        469586484,
        78959290,
        35200849,
        20339336,
        12685672,
        7913637,
        5147506,
        4117200,
        4399029,
        5381710,
        6376813,
        6857708,
        6573702,
        5580711,
        4204590,
        2811652,
        1666218,
        874905,
        407260,
        167387,
        60822,
        18809,
        5320,
        1241,
        228,
        33,
        3,
        0,
        0,
        0,
        0,
        0,
    ]
    plot_strides_sizes(title, fname, stride_sizes)


def plot_strides_sizes_imc_univ2_greedy():
    title = "Greedy stride sizes: IMC Univ2 1 core, 0 churn"
    fname = "stride_sizes_imc_univ2_unsorted"
    stride_sizes = [
        57597312,
        30459989,
        15231563,
        15385831,
        9806968,
        8717848,
        6263532,
        19188117,
        4066122,
        5024253,
        3546119,
        8339323,
        2869760,
        3333395,
        2566033,
        3642966,
        2193768,
        2450167,
        1999070,
        6157174,
        1823942,
        2552951,
        1650800,
        12544143,
        1290273,
        1472690,
        1216945,
        2755520,
        1270941,
        1994482,
        1415694,
        40774965,
    ]

    plot_strides_sizes(title, fname, stride_sizes)


def plot_strides_sizes_imc_univ2_sorted():
    title = "Sorted stride sizes: IMC Univ2 1 core, 0 churn"
    fname = "stride_sizes_imc_univ2_sorted"
    stride_sizes = [
        87569535,
        40811380,
        23539985,
        18571963,
        13851406,
        11713891,
        9885699,
        9587154,
        8561487,
        8247712,
        6942025,
        5617238,
        4998550,
        4561349,
        4207084,
        4005277,
        3480217,
        3127361,
        2792760,
        2594397,
        2521734,
        1860692,
        1105224,
        674167,
        355310,
        186334,
        77116,
        48263,
        28957,
        20295,
        19487,
        38996,
    ]
    plot_strides_sizes(title, fname, stride_sizes)


if __name__ == "__main__":
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    # plot_1flow()
    # plot_zipfian_s1_26()
    # plot_zipfian_s1_5()
    # plot_imc_univ2()

    # plot_strides_sizes_zipfian_s1_26_greedy()
    # plot_strides_sizes_zipfian_s1_26_sorted()
    # plot_strides_sizes_zipfian_s1_5_greedy()
    # plot_strides_sizes_zipfian_s1_5_sorted()
    plot_strides_sizes_imc_univ2_greedy()
    # plot_strides_sizes_imc_univ2_sorted()

    # plot_orchestrator_zipfian_1_26()
    # plot_orchestrator_zipfian_1_5()
    # plot_orchestrator_imc_univ2()
