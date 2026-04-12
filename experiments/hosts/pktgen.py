import re
import itertools
import enum

from paramiko import ssh_exception
from dataclasses import dataclass
from humanize import metric
from pathlib import Path
from typing import Optional, Union

from .remote import RemoteHost
from .dpdk_config import DpdkConfig

MIN_NUM_TX_CORES = 2
PKTGEN_PROMPT = "Pktgen> "


class TrafficDist(enum.Enum):
    UNIFORM = "uniform"
    ZIPF = "zipf"


@dataclass
class ThroughputReport:
    bps: int
    pps: int
    loss: float

    def __str__(self):
        bps_str = metric(self.bps)
        pps_str = metric(self.pps)
        loss_str = f"{self.loss*100:.3f}%"
        return f"Throughput: {bps_str}bps ({pps_str}pps), Loss: {loss_str}"


@dataclass
class BenchData:
    tx_pkts: int
    tx_bytes: int
    rx_pkts: int
    rx_bytes: int
    elapsed_seconds: float
    tput: ThroughputReport


class Pktgen:
    def __init__(
        self,
        hostname: str,
        repo: str,
        rx_pcie_dev: str,
        tx_pcie_dev: str,
        nb_tx_cores: int,
        log_file: Optional[Path] = None,
    ) -> None:
        self.host = RemoteHost(hostname, log_file=log_file)
        self.cmd_dir = Path(repo)
        self.exe = self.cmd_dir / "Release" / "bin" / "pktgen"
        self.rx_pcie_dev = rx_pcie_dev
        self.tx_pcie_dev = tx_pcie_dev
        self.nb_tx_cores = nb_tx_cores

        self.setup_env_script = Path(repo) / "paths.sh"

        self.active = False
        self.ready = False

        if self.nb_tx_cores < MIN_NUM_TX_CORES:
            raise Exception(f"Number of TX cores must be >= {MIN_NUM_TX_CORES} (is {self.nb_tx_cores})")

        self.host.test_connection()

        if not self.host.remote_dir_exists(Path(repo)):
            self.host.crash(f"Repo {repo} not found on remote host {self.host.hostname}")

        self._build()

    def _build(self):
        build_script = self.cmd_dir / "build.sh"

        cmd = self.host.run_command(f"source {self.setup_env_script} && {build_script}")
        cmd.watch()
        code = cmd.recv_exit_status()

        if code != 0:
            self.host.crash(f"Failed to build pktgen.")

        assert self.host.remote_file_exists(self.exe)

    def _run_commands(
        self,
        cmds: Union[str, list[str]],
        wait_from_prompt: bool = True,
    ) -> str:
        assert self.active
        return self.cmd.run_console_commands(
            cmds,
            console_pattern=PKTGEN_PROMPT if wait_from_prompt else None,
        )

    def kill(self) -> None:
        self.host.run_command(f"sudo killall -s SIGKILL {self.exe.name}")
        self.active = False
        self.ready = False

    def launch(
        self,
        pcap: Optional[Path] = None,
        logical_batch_size: Optional[int] = None,
        sync_cores: bool = False,
        nb_flows: Optional[int] = None,
        traffic_dist: Optional[TrafficDist] = None,
        zipf_param: Optional[float] = None,
        pkt_size: Optional[int] = None,
        kvs_mode: bool = False,
        kvs_get_ratio: Optional[float] = None,
        seed: Optional[int] = None,
    ) -> None:
        assert not self.active

        self.kill()

        # This is kind of lazy, and not sure if even correct, but let's
        # grab the last digit from he PCIe ID and use it as the port ID.
        tx_port = int(self.tx_pcie_dev.split(".")[-1])
        rx_port = int(self.rx_pcie_dev.split(".")[-1])

        if zipf_param == 0:
            traffic_dist = TrafficDist.UNIFORM

        pktgen_options_list = [
            f"--tx {tx_port}",
            f"--rx {rx_port}",
            f"--tx-cores {self.nb_tx_cores}",
        ]

        if pcap is not None:
            pktgen_options_list.append(f"--pcap {pcap}")
        if logical_batch_size is not None:
            pktgen_options_list.append(f"--logical-batch-size {logical_batch_size}")
        if sync_cores:
            pktgen_options_list.append(f"--sync-cores")
        if nb_flows is not None:
            pktgen_options_list.append(f"--total-flows {nb_flows}")
        if traffic_dist is not None:
            pktgen_options_list.append(f"--dist {traffic_dist.value}")
        if zipf_param is not None:
            pktgen_options_list.append(f"--zipf-param {zipf_param}")
        if pkt_size is not None:
            pktgen_options_list.append(f"--pkt-size {pkt_size}")
        if kvs_mode:
            pktgen_options_list.append(f"--kvs-mode")
            if kvs_get_ratio is not None:
                pktgen_options_list.append(f"--kvs-get-ratio {kvs_get_ratio}")
        if seed is not None:
            pktgen_options_list.append(f"--seed {seed}")

        pktgen_options = " ".join(pktgen_options_list)

        remote_cmd = f"source {self.setup_env_script} && sudo -E {str(self.exe)} {self.dpdk_config} -- {pktgen_options}"

        self.cmd = self.host.run_command(remote_cmd, pty=True)
        self.active = True
        self.ready = False

        self.remote_cmd = remote_cmd
        self.target_pkt_tx = 0

    def wait_launch(self):
        assert self.active

        if self.ready:
            return

        self.wait_ready()
        self.ready = True

    def wait_ready(self) -> str:
        assert self.active

        # Wait to see if we actually managed to run pktgen successfuly.
        # Typically we fail here if we forgot to bind ports to DPDK,
        # or allocate hugepages.
        if self.cmd.exit_status_ready() and self.cmd.recv_exit_status() != 0:
            self.active = False
            output = self.cmd.watch()
            self.host.log(output)
            raise Exception("Cannot run pktgen")

        return self.cmd.watch(stop_pattern=PKTGEN_PROMPT)

    def start(self) -> None:
        assert self.active

        if not self.ready:
            self.wait_launch()

        self._run_commands("start")

    def set_rate(self, rate_mbps: int) -> None:
        assert rate_mbps > 0
        self._run_commands(f"rate {rate_mbps}")

    def set_churn(self, churn_fpm: int) -> None:
        assert churn_fpm >= 0
        self._run_commands(f"churn {churn_fpm}")

    def stop(self) -> None:
        assert self.active
        self._run_commands("stop")

    def bench(self) -> BenchData:
        assert self.active
        output = self._run_commands("bench")

        lines = output.split("\r\n")
        while lines:
            if "Stable report:" in lines[0]:
                break
            lines = lines[1:]

        tx_line = lines[1]
        rx_line = lines[2]
        elapsed_line = lines[3]

        tx_result = re.search(r"\s+TX\s+(\d+)\s+pkts\s+(\d+) bytes", tx_line)
        rx_result = re.search(r"\s+RX\s+(\d+)\s+pkts\s+(\d+) bytes", rx_line)
        elapsed_result = re.search(r"\s+Elapsed\s+([\d.]+) seconds", elapsed_line)

        assert tx_result
        assert rx_result
        assert elapsed_result

        tx_pkts = int(tx_result.group(1))
        tx_bytes = int(tx_result.group(2))
        rx_pkts = int(rx_result.group(1))
        rx_bytes = int(rx_result.group(2))
        elapsed_seconds = float(elapsed_result.group(1))
        tput_report = ThroughputReport(
            bps=int(rx_bytes * 8 / elapsed_seconds),
            pps=int(rx_pkts / elapsed_seconds),
            loss=1 - rx_pkts / tx_pkts if tx_pkts > 0 else 0,
        )

        return BenchData(
            tx_pkts=tx_pkts,
            tx_bytes=tx_bytes,
            rx_pkts=rx_pkts,
            rx_bytes=rx_bytes,
            elapsed_seconds=elapsed_seconds,
            tput=tput_report,
        )

    def reset_stats(self) -> None:
        assert self.active
        self._run_commands("reset")

    def quit(self) -> None:
        if not self.active:
            return

        self._run_commands("quit", wait_from_prompt=False)

        self.active = False
        self.host.log("Pktgen exited successfuly.")

    def get_stats(self) -> tuple[int, int, int, int]:
        assert self.active
        output = self._run_commands("stats")

        lines = output.split("\r\n")

        while lines:
            if lines[0] == "~~~~~~ Pktgen ~~~~~~":
                break
            lines = lines[1:]

        assert lines

        tx_line = lines[1]
        rx_line = lines[2]

        tx_result = re.search(r"\s+TX:\s+(\d+) pkts (\d+) bytes", tx_line)
        rx_result = re.search(r"\s+RX:\s+(\d+) pkts (\d+) bytes", rx_line)

        assert tx_result
        assert rx_result

        tx_pkts = int(tx_result.group(1))
        tx_bytes = int(tx_result.group(2))
        rx_pkts = int(rx_result.group(1))
        rx_bytes = int(rx_result.group(2))

        return tx_pkts, tx_bytes, rx_pkts, rx_bytes

    def enter_interactive(self) -> None:
        self.cmd.posix_shell()

    @property
    def dpdk_config(self):
        if hasattr(self, "_dpdk_config"):
            return self._dpdk_config

        self.host.validate_pcie_dev(self.rx_pcie_dev)
        self.host.validate_pcie_dev(self.tx_pcie_dev)

        all_cores = self.host.get_all_cpus()

        all_cores = set(self.host.get_pcie_dev_cpus(self.tx_pcie_dev))

        # Needs an extra core for the main thread.
        if len(all_cores) < self.nb_tx_cores + 1:
            raise Exception(f"Cannot find {self.nb_tx_cores + 1} cores")

        tx_cores = list(itertools.islice(all_cores, self.nb_tx_cores + 1))

        dpdk_config = DpdkConfig(
            cores=tx_cores,
            proc_type="auto",
            pci_allow_list=[self.rx_pcie_dev, self.tx_pcie_dev],
        )

        self._dpdk_config = dpdk_config
        return self._dpdk_config

    def __del__(self) -> None:
        try:
            if self.active:
                self.quit()
        except OSError:
            # Not important if we crash here.
            pass
        except ssh_exception.SSHException:
            # Not important if we crash here.
            pass
