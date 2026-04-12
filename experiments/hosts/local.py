from pathlib import Path

from .commands.command import Command
from .commands.local import LocalCommand

from .host import Host

from typing import Optional


class LocalHost(Host):
    def __init__(
        self,
        log_file: Optional[Path] = None,
    ) -> None:
        super().__init__(str(log_file) if log_file else None)

    def run_command(self, *args, **kwargs) -> Command:
        return LocalCommand(*args, **kwargs)
