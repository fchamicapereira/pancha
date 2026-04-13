#!/usr/bin/env python3

import re
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

from plot_style import _NF_COLORS, _NFS, _TECHNIQUE_COLORS, _TECHNIQUES

SCRIPT_DIR = Path(__file__).parent.absolute()
OUT_DIR = SCRIPT_DIR / "plots"
DATA_DIR = SCRIPT_DIR / ".." / "experiments" / "data"

# FIG_FORMAT = "pdf"
FIG_FORMAT = "png"

_CSV_PATTERN = re.compile(r"^(.+)-single-core-(fw|nop|telemetry)(?:_batch_(greedy|lazy|sorted))?(?:-lbatch(\d+))?\.csv$")


def parse_csv_files(data_dir: Path) -> dict:
    """Parse all CSV files in data_dir.

    Returns a nested dict: data[pcap][nf][technique][lbatch] = {"mbps": (avg, std), "mpps": (avg, std)}
    lbatch is None for files without a logical batch size suffix.
    """
    data: dict = {}

    for csv_file in sorted(data_dir.glob("*.csv")):
        m = _CSV_PATTERN.match(csv_file.name)
        if not m:
            continue

        pcap = m.group(1)
        nf = m.group(2)
        technique = m.group(3) if m.group(3) else "baseline"
        lbatch = int(m.group(4)) if m.group(4) else None

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

        data.setdefault(pcap, {}).setdefault(nf, {}).setdefault(technique, {})[lbatch] = {
            "mbps": (float(np.mean(mbps)), float(np.std(mbps))),
            "mpps": (float(np.mean(mpps)), float(np.std(mpps))),
        }

    return data


def plot_nf_lbatch(data: dict, metric: str = "mpps"):
    """One plot per (pcap, NF): grouped barplot of techniques across logical batch sizes."""
    assert metric in ("mpps", "mbps")
    ylabel = "Throughput (Mpps)" if metric == "mpps" else "Throughput (Mbps)"

    for pcap, pcap_data in sorted(data.items()):
        for nf, tech_data in sorted(pcap_data.items()):
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
            ax.tick_params(axis="x", which="both", length=0, bottom=False)
            ax.set_xlabel("Logical batch size")
            ax.set_ylabel(ylabel)
            ax.set_title(f"{pcap} — {nf} — throughput by logical batch size")
            ax.set_ylim(0, 50 if metric == "mpps" else 50_000)
            ax.legend(loc="upper left")

            plt.tight_layout()
            out_file = OUT_DIR / f"{pcap}_single_core_{nf}.{FIG_FORMAT}"
            print(f"Plotting to {out_file}...")
            plt.savefig(out_file, dpi=300)
            plt.close()


def plot_all_nfs_by_technique(data: dict, metric: str = "mpps"):
    """One plot per (pcap, lbatch): grouped barplot of NFs across techniques."""
    assert metric in ("mpps", "mbps")
    ylabel = "Throughput (Mpps)" if metric == "mpps" else "Throughput (Mbps)"

    for pcap, pcap_data in sorted(data.items()):
        all_lbatches = sorted(
            {lb for nf_data in pcap_data.values() for td in nf_data.values() for lb in td},
            key=lambda v: (v is not None, v),
        )
        nfs = [nf for nf in _NFS if nf in pcap_data]

        for lbatch in all_lbatches:
            n_groups = len(_TECHNIQUES)
            n_bars = len(nfs)
            bar_width = 0.8 / n_bars
            x = np.arange(n_groups)

            _, ax = plt.subplots(figsize=(10, 6))

            for j, nf in enumerate(nfs):
                nf_data = pcap_data.get(nf, {})
                for k, technique in enumerate(_TECHNIQUES):
                    td = nf_data.get(technique, {})
                    avg = td[lbatch][metric][0] if lbatch in td else 0.0
                    std = td[lbatch][metric][1] if lbatch in td else 0.0
                    offset = x[k] + (j - (n_bars - 1) / 2) * bar_width
                    bar = ax.bar(
                        offset,
                        avg,
                        width=bar_width,
                        yerr=std,
                        capsize=3,
                        color=_NF_COLORS.get(nf, "#888888"),
                        label=nf if k == 0 else None,
                    )
                    ax.bar_label(bar, fmt="%.1f", padding=2, fontsize=7)

            ax.set_xticks(x)
            ax.set_xticklabels(_TECHNIQUES)
            ax.tick_params(axis="x", which="both", length=0, bottom=False)
            ax.set_xlabel("Technique")
            ax.set_ylabel(ylabel)
            lbatch_label = "" if lbatch is None else f" — logical batch size {lbatch}"
            ax.set_title(f"{pcap} — all NFs{lbatch_label}")
            ax.set_ylim(0, 50 if metric == "mpps" else 50_000)
            ax.legend(loc="upper left")

            plt.tight_layout()
            lbatch_suffix = "no_lbatch" if lbatch is None else f"lbatch{lbatch}"
            out_file = OUT_DIR / f"{pcap}_all_nfs_{lbatch_suffix}.{FIG_FORMAT}"
            print(f"Plotting to {out_file}...")
            plt.savefig(out_file, dpi=300)
            plt.close()


def plot_all_nf_lbatch():
    data = parse_csv_files(DATA_DIR)
    plot_nf_lbatch(data, metric="mpps")
    plot_all_nfs_by_technique(data, metric="mpps")


if __name__ == "__main__":
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    plot_all_nf_lbatch()
