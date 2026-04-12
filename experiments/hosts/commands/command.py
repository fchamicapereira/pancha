from abc import ABC, abstractmethod
from pathlib import Path
from typing import TextIO, Union, Optional, Callable

class Command(ABC):
    def __init__(
        self,
        command: str,
        dir: Optional[Union[str,Path]] = None,
        source_bashrc: bool = True,
        log_file: Optional[TextIO] = None,
    ) -> None:
        super().__init__()

        self.command = command
        
        if dir is not None:
            self.command = f"cd {str(dir)}; {self.command}"

        if source_bashrc:
            self.command = f"source $HOME/.bashrc; {self.command}"
        
        self.log_file = log_file

        if self.log_file:
            self.log_file.write(f"command: {self.command}\n")

    @abstractmethod
    def send(self, data: Union[str, bytes]) -> None:
        pass

    @abstractmethod
    def recv(self, size: int) -> str:
        pass

    @abstractmethod
    def recv_stderr(self, size: int) -> str:
        pass

    @abstractmethod
    def exit_status_ready(self) -> bool:
        pass

    @abstractmethod
    def flush(
        self,
        keyboard_int: Optional[Callable[[], None]] = None,
    ):
        pass

    @abstractmethod
    def watch(
        self,
        stop_condition: Optional[Callable[[], bool]] = None,
        keyboard_int: Optional[Callable[[], None]] = None,
        timeout: Optional[float] = None,
        stop_pattern: Optional[str] = None,
        max_match_length: Optional[int] = None,
    ) -> str:
        pass

    @abstractmethod
    def recv_exit_status(self) -> int:
        pass

    @abstractmethod
    def fileno(self) -> int:
        pass

    @abstractmethod
    def run_console_commands(
        self,
        commands: Union[str, list[str]],
        timeout: float = 1.0,
        console_pattern: Optional[str] = None,
    ) -> str:
        pass

    @abstractmethod
    def posix_shell(self) -> None:
        pass