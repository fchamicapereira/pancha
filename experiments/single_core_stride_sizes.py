#!/usr/bin/env python3

from pathlib import Path
from itertools import product
from time import sleep

from rich.console import Console
from rich.progress import Progress

from experiment import Experiment, ExperimentTracker
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


TRAFFIC_CONFIGS = [
    SyntheticTrafficConfig(nb_flows=100_000, traffic_dist=TrafficDist.ZIPF, zipf_param=1.0),
    PcapConfig(pcap_path=Path("/home/fcp/pcaps/imc10/univ2_pt0")),
    PcapConfig(pcap_path=Path("/home/fcp/pcaps/mawi-202604071400-10M.pcap.zst")),
]

NFS = [
    "fw_batch_lazy",
    "fw_batch_greedy",
    "fw_batch_sorted",
]

LOGICAL_BATCH_SIZES = [
    None,
    64,
    256,
    1024,
    4096,
]


class StrideSizesExperiment(Experiment):
    def __init__(
        self,
        name: str,
        save_name: Path,
        pktgen: Pktgen,
        nf: NF,
        traffic_config: SyntheticTrafficConfig | PcapConfig,
        logical_batch_size: int | None,
        experiment_log_file: Path | None = None,
        console: Console = Console(),
    ) -> None:
        super().__init__(name, pktgen, nf, experiment_log_file, iterations=1)
        self.save_name = save_name
        self.traffic_config = traffic_config
        self.logical_batch_size = logical_batch_size
        self.console = console

        self.save_name.parent.mkdir(parents=True, exist_ok=True)
        if not self.save_name.exists():
            with open(self.save_name, "w") as f:
                f.write("# stride_size,count\n")

    def run(self, step_progress: Progress, current_iter: int) -> None:
        task_id = step_progress.add_task(self.name, total=1)

        self.log("Launching pktgen")
        self.pktgen.launch(
            traffic_config=self.traffic_config,
            sync_cores=True,
            logical_batch_size=self.logical_batch_size,
        )

        self.log("Launching NF")
        self.nf.launch()

        self.log("Waiting for pktgen")
        self.pktgen.wait_launch()

        self.log("Waiting for NF")
        self.nf.wait_launch()

        self.log("Running")
        self.pktgen.set_rate(1000)
        self.pktgen.start()
        sleep(10)
        self.pktgen.stop()

        strides = self.nf.collect_strides()

        self.pktgen.quit()
        self.nf.kill()

        with open(self.save_name, "a") as f:
            for stride_size, count in enumerate(strides, start=1):
                f.write(f"{stride_size},{count}\n")

        step_progress.update(task_id, advance=1)
        step_progress.update(task_id, visible=False)


def build_experiment(
    nf_name: str,
    traffic_config: SyntheticTrafficConfig | PcapConfig,
    logical_batch_size: int | None,
    pktgen: Pktgen,
    console: Console,
) -> StrideSizesExperiment:
    if isinstance(traffic_config, PcapConfig):
        name = f"{_pcap_stem(traffic_config.pcap_path)}-single-core-stride-sizes-{nf_name}"
    else:
        if traffic_config.traffic_dist == TrafficDist.ZIPF:
            zipf_param_str = str(traffic_config.zipf_param).replace(".", "-")
            name = f"zipf-s{zipf_param_str}-f{traffic_config.nb_flows}-single-core-stride-sizes-{nf_name}"
        else:
            name = f"uniform-f{traffic_config.nb_flows}-single-core-stride-sizes-{nf_name}"

    if logical_batch_size is not None:
        name += f"-lbatch{logical_batch_size}"
    return StrideSizesExperiment(
        name=name,
        save_name=DATA_DIR / f"{name}.csv",
        pktgen=pktgen,
        nf=NF(
            name=nf_name,
            hostname="graveler",
            repo="/home/fcp/pancha",
            pcie_devs=["0000:af:00.0", "0000:af:00.1"],
            log_file=LOG_DIR / "nf.log",
            track_stride_sizes=True,
        ),
        traffic_config=traffic_config,
        logical_batch_size=logical_batch_size,
        experiment_log_file=LOG_DIR / "experiment.log",
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
