#!/usr/bin/env python3

from pathlib import Path
from rich.console import Console
from itertools import product

from experiment import SingleCore, ExperimentTracker
from hosts.pktgen import Pktgen
from hosts.nf import NF

LOG_DIR = Path("logs")
DATA_DIR = Path("data")
ITERATIONS = 5

PCAPS = [
    Path("/home/fcp/pcaps/imc10/univ2_pt0"),
]

NFS = [
    "nop",
    "nop_batch_lazy",
    "nop_batch_greedy",
    "nop_batch_sorted",
    "fw",
    "fw_batch_lazy",
    "fw_batch_greedy",
    "fw_batch_sorted",
    "telemetry",
    "telemetry_batch_lazy",
    "telemetry_batch_greedy",
    "telemetry_batch_sorted",
]

LOGICAL_BATCH_SIZES = [
    None,
    64,
    256,
    1024,
    4096,
]


def build_experiment(
    nf_name: str,
    pcap: Path,
    logical_batch_size: int | None,
    pktgen: Pktgen,
    console: Console,
) -> SingleCore:
    name = f"single-core-{pcap.stem}-{nf_name}"
    if logical_batch_size is not None:
        name += f"-lbatch{logical_batch_size}"
    return SingleCore(
        name=name,
        save_name=DATA_DIR / f"{name}.csv",
        pktgen=pktgen,
        nf=NF(
            name=nf_name,
            hostname="graveler",
            repo="/home/fcp/pancha",
            pcie_devs=["0000:af:00.0", "0000:af:00.1"],
            nb_cores=1,
            log_file=LOG_DIR / "nf.log",
        ),
        pcap=pcap,
        logical_batch_size=logical_batch_size,
        experiment_log_file=LOG_DIR / "experiment.log",
        iterations=ITERATIONS,
        console=console,
    )


def main():
    pktgen = Pktgen(
        hostname="geodude",
        repo="/home/fcp/pktgen",
        rx_pcie_dev="0000:d8:00.0",
        tx_pcie_dev="0000:d8:00.1",
        nb_tx_cores=4,
        log_file=LOG_DIR / "pktgen.log",
    )

    console = Console()
    exp_tracker = ExperimentTracker()

    for nf_name, pcap, logical_batch_size in product(NFS, PCAPS, LOGICAL_BATCH_SIZES):
        exp_tracker.add_experiment(
            build_experiment(
                nf_name=nf_name,
                pcap=pcap,
                logical_batch_size=logical_batch_size,
                pktgen=pktgen,
                console=console,
            )
        )

    exp_tracker.run_experiments()


if __name__ == "__main__":
    main()
