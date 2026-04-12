import itertools
import re

from paramiko import ssh_exception

from pathlib import Path
from typing import Optional

from .remote import RemoteHost
from .dpdk_config import DpdkConfig


class NF:
    def __init__(
        self,
        name: str,
        hostname: str,
        repo: str,
        pcie_devs: list[str],
        nb_cores: int = 1,
        log_file: Optional[Path] = None,
        track_stride_sizes: bool = False,
    ) -> None:
        self.host = RemoteHost(hostname, log_file=log_file)
        self.base_dir = Path(repo) / "nfs"
        self.exe = self.base_dir / "build" / "release" / "bin" / name
        self.pcie_devs = pcie_devs
        self.nb_cores = nb_cores
        self.track_stride_sizes = track_stride_sizes
        self._stride_output: str = ""

        self.setup_env_script = Path(repo) / "paths.sh"

        self.active = False
        self.ready = False

        self.host.test_connection()

        if not self.host.remote_dir_exists(Path(repo)):
            self.host.crash(f"Repo {repo} not found on remote host {self.host.hostname}")

        self._build()

    def _build(self):
        if self.track_stride_sizes:
            build_script = self.base_dir / "build-track-stride-sizes.sh"
        else:
            build_script = self.base_dir / "build.sh"
        cmd = self.host.run_command(f"source {self.setup_env_script} && {build_script}")
        cmd.watch()
        code = cmd.recv_exit_status()
        if code != 0:
            self.host.crash(f"Failed to build NF.")

        assert self.host.remote_file_exists(self.exe)

    def kill(self) -> None:
        self.host.run_command(f"sudo killall {self.exe.name}")
        self.active = False
        self.ready = False

    def collect_strides(self) -> list[int]:
        assert self.track_stride_sizes
        self.host.run_command(f"sudo killall -SIGUSR1 {self.exe.name}")
        output = self.cmd.watch(timeout=5.0)

        counts: dict[int, int] = {}
        for match in re.finditer(r"Stride size (\d+): (\d+) packets", output):
            counts[int(match.group(1))] = int(match.group(2))

        if not counts:
            return []
        return [counts.get(i, 0) for i in range(1, max(counts) + 1)]

    def launch(self) -> None:
        assert not self.active

        self.kill()

        remote_cmd = f"source {self.setup_env_script} && sudo -E {str(self.exe)} {self.dpdk_config}"

        self.cmd = self.host.run_command(remote_cmd, pty=True)
        self.active = True
        self.ready = False

    def wait_launch(self) -> None:
        assert self.active

        if self.ready:
            return

        self._wait_ready()
        self.ready = True

    def _wait_ready(self) -> str:
        assert self.active

        # Wait to see if we actually managed to run pktgen successfuly.
        # Typically we fail here if we forgot to bind ports to DPDK, or allocate hugepages.
        if self.cmd.exit_status_ready():
            self.active = False
            output = self.cmd.watch()
            self.host.log(output)
            self.host.crash("Failed to run NF.")

        output = self.cmd.watch(timeout=2)

        if self.cmd.exit_status_ready():
            self.active = False
            self.host.log(output)
            self.host.crash("Failed to run NF.")

        return output

    @property
    def dpdk_config(self):
        if hasattr(self, "_dpdk_config"):
            return self._dpdk_config

        for pcie_dev in self.pcie_devs:
            self.host.validate_pcie_dev(pcie_dev)

        all_cores = self.host.get_all_cpus()
        all_cores = set().union(*(self.host.get_pcie_dev_cpus(pcie_dev) for pcie_dev in self.pcie_devs))

        cores = list(itertools.islice(all_cores, self.nb_cores))

        dpdk_config = DpdkConfig(
            cores=cores,
            proc_type="auto",
            pci_allow_list=self.pcie_devs,
        )

        self._dpdk_config = dpdk_config
        return self._dpdk_config

    def __del__(self) -> None:
        try:
            if self.active:
                self.kill()
        except OSError:
            # Not important if we crash here.
            pass
        except ssh_exception.SSHException:
            # Not important if we crash here.
            pass
