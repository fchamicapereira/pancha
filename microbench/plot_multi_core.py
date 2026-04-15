#!/usr/bin/env python3

import re
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

from plot_style import _NF_COLORS

SCRIPT_DIR = Path(__file__).parent.absolute()
OUT_DIR = SCRIPT_DIR / "plots"
DATA_DIR = SCRIPT_DIR / ".." / "experiments" / "data"

# FIG_FORMAT = "pdf"
FIG_FORMAT = "png"

_CSV_PATTERN = re.compile(
    r"^(.+)-multi-core-((?:nop|fw|telemetry)(?:_batch_(?:greedy|lazy|sorted))?_multicore)(?:-lbatch(\d+))?\.csv$"
)

_TECHNIQUE_LINESTYLES = {
    "baseline": "-",
    "lazy": "--",
    "greedy": ":",
    "sorted": "-.",
}

_TECHNIQUE_MARKERS = {
    "baseline": "o",
    "greedy": "s",
    "lazy": "^",
    "sorted": "D",
}

_TECHNIQUE_ORDER = ["baseline", "greedy", "lazy", "sorted"]


def _parse_nf_name(nf_full: str) -> tuple[str, str]:
    """Parse 'fw_batch_lazy_multicore' -> ('fw', 'lazy'), 'fw_multicore' -> ('fw', 'baseline')."""
    parts = nf_full.split("_")
    if "batch" in parts:
        # parts: [nf_type, 'batch', technique, 'multicore']
        return parts[0], parts[2]
    else:
        # parts: [nf_type, 'multicore']
        return parts[0], "baseline"


def parse_csv_files(data_dir: Path) -> dict:
    """Parse all multi-core CSV files.

    Returns nested dict: data[pcap][nf_full][lbatch][nb_cores] = {"mbps": (avg, std), "mpps": (avg, std)}
    lbatch is None for files without a logical batch size suffix.
    """
    data: dict = {}

    for csv_file in sorted(data_dir.glob("*.csv")):
        m = _CSV_PATTERN.match(csv_file.name)
        if not m:
            continue

        pcap = m.group(1)
        nf_full = m.group(2)
        lbatch = int(m.group(3)) if m.group(3) else None

        lines = csv_file.read_text().splitlines()
        per_cores: dict[int, tuple[list, list]] = defaultdict(lambda: ([], []))
        for line in lines[1:]:  # skip header
            line = line.strip()
            if not line:
                continue
            parts = line.split(",")
            nb_cores = int(parts[1])
            per_cores[nb_cores][0].append(float(parts[2]) / 1e6)  # Mbps
            per_cores[nb_cores][1].append(float(parts[3]) / 1e6)  # Mpps

        entry = data.setdefault(pcap, {}).setdefault(nf_full, {}).setdefault(lbatch, {})
        for nb_cores, (mbps_vals, mpps_vals) in per_cores.items():
            entry[nb_cores] = {
                "mbps": (float(np.mean(mbps_vals)), float(np.std(mbps_vals))),
                "mpps": (float(np.mean(mpps_vals)), float(np.std(mpps_vals))),
            }

    return data


