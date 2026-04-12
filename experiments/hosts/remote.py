import paramiko
import os
import time

from pathlib import Path
from typing import Optional

from .host import Host

from .commands.command import Command
from .commands.remote import RemoteCommand
from .commands.local import LocalCommand


class RemoteHost(Host):
    def __init__(
        self,
        hostname: str,
        nb_retries: int = 0,
        retry_interval: int = 1,
        log_file: Optional[Path] = None,
    ) -> None:
        super().__init__(str(log_file) if log_file else None)

        self.hostname = hostname
        self.nb_retries = nb_retries
        self.retry_interval = retry_interval

        self._ssh_client = None

    def run_command(self, *args, **kwargs) -> Command:
        return RemoteCommand(self.ssh_client, *args, **kwargs, log_file=self.log_file)

    def run_command_locally(self, *args, **kwargs) -> Command:
        return LocalCommand(*args, **kwargs, log_file=self.log_file)

    def remote_file_exists(self, remote_path: Path) -> bool:
        return super().remote_file_exists(self.hostname, remote_path)

    def remote_dir_exists(self, remote_path: Path) -> bool:
        return super().remote_dir_exists(self.hostname, remote_path)

    def log(self, msg):
        super().log(msg, self.hostname)

    def crash(self, msg):
        super().crash(msg, self.hostname)

    def is_proc_running(self, proc_name):
        cmd = self.run_command(f"pgrep {proc_name}")
        code = cmd.recv_exit_status()
        return code == 0

    def upload_file(
        self,
        local_path: Path,
        remote_path: Path,
        overwrite: bool = True,
    ) -> None:
        super().upload_file(
            self.hostname,
            local_path,
            remote_path,
            overwrite,
        )

    def get_ssh_host_addr(self) -> str:
        cmd = f"grep {self.hostname} ~/.ssh/config"
        cmd = self.run_command_locally(cmd)
        code = cmd.recv_exit_status()

        if code != 0:
            self.crash(f"Host {self.hostname} not found in local ssh config.")

        cmd = f'ssh -tt -G {self.hostname} | grep "^hostname"'
        cmd = self.run_command_locally(cmd)
        info = cmd.watch()

        info = info.split("\n")
        assert len(info) > 0
        info = info[0]

        info = info.split(" ")
        assert len(info) == 2
        addr = info[1]

        return addr

    def test_connection(self):
        # Test reachability
        host_addr = self.get_ssh_host_addr()
        num_of_icmp_req = 2
        timeout_sec = 5
        cmd = f"ping {host_addr} -c {num_of_icmp_req} -W {timeout_sec}"
        cmd = self.run_command_locally(cmd)
        code = cmd.recv_exit_status()

        if code != 0:
            self.crash(f"Host {self.hostname} is unreachable")

        # Test SSH reachability
        cmd = self.run_command(f"echo Host {self.hostname} is reachable!")
        code = cmd.recv_exit_status()

        if code != 0:
            self.crash(f"Unable to ssh into {self.hostname}")

    @property
    def ssh_client(self):
        if self._ssh_client is not None:
            return self._ssh_client

        # adapted from https://gist.github.com/acdha/6064215
        client = paramiko.SSHClient()
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())

        ssh_config = paramiko.SSHConfig()
        user_config_file = os.path.expanduser("~/.ssh/config")
        if os.path.exists(user_config_file):
            with open(user_config_file) as f:
                ssh_config.parse(f)

        cfg = {
            "hostname": self.hostname,
            "sock": None,
            "pkey": None,
        }

        user_config = ssh_config.lookup(self.hostname)

        for k in ("hostname", "username", "port"):
            if k in user_config:
                cfg[k] = user_config[k]

        if "user" in user_config:
            cfg["username"] = user_config["user"]

        if "proxycommand" in user_config and user_config["proxycommand"]:
            cfg["sock"] = paramiko.ProxyCommand(user_config["proxycommand"])

        if "identityfile" in user_config:
            cfg["pkey"] = paramiko.RSAKey.from_private_key_file(user_config["identityfile"][0])

        trial = 0
        while True:
            trial += 1
            try:
                client.connect(**cfg)
                break
            except KeyboardInterrupt as e:
                raise e
            except Exception as e:
                time.sleep(self.retry_interval)
                if trial > self.nb_retries:
                    raise e

        self._ssh_client = client
        return self._ssh_client

    @ssh_client.deleter
    def ssh_client(self):
        if self._ssh_client is not None:
            self._ssh_client.close()
            del self._ssh_client
        self._ssh_client = None

    def __del__(self):
        del self.ssh_client
