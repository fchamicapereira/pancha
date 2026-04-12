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

        ax.bar(strides, rel, color=_TECHNIQUE_COLORS[technique])
        ax.set_yscale("log")
        ax.set_yticks([0.01, 0.1, 1, 10, 100])
        ax.set_yticklabels(["0.01", "0.1", "1", "10", "100"])
        ax.set_ylim(0.005, 100)
        ax.set_xticks([x for x in strides if x % 4 == 0])
        ax.set_xlabel("Stride size")
        ax.set_title(technique)

    axes[0].set_ylabel("Relative frequency (%)")

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


if __name__ == "__main__":
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    plot_stride_sizes_single_core()
