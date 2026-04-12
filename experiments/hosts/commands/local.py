import os
import re
import select
import subprocess
import sys
import termios
import time
import tty
import socket

from pathlib import Path
from typing import TextIO, Union, Optional, Callable

from .command import Command

class LocalCommand(Command):
    def __init__(
        self,
        command: str,
        dir: Optional[Union[str,Path]] = None,
        source_bashrc: bool = True,
        log_file: Optional[TextIO] = None,
    ) -> None:
        super().__init__(command, dir, source_bashrc, log_file)

        self.proc_ = subprocess.Popen(
            self.command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            shell=True,
            executable="/bin/bash" # For sourcing if needed
        )

        assert self.proc_.stdin
        assert self.proc_.stdout
        assert self.proc_.stderr

        self.stdin = self.proc_.stdin
        self.stdout = self.proc_.stdout
        self.stderr = self.proc_.stderr


    def send(self, data: Union[str, bytes]) -> None:
        if isinstance(data, str):
            data = data.encode("utf-8")
        self.stdin.write(data)
        self.stdin.flush()

    def recv(self, size: int) -> str:
        original_proc_blocking = os.get_blocking(self.stdout.fileno())

        try:
            os.set_blocking(self.stdout.fileno(), False)
            data = self.stdout.read(size)
        finally:
            os.set_blocking(self.stdout.fileno(), original_proc_blocking)

        if data is None:
            return ""

        return data.decode("utf-8")

    def recv_stderr(self, size: int) -> str:
        original_proc_blocking = os.get_blocking(self.stderr.fileno())

        try:
            os.set_blocking(self.stderr.fileno(), False)
            data = self.stderr.read(size)
        finally:
            os.set_blocking(self.stderr.fileno(), original_proc_blocking)

        return data.decode("utf-8")

    def exit_status_ready(self) -> bool:
        return self.proc_.poll() is not None
    
    def flush(
        self,
        keyboard_int: Optional[Callable[[], None]] = None,
    ):
        raise NotImplementedError()

    def watch(
        self,
        stop_condition: Optional[Callable[[], bool]] = None,
        keyboard_int: Optional[Callable[[], None]] = None,
        timeout: Optional[float] = None,
        stop_pattern: Optional[str] = None,
        max_match_length: Optional[int] = None,
    ) -> str:
        if stop_condition is None:
            stop_condition = self.exit_status_ready

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

        original_proc_blocking = os.get_blocking(self.stdout.fileno())

        try:
            os.set_blocking(self.stdout.fileno(), False)

            while keep_going:
                s_out = select.select([self.stdout, self.stderr], [], [], 1)
                r, _, _ = s_out

                # We consume the output one more time after it's done.
                # This prevents us from missing the last bytes.
                keep_going = continue_running()

                if self.stdout is not None and self.stdout in r:
                    data = self.stdout.read(512)
                    data = data.decode("utf-8")
                    output += data
                    if self.log_file:
                        self.log_file.write(data)
                        self.log_file.flush()

                if self.stderr is not None and self.stderr in r:
                    data = self.stderr.read(512)
                    data = data.decode("utf-8")
                    output += data
                    if self.log_file:
                        self.log_file.write(data)
                        self.log_file.flush()

        except KeyboardInterrupt:
            if keyboard_int is not None:
                keyboard_int()
            raise

        finally:
            os.set_blocking(self.stdout.fileno(), original_proc_blocking)

        return output

    def recv_exit_status(self) -> int:
        self.proc_.communicate()
        return self.proc_.returncode

    def fileno(self) -> int:
        return self.stdout.fileno()

    def run_console_commands(
        self,
        commands: Union[str, list[str]],
        timeout: float = 1.0,
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
                r, _, _ = select.select(
                    [self.stdout, self.stderr, sys.stdin], [], []
                )
                if self.stdout in r:
                    try:
                        data = self.recv(512)
                        if len(data) == 0:
                            break

                        sys.stdout.write(data)
                        sys.stdout.flush()

                    except socket.timeout:
                        pass

                if self.stderr in r:
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
