#!/usr/bin/env python3

from pathlib import Path
from rich.console import Console
from itertools import product

from experiment import SingleCore, ExperimentTracker
from hosts.pktgen import SyntheticTrafficConfig, PcapConfig, Pktgen, TrafficDist
from hosts.nf import NF

LOG_DIR = Path("logs")
DATA_DIR = Path("data")


def _pcap_stem(pcap: Path) -> str:
    """Return the pcap filename with all extensions stripped."""
    name = pcap.name
    while Path(name).suffix:
        name = Path(name).stem
    return name


ITERATIONS = 1

TRAFFIC_CONFIGS = [
    SyntheticTrafficConfig(nb_flows=100_000, traffic_dist=TrafficDist.ZIPF, zipf_param=0.99),
    # SyntheticTrafficConfig(nb_flows=1, traffic_dist=TrafficDist.UNIFORM),
    # PcapConfig(pcap_path=Path("/home/fcp/pcaps/imc10/univ2_pt0")),
    # PcapConfig(pcap_path=Path("/home/fcp/pcaps/mawi-202604071400-10M.pcap.zst")),
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
    # 64,
    # 256,
    # 1024,
    4096,
]


def build_experiment(
    nf_name: str,
    traffic_config: SyntheticTrafficConfig | PcapConfig,
    logical_batch_size: int | None,
    pktgen: Pktgen,
    console: Console,
) -> SingleCore:
    if isinstance(traffic_config, PcapConfig):
        exp_name = f"{_pcap_stem(traffic_config.pcap_path)}-single-core-{nf_name}"
    else:
        if traffic_config.traffic_dist == TrafficDist.ZIPF:
            zipf_param_str = str(traffic_config.zipf_param).replace(".", "-")
            exp_name = f"zipf-s{zipf_param_str}-f{traffic_config.nb_flows}-single-core-{nf_name}"
        else:
            exp_name = f"uniform-f{traffic_config.nb_flows}-single-core-{nf_name}"

    if logical_batch_size is not None:
        exp_name += f"-lbatch{logical_batch_size}"

    return SingleCore(
        name=exp_name,
        save_name=DATA_DIR / f"{exp_name}.csv",
        pktgen=pktgen,
        nf=NF(
            name=nf_name,
            hostname="graveler",
            repo="/home/fcp/pancha",
            pcie_devs=["0000:af:00.0", "0000:af:00.1"],
            log_file=LOG_DIR / "nf.log",
        ),
        traffic_config=traffic_config,
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
        log_file=LOG_DIR / "pktgen.log",
    )

    console = Console()
    exp_tracker = ExperimentTracker()

    for nf_name, traffic_config, logical_batch_size in product(NFS, TRAFFIC_CONFIGS, LOGICAL_BATCH_SIZES):
        exp_tracker.add_experiment(
            build_experiment(
                nf_name=nf_name,
                traffic_config=traffic_config,
                logical_batch_size=logical_batch_size,
                pktgen=pktgen,
                console=console,
            )
        )

    exp_tracker.run_experiments()


if __name__ == "__main__":
    main()