def plot_scaling(data: dict, metric: str = "mpps"):
    """One line plot per (pcap, lbatch): throughput vs nb_cores, one line per NF+technique."""
    assert metric in ("mpps", "mbps")
    ylabel = "MultiCore (Mpps)" if metric == "mpps" else "MultiCore (Mbps)"

    for pcap, pcap_data in sorted(data.items()):
        all_lbatches = sorted(
            {lb for nf_data in pcap_data.values() for lb in nf_data},
            key=lambda v: (v is not None, v),
        )
        all_nb_cores = sorted(
            {nc for nf_data in pcap_data.values() for lb_data in nf_data.values() for nc in lb_data}
        )

        for lbatch in all_lbatches:
            _, ax = plt.subplots(figsize=(10, 6))

            nf_fulls_sorted = sorted(
                pcap_data,
                key=lambda n: (_parse_nf_name(n)[0], _TECHNIQUE_ORDER.index(_parse_nf_name(n)[1]) if _parse_nf_name(n)[1] in _TECHNIQUE_ORDER else 99),
            )
            for nf_full in nf_fulls_sorted:
                nf_type, technique = _parse_nf_name(nf_full)
                lbatch_data = pcap_data[nf_full].get(lbatch, {})
                if not lbatch_data:
                    continue

                nb_cores_sorted = sorted(lbatch_data)
                avgs = [lbatch_data[nc][metric][0] for nc in nb_cores_sorted]
                stds = [lbatch_data[nc][metric][1] for nc in nb_cores_sorted]

                ax.errorbar(
                    nb_cores_sorted,
                    avgs,
                    yerr=stds,
                    marker=_TECHNIQUE_MARKERS.get(technique, "o"),
                    capsize=4,
                    label=f"{nf_type} ({technique})",
                    color=_NF_COLORS.get(nf_type, "#888888"),
                    linestyle=_TECHNIQUE_LINESTYLES.get(technique, "-"),
                )

            ax.set_xticks(all_nb_cores)
            ax.set_xlabel("Number of cores")
            ax.set_ylabel(ylabel)
            lbatch_label = "" if lbatch is None else f" — logical batch size {lbatch}"
            ax.set_title(f"{pcap} — multi-core scaling{lbatch_label}")
            ax.legend(loc="upper left")
            ax.grid(True, linestyle="--", alpha=0.5)

            plt.tight_layout()
            lbatch_suffix = "no_lbatch" if lbatch is None else f"lbatch{lbatch}"
            out_file = OUT_DIR / f"{pcap}_multi_core_{lbatch_suffix}.{FIG_FORMAT}"
            print(f"Plotting to {out_file}...")
            plt.savefig(out_file, dpi=300)
            plt.close()


def plot_efficiency(data: dict, metric: str = "mpps"):
    """One line plot per (pcap, lbatch): throughput-per-core vs nb_cores (efficiency/scaling linearity)."""
    assert metric in ("mpps", "mbps")
    ylabel = "MultiCore efficiency (Mpps/core)" if metric == "mpps" else "MultiCore efficiency (Mbps/core)"

    for pcap, pcap_data in sorted(data.items()):
        all_lbatches = sorted(
            {lb for nf_data in pcap_data.values() for lb in nf_data},
            key=lambda v: (v is not None, v),
        )
        all_nb_cores = sorted(
            {nc for nf_data in pcap_data.values() for lb_data in nf_data.values() for nc in lb_data}
        )

        for lbatch in all_lbatches:
            _, ax = plt.subplots(figsize=(10, 6))

            for nf_full in sorted(pcap_data):
                nf_type, technique = _parse_nf_name(nf_full)
                lbatch_data = pcap_data[nf_full].get(lbatch, {})
                if not lbatch_data:
                    continue

                nb_cores_sorted = sorted(lbatch_data)
                avgs = [lbatch_data[nc][metric][0] / nc for nc in nb_cores_sorted]

                ax.plot(
                    nb_cores_sorted,
                    avgs,
                    marker="o",
                    label=f"{nf_type} ({technique})",
                    color=_NF_COLORS.get(nf_type, "#888888"),
                    linestyle=_TECHNIQUE_LINESTYLES.get(technique, "-"),
                )

            ax.set_xticks(all_nb_cores)
            ax.set_xlabel("Number of cores")
            ax.set_ylabel(ylabel)
            lbatch_label = "" if lbatch is None else f" — logical batch size {lbatch}"
            ax.set_title(f"{pcap} — multi-core efficiency{lbatch_label}")
            ax.legend(loc="upper right")
            ax.grid(True, linestyle="--", alpha=0.5)

            plt.tight_layout()
            lbatch_suffix = "no_lbatch" if lbatch is None else f"lbatch{lbatch}"
            out_file = OUT_DIR / f"{pcap}_multi_core_efficiency_{lbatch_suffix}.{FIG_FORMAT}"
            print(f"Plotting to {out_file}...")
            plt.savefig(out_file, dpi=300)
            plt.close()


def plot_all():
    data = parse_csv_files(DATA_DIR)
    plot_scaling(data, metric="mpps")


if __name__ == "__main__":
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    plot_all()
