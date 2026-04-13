#!/usr/bin/env python3

import re
from pathlib import Path

import matplotlib.pyplot as plt

SCRIPT_DIR = Path(__file__).parent.absolute()
OUT_DIR = SCRIPT_DIR / "plots"
DATA_DIR = SCRIPT_DIR / ".." / "experiments" / "data"

# FIG_FORMAT = "pdf"
FIG_FORMAT = "png"

_STRIDE_SIZES_PATTERN = re.compile(
    r"^single-core-stride-sizes-[^-]+-fw_batch_(greedy|lazy|sorted)-lbatch(\d+)\.csv$"
)
_STRIDE_SIZES_NO_LBATCH_PATTERN = re.compile(
    r"^single-core-stride-sizes-[^-]+-fw_batch_(greedy|lazy|sorted)\.csv$"
)

_TECHNIQUE_COLORS = {
    "lazy": "#19B2FF",
    "greedy": "#FF7F00",
    "sorted": "#654CFF",
}


def _read_stride_sizes_csv(path: Path) -> list[int]:
    """Read a stride-sizes CSV and return counts indexed by stride size (1-based)."""
    counts: dict[int, int] = {}
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        stride_size, count = line.split(",")
        counts[int(stride_size)] = int(count)
    max_stride = max(counts)
    return [counts.get(i, 0) for i in range(1, max_stride + 1)]


def _weighted_average(counts: list[int]) -> float:
    total = sum(counts)
    if total == 0:
        return 0.0
    return sum(stride * c for stride, c in enumerate(counts, start=1)) / total


def _plot_stride_sizes_figure(stride_data_for_lbatch: dict[str, list[int]], title: str, out_file: Path):
    techniques = ["lazy", "greedy", "sorted"]
    fig, axes = plt.subplots(1, 3, figsize=(15, 4), sharey=True)
    fig.suptitle(title)

    for ax, technique in zip(axes, techniques):
        if technique not in stride_data_for_lbatch:
            ax.set_title(technique)
            ax.text(0.5, 0.5, "no data", ha="center", va="center", transform=ax.transAxes)
            continue

        counts = stride_data_for_lbatch[technique]
        total = sum(counts)
        strides = list(range(1, len(counts) + 1))
        rel = [100.0 * c / total for c in counts]

        avg = _weighted_average(counts)
        ax.bar(strides, rel, color=_TECHNIQUE_COLORS[technique])
        ax.set_yscale("log")
        ax.set_yticks([0.01, 0.1, 1, 10, 100])
        ax.set_yticklabels(["0.01", "0.1", "1", "10", "100"])
        ax.set_ylim(0.005, 100)
        ax.set_xticks([x for x in strides if x % 4 == 0])
        ax.set_xlabel("Stride size")
        ax.set_title(technique)
        ax.text(
            0.05, 0.95, f"avg = {avg:.2f}",
            transform=ax.transAxes, ha="left", va="top",
            fontsize=9, bbox=dict(boxstyle="round,pad=0.3", fc="white", alpha=0.7),
        )

    axes[0].set_ylabel("Relative frequency (%)")

    plt.tight_layout()
    print(f"Plotting to {out_file}...")
    plt.savefig(out_file, dpi=300)
    plt.close()


def _plot_stride_sizes_cdf_figure(stride_data_for_lbatch: dict[str, list[int]], title: str, out_file: Path):
    techniques = ["lazy", "greedy", "sorted"]
    fig, ax = plt.subplots(figsize=(7, 4))
    fig.suptitle(title)

    max_stride = 0
    for technique in techniques:
        if technique not in stride_data_for_lbatch:
            continue
        counts = stride_data_for_lbatch[technique]
        total = sum(counts)
        strides = list(range(1, len(counts) + 1))
        cumulative = []
        running = 0
        for c in counts:
            running += c
            cumulative.append(100.0 * running / total)
        avg = _weighted_average(counts)
        max_stride = max(max_stride, len(strides))
        ax.plot(strides, cumulative, color=_TECHNIQUE_COLORS[technique], label=f"{technique} (avg={avg:.2f})", linewidth=1.5)

    ax.set_xlabel("Stride size")
    ax.set_ylabel("Cumulative frequency (%)")
    ax.set_xlim(1, max_stride)
    ax.set_ylim(0, 100)
    if max_stride > 0:
        tick_step = max(1, (max_stride // 8) // 4 * 4)
        ax.set_xticks([x for x in range(tick_step, max_stride + 1, tick_step)])
    ax.legend(loc="lower right")

    plt.tight_layout()
    print(f"Plotting to {out_file}...")
    plt.savefig(out_file, dpi=300)
    plt.close()


def plot_stride_sizes_single_core():
    """5 figures: one without logical batching, then one per logical batch size (64/256/1024/4096).
    Each figure has 3 subplots: lazy, greedy, sorted."""
    lbatch_sizes = [64, 256, 1024, 4096]

    # Load data for files without logical batching
    no_lbatch_data: dict[str, list[int]] = {}
    for csv_file in DATA_DIR.glob("single-core-stride-sizes-*.csv"):
        m = _STRIDE_SIZES_NO_LBATCH_PATTERN.match(csv_file.name)
        if not m:
            continue
        no_lbatch_data[m.group(1)] = _read_stride_sizes_csv(csv_file)

    if no_lbatch_data:
        _plot_stride_sizes_figure(
            no_lbatch_data,
            "Stride sizes — no logical batching",
            OUT_DIR / f"stride_sizes_single_core_no_lbatch.{FIG_FORMAT}",
        )
        _plot_stride_sizes_cdf_figure(
            no_lbatch_data,
            "Stride sizes CDF — no logical batching",
            OUT_DIR / f"stride_sizes_cdf_single_core_no_lbatch.{FIG_FORMAT}",
        )

    # Load data for files with logical batching
    stride_data: dict[int, dict[str, list[int]]] = {}
    for csv_file in DATA_DIR.glob("single-core-stride-sizes-*.csv"):
        m = _STRIDE_SIZES_PATTERN.match(csv_file.name)
        if not m:
            continue
        technique, lbatch = m.group(1), int(m.group(2))
        stride_data.setdefault(lbatch, {})[technique] = _read_stride_sizes_csv(csv_file)

    for lbatch in lbatch_sizes:
        if lbatch not in stride_data:
            print(f"No stride-sizes data for lbatch={lbatch}, skipping.")
            continue

        _plot_stride_sizes_figure(
            stride_data[lbatch],
            f"Stride sizes — logical batch size {lbatch}",
            OUT_DIR / f"stride_sizes_single_core_lbatch{lbatch}.{FIG_FORMAT}",
        )
        _plot_stride_sizes_cdf_figure(
            stride_data[lbatch],
            f"Stride sizes CDF — logical batch size {lbatch}",
            OUT_DIR / f"stride_sizes_cdf_single_core_lbatch{lbatch}.{FIG_FORMAT}",
        )


if __name__ == "__main__":
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    plot_stride_sizes_single_core()
