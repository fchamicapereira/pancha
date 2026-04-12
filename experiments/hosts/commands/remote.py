import re
import select
import sys
import termios
import time
import tty
import socket
import paramiko
import os

from pathlib import Path
from typing import TextIO, Union, Optional, Callable

from .command import Command


class RemoteCommand(Command):
    def __init__(
        self,
        ssh_client: paramiko.SSHClient,
        command: str,
        dir: Optional[Union[str, Path]] = None,
        source_bashrc: bool = True,
        log_file: Optional[TextIO] = None,
        pty: bool = False,
    ) -> None:
        super().__init__(command, dir, source_bashrc, log_file)
        self.ssh_client = ssh_client

        transport = ssh_client.get_transport()

        if transport is None:
            raise RuntimeError("Failed to get transport from client.")

        session = transport.open_session()

        if pty:
            session.setblocking(0)
            session.get_pty()

        session.exec_command(self.command)
        self.cmd_ = session

    def send(self, data: Union[str, bytes]) -> None:
        if isinstance(data, str):
            data = data.encode("utf-8")
        self.cmd_.send(data)

    def recv(self, size: int) -> str:
        data = self.cmd_.recv(size)
        return data.decode("utf-8")

    def recv_stderr(self, size: int) -> str:
        data = self.cmd_.recv_stderr(size)
        return data.decode("utf-8")

    def exit_status_ready(self) -> bool:
        return self.cmd_.exit_status_ready()

    def flush(
        self,
        keyboard_int: Optional[Callable[[], None]] = None,
    ):
        output = ""

        keep_going = True
        try:
            while keep_going:
                time.sleep(0.1)

                # Consume everything that comes out.
                keep_going = len(output) > 0

                while self.cmd_.recv_ready():
                    data = self.cmd_.recv(512)
                    decoded_data = data.decode("utf-8")
                    output += decoded_data
                    if self.log_file:
                        self.log_file.write(decoded_data)
                        self.log_file.flush()

                while self.cmd_.recv_stderr_ready():
                    data = self.cmd_.recv_stderr(512)
                    decoded_data = data.decode("utf-8")
                    output += decoded_data
                    if self.log_file:
                        self.log_file.write(decoded_data)
                        self.log_file.flush()

        except KeyboardInterrupt:
            if keyboard_int is not None:
                keyboard_int()
            raise

    def watch(
        self,
        stop_condition: Optional[Callable[[], bool]] = None,
        keyboard_int: Optional[Callable[[], None]] = None,
        timeout: Optional[float] = None,
        stop_pattern: Optional[str] = None,
        max_match_length: Optional[int] = None,
    ) -> str:
        if stop_condition is None:
            stop_condition = self.cmd_.exit_status_ready

        assert stop_condition is not None

        if timeout is None:
            deadline = None
        else:
            deadline = time.time() + timeout

        if max_match_length is None:
            max_match_length = 1024

        output = ""

        def continue_running():
            if (deadline is not None) and (time.time() > deadline):
                return False

            if stop_pattern is not None:
                search_len = min(len(output), max_match_length)
                if re.search(stop_pattern, output[-search_len:]):
                    return False

            return not stop_condition()

        keep_going = True
        try:
            while keep_going:
                time.sleep(0.01)

                # We consume the output one more time after it's done.
                # This prevents us from missing the last bytes.
                keep_going = continue_running()

                while self.cmd_.recv_ready():
                    data = self.cmd_.recv(512)
                    decoded_data = data.decode("utf-8")
                    output += decoded_data
                    if self.log_file:
                        self.log_file.write(decoded_data)
                        self.log_file.flush()

                while self.cmd_.recv_stderr_ready():
                    data = self.cmd_.recv_stderr(512)
                    decoded_data = data.decode("utf-8")
                    output += decoded_data
                    if self.log_file:
                        self.log_file.write(decoded_data)
                        self.log_file.flush()

        except KeyboardInterrupt:
            if keyboard_int is not None:
                keyboard_int()
            raise

        return output

    def recv_exit_status(self) -> int:
        return self.cmd_.recv_exit_status()

    def fileno(self) -> int:
        return self.cmd_.fileno()

    def run_console_commands(
        self,
        commands: Union[str, list[str]],
        timeout: Optional[float] = None,
        console_pattern: Optional[str] = None,
    ) -> str:
        if not isinstance(commands, list):
            commands = [commands]

        if console_pattern is not None:
            console_pattern_len = len(console_pattern)
        else:
            console_pattern_len = None

        output = ""
        for cmd in commands:
            self.send(cmd + "\n")
            output += self.watch(
                keyboard_int=lambda: self.send("\x03"),
                timeout=timeout,
                stop_pattern=console_pattern,
                max_match_length=console_pattern_len,
            )

        return output

    def posix_shell(self) -> None:
        oldtty = termios.tcgetattr(sys.stdin)
        stdin_blocking = os.get_blocking(sys.stdin.fileno())
        try:
            tty.setcbreak(sys.stdin.fileno())
            os.set_blocking(sys.stdin.fileno(), False)

            self.send("\n")
            print("\n")

            while True:
                r, _, _ = select.select([sys.stdout, sys.stderr, sys.stdin], [], [])
                if sys.stdout in r:
                    try:
                        data = self.recv(512)
                        if len(data) == 0:
                            break

                        sys.stdout.write(data)
                        sys.stdout.flush()

                    except socket.timeout:
                        pass

                if sys.stderr in r:
                    try:
                        data = self.recv_stderr(512)
                        sys.stderr.write(data)
                        sys.stderr.flush()

                    except socket.timeout:
                        pass

                if sys.stdin in r:
                    x = sys.stdin.read(512)
                    if len(x) == 0:
                        break

                    if x.isprintable() or x in ["\r", "\n", "\t", "\x7f"]:
                        # If backspace, delete the last character
                        if x == "\x7f":
                            sys.stdout.write("\b \b")
                        else:
                            sys.stdout.write(x)
                        sys.stdout.flush()
                        self.send(x)

        finally:
            termios.tcsetattr(sys.stdin, termios.TCSADRAIN, oldtty)
            os.set_blocking(sys.stdin.fileno(), stdin_blocking)

    def __del__(self):
        if hasattr(self, "cmd_"):
            self.cmd_.close()
