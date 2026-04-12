from time import sleep
from datetime import datetime
from typing import Optional
from pathlib import Path

from rich.console import Console, Group
from rich.live import Live
from rich.progress import (
    BarColumn,
    Progress,
    TextColumn,
    TimeElapsedColumn,
    TimeRemainingColumn,
)

from hosts.pktgen import Pktgen
from hosts.nf import NF

DEFAULT_EXPERIMENT_ITERATIONS = 5


class Experiment:
    def __init__(
        self,
        name: str,
        pktgen: Pktgen,
        nf: NF,
        log_file: Optional[Path] = None,
        iterations: int = DEFAULT_EXPERIMENT_ITERATIONS,
    ) -> None:
        self.name = name
        self.iterations = iterations
        self.pktgen = pktgen
        self.nf = nf

        if log_file:
            out_file_path = Path(log_file)

            if out_file_path.exists():
                assert out_file_path.is_file()
                self.log_file = open(log_file, "a")
            else:
                out_file_path.parents[0].mkdir(parents=True, exist_ok=True)
                self.log_file = open(log_file, "w")
        else:
            self.log_file = None

        self.log()
        self.log(f"=================== {name} ===================")

    def log(self, msg=""):
        now = datetime.now()
        ts = now.strftime("%Y-%m-%d %H:%M:%S")
        if self.log_file:
            print(f"[{ts}][{self.name}] {msg}", file=self.log_file, flush=True)
        else:
            print(f"[{ts}][{self.name}] {msg}", flush=True)

    def run(self, step_progress: Progress, current_iter: int) -> None:
        raise NotImplementedError

    def run_many(self, progress: Progress, step_progress: Progress) -> None:
        task_id = progress.add_task("", total=self.iterations, name=self.name)
        for iter in range(self.iterations):
            self.run(step_progress, iter)
            progress.update(task_id, advance=1)

        progress.update(task_id, description="[bold green] done!")


class ExperimentTracker:
    def __init__(self) -> None:
        self.overall_progress = Progress(
            TimeElapsedColumn(),
            BarColumn(),
            TimeRemainingColumn(),
            TextColumn("{task.description}"),
        )

        self.experiment_iters_progress = Progress(
            TextColumn("  "),
            TextColumn("[bold blue]{task.fields[name]}: " "{task.percentage:.0f}%"),
            BarColumn(),
            TimeRemainingColumn(),
            TextColumn("{task.description}"),
        )

        self.step_progress = Progress(
            TextColumn("  "),
            TimeElapsedColumn(),
            TextColumn("[bold purple]"),
            BarColumn(),
            TimeRemainingColumn(),
            TextColumn("{task.description}"),
        )

        self.progress_group = Group(
            Group(self.step_progress, self.experiment_iters_progress),
            self.overall_progress,
        )

        self.experiments: list[Experiment] = []

    def add_experiment(self, experiment: Experiment) -> None:
        self.experiments.append(experiment)

    def add_experiments(self, experiments: list[Experiment]) -> None:
        self.experiments += experiments

    def run_experiments(self):
        with Live(self.progress_group):
            nb_exps = len(self.experiments)
            overall_task_id = self.overall_progress.add_task("", total=nb_exps)

            for i, exp in enumerate(self.experiments):
                description = f"[bold #AAAAAA]({i} out of {nb_exps} experiments)"
                self.overall_progress.update(overall_task_id, description=description)
                exp.run_many(self.experiment_iters_progress, self.step_progress)
                self.overall_progress.update(overall_task_id, advance=1)

            self.overall_progress.update(overall_task_id, description="[bold green] All done!")


class SingleCore(Experiment):
    def __init__(
        self,
        # Experiment parameters
        name: str,
        save_name: Path,
        # Hosts
        pktgen: Pktgen,
        nf: NF,
        # Pktgen
        pcap: Path,
        logical_batch_size: Optional[int],
        experiment_log_file: Optional[Path] = None,
        iterations: int = DEFAULT_EXPERIMENT_ITERATIONS,
        console: Console = Console(),
    ) -> None:
        super().__init__(name, pktgen, nf, experiment_log_file, iterations)

        # Experiment parameters
        self.name = name
        self.save_name = save_name

        # Hosts
        self.pktgen = pktgen
        self.nf = nf

        # Pktgen
        self.pcap = pcap
        self.logical_batch_size = logical_batch_size

        self.console = console

        self._sync()

    def _sync(self):
        header = f"#it, tput (bps), tput (pps)\n"

        self.experiment_tracker = set()
        self.save_name.parent.mkdir(parents=True, exist_ok=True)

        # If file exists, continue where we left off.
        if self.save_name.exists():
            with open(self.save_name) as f:
                read_header = f.readline()
                assert header == read_header

                for row in f.readlines():
                    cols = row.split(",")
                    it = int(cols[0])
                    self.experiment_tracker.add(it)
        else:
            with open(self.save_name, "w") as f:
                f.write(header)

    def run(
        self,
        step_progress: Progress,
        current_iter: int,
    ) -> None:
        task_id = step_progress.add_task(self.name, total=1)

        if current_iter in self.experiment_tracker:
            self.console.log(f"[orange1]Skipping: {current_iter}")
            step_progress.update(task_id, advance=1)
            return

        self.log("Launching pktgen")
        self.pktgen.launch(
            pcap=self.pcap,
            sync_cores=True,
            logical_batch_size=self.logical_batch_size,
        )

        self.log("Launching NF")
        self.nf.launch()

        self.log("Waiting for Pktgen")
        self.pktgen.wait_launch()

        self.log("Waiting for NF")
        self.nf.wait_launch()

        self.log("Starting experiment")

        step_progress.update(task_id, description=f"({current_iter})")

        report = self.pktgen.bench()
        self.log(str(report.tput))

        with open(self.save_name, "a") as f:
            f.write(f"{current_iter}")
            f.write(f",{report.tput.bps}")
            f.write(f",{report.tput.pps}")
            f.write(f"\n")

        step_progress.update(task_id, advance=1)
        step_progress.update(task_id, visible=False)

        self.pktgen.quit()
        self.nf.kill()
