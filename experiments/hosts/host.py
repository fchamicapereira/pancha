import sys
import re
import subprocess

from abc import ABC, abstractmethod
from typing import Optional
from pathlib import Path

from .commands.command import Command


class Host(ABC):
    def __init__(
        self,
        log_file: Optional[str] = None,
    ):
        super().__init__()

        if log_file:
            out_file_path = Path(log_file)

            if out_file_path.exists():
                assert out_file_path.is_file()
            else:
                out_file_path.parents[0].mkdir(parents=True, exist_ok=True)

            # Always overwrite. The logs become a mess otherwise.
            self.log_file = open(log_file, "w")
        else:
            self.log_file = None

    @abstractmethod
    def run_command(self, *args, **kwargs) -> Command:
        pass

    def log(self, msg, host: Optional[str] = None):
        if self.log_file:
            if host:
                msg = f"[{host}] {msg}"
            print(msg, file=self.log_file)

    def crash(self, msg, host: Optional[str] = None):
        msg = f"ERROR: {msg}"
        if host:
            msg = f"[{host}] {msg}\n"
        if self.log_file:
            print(msg, file=self.log_file)
        print(msg, file=sys.stderr)
        exit(1)

    def remote_file_exists(
        self,
        host: str,
        remote_path: Path,
    ) -> bool:
        cp = subprocess.run(
            ["ssh", host, "test", "-f", str(remote_path)],
            stdout=sys.stdout,
            stderr=sys.stdout,
        )

        return cp.returncode == 0

    def remote_dir_exists(
        self,
        host: str,
        remote_path: Path,
    ) -> bool:
        cp = subprocess.run(
            ["ssh", host, "test", "-d", str(remote_path)],
            stdout=sys.stdout,
            stderr=sys.stdout,
        )

        return cp.returncode == 0

    def upload_file(
        self,
        host: str,
        local_path: Path,
        remote_path: Path,
        overwrite: bool = True,
    ) -> None:
        if not overwrite and self.remote_file_exists(host, remote_path):
            return

        cp = subprocess.run(
            ["scp", "-r", str(local_path), f"{host}:{str(remote_path)}"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        cp.check_returncode()

    def validate_pcie_dev(self, pcie_dev: str) -> None:
        cmd = "lspci -mm"

        cmd = self.run_command(cmd)
        info = cmd.watch()

        info = info.split("\n")

        for line in info:
            line = line.split(" ")
            device = line[0]

            if device in pcie_dev:
                return

        self.crash(f'Invalid PCIe device "{pcie_dev}"')

    def get_device_numa_node(self, pcie_dev: str) -> int:
        cmd = f"lspci -s {pcie_dev} -vv"
        cmd = self.run_command(cmd)
        info = cmd.watch()

        result = re.search(r"NUMA node: (\d+)", info)

        if not result:
            return 0

        assert result
        return int(result.group(1))

    def get_all_cpus(self) -> list[int]:
        cmd = "lscpu"
        cmd = self.run_command(cmd)
        info = cmd.watch()

        result = re.search(r"CPU\(s\):\D+(\d+)", info)

        assert result
        total_cpus = int(result.group(1))

        return [x for x in range(total_cpus)]

    def get_numa_node_cpus(self, node: int):
        cmd = "lscpu"
        cmd = self.run_command(cmd)
        info = cmd.watch()
        info = [line for line in info.split("\n") if "NUMA" in line]

        assert len(info) > 0
        total_nodes_match = re.search(r"\D+(\d+)", info[0])

        assert total_nodes_match
        total_nodes = int(total_nodes_match.group(1))

        if node > total_nodes:
            self.crash(f"Requested NUMA node ({node}) >= available nodes ({total_nodes})")

        if total_nodes == 1:
            return self.get_all_cpus()

        assert len(info) == total_nodes + 1
        node_info = info[node + 1]

        if "-" in node_info:
            cpus_match = re.search(r"\D+(\d+)\-(\d+)$", node_info)
            assert cpus_match

            min_cpu = int(cpus_match.group(1))
            max_cpu = int(cpus_match.group(2))

            return [cpu for cpu in range(min_cpu, max_cpu + 1)]

        cpus_match = re.search(r"\D+([\d,]+)$", node_info)
        assert cpus_match
        return [int(i) for i in str(cpus_match.groups(0)[0]).split(",")]

    def get_pcie_dev_cpus(self, pcie_dev: str) -> list[int]:
        numa = self.get_device_numa_node(pcie_dev)
        cpus = self.get_numa_node_cpus(numa)

        if self.log_file:
            self.log_file.write(f"PCIe={pcie_dev} NUMA={numa} CPUs={cpus}\n")

        return cpus

    def find(self, name: str, path: Path) -> list[Path]:
        cmd = f'find {str(path)} -name "{name}"'

        cmd = self.run_command(cmd)
        info = cmd.watch()
        code = cmd.recv_exit_status()

        if code != 0:
            self.crash(f"find command failed with code {code}")

        options = [Path(p) for p in info.split("\n")]
        return options
