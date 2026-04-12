#!/usr/bin/env python3

import re
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

SCRIPT_DIR = Path(__file__).parent.absolute()
OUT_DIR = SCRIPT_DIR / "plots"
DATA_DIR = SCRIPT_DIR / ".." / "experiments" / "data"

# FIG_FORMAT = "pdf"
FIG_FORMAT = "png"

_CSV_PATTERN = re.compile(r"^.+-(fw|nop|telemetry)(?:_batch_(greedy|lazy|sorted))?(?:-lbatch(\d+))?\.csv$")

_TECHNIQUES = ["baseline", "lazy", "greedy", "sorted"]
_TECHNIQUE_COLORS = {
    "baseline": "#555555",
    "lazy": "#19B2FF",
    "greedy": "#FF7F00",
    "sorted": "#654CFF",
}


def parse_csv_files(data_dir: Path) -> dict:
    """Parse all CSV files in data_dir.

    Returns a nested dict: data[nf][technique][lbatch] = {"mbps": (avg, std), "mpps": (avg, std)}
    lbatch is None for files without a logical batch size suffix.
    """
    data: dict = {}

    for csv_file in sorted(data_dir.glob("*.csv")):
        m = _CSV_PATTERN.match(csv_file.name)
        if not m:
            continue

        nf = m.group(1)
        technique = m.group(2) if m.group(2) else "baseline"
        lbatch = int(m.group(3)) if m.group(3) else None

        lines = csv_file.read_text().splitlines()
        bps_vals, pps_vals = [], []
        for line in lines[1:]:  # skip header
            line = line.strip()
            if not line:
                continue
            parts = line.split(",")
            bps_vals.append(float(parts[1]))
            pps_vals.append(float(parts[2]))

        mbps = np.array(bps_vals) / 1e6
        mpps = np.array(pps_vals) / 1e6

        data.setdefault(nf, {}).setdefault(technique, {})[lbatch] = {
            "mbps": (float(np.mean(mbps)), float(np.std(mbps))),
            "mpps": (float(np.mean(mpps)), float(np.std(mpps))),
        }

    return data


def plot_nf_lbatch(data: dict, metric: str = "mpps"):
    """One plot per NF: grouped barplot of techniques across logical batch sizes.

    metric: "mpps" or "mbps"
    """
    assert metric in ("mpps", "mbps")
    ylabel = "Throughput (Mpps)" if metric == "mpps" else "Throughput (Mbps)"

    for nf, tech_data in sorted(data.items()):
        # Collect lbatch sizes present (exclude None — no-lbatch baseline)
        lbatch_sizes = sorted({lb for td in tech_data.values() for lb in td if lb is not None})
        if not lbatch_sizes:
            continue

        techniques = [t for t in _TECHNIQUES if t in tech_data]
        n_groups = len(lbatch_sizes)
        n_bars = len(techniques)
        bar_width = 0.8 / n_bars
        x = np.arange(n_groups)

        _, ax = plt.subplots(figsize=(10, 6))

        for i, technique in enumerate(techniques):
            td = tech_data[technique]
            avgs, stds = [], []
            for lb in lbatch_sizes:
                if lb in td:
                    avgs.append(td[lb][metric][0])
                    stds.append(td[lb][metric][1])
                else:
                    avgs.append(0.0)
                    stds.append(0.0)

            offsets = x + (i - (n_bars - 1) / 2) * bar_width
            bars = ax.bar(
                offsets,
                avgs,
                width=bar_width,
                yerr=stds,
                capsize=3,
                label=technique,
                color=_TECHNIQUE_COLORS[technique],
            )
            ax.bar_label(bars, fmt="%.1f", padding=2, fontsize=7)

        ax.set_xticks(x)
        ax.set_xticklabels([str(lb) for lb in lbatch_sizes])
        ax.set_xlabel("Logical batch size")
        ax.set_ylabel(ylabel)
        ax.set_title(f"{nf} — throughput by logical batch size")
        ax.set_ylim(0, 50 if metric == "mpps" else 50_000)
        ax.legend()

        plt.tight_layout()
        out_file = OUT_DIR / f"single_core_{nf}.{FIG_FORMAT}"
        print(f"Plotting to {out_file}...")
        plt.savefig(out_file, dpi=300)
        plt.close()


def plot_all_nf_lbatch():
    data = parse_csv_files(DATA_DIR)
    plot_nf_lbatch(data, metric="mpps")


if __name__ == "__main__":
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    plot_all_nf_lbatch()
