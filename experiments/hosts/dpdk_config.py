from typing import Optional, Iterable


class DpdkConfig:
    """Represent DPDK command-line options.

    Attributes:
        cores: List of cores to run on.
        mem_channels: Number of memory channels to use.
        drivers: Load external drivers. Can be a single shared object file, or
          a directory containing multiple driver shared objects.
        mem_alloc: Amount of memory to preallocate at startup.
        mem_ranks: Set number of memory ranks.
        xen_dom0: Support application running on Xen Domain0 without hugetlbfs.
        syslog: Set syslog facility.
        socket_mem: Preallocate specified amounts of memory per socket.
        huge_dir: Use specified hugetlbfs directory instead of autodetected
          ones. This can be a sub-directory within a hugetlbfs mountpoint.
        proc_type: Set the type of the current process. (`primary`,
          `secondary`, or `auto`)
        file_prefix: Use a different shared data file prefix for a DPDK
          process. This option allows running multiple independent DPDK
          primary/secondary processes under different prefixes.
        pci_block_list: Skip probing specified PCI device to prevent EAL from
          using it.
        pci_allow_list: Add PCI devices in to the list of devices to probe.
        vdev: Add a virtual device using the format:
          `<driver><id>[,key=val, ...]`.
        vmware_tsc_map: Use VMware TSC map instead of native RDTSC.
        base_virtaddr: Attempt to use a different starting address for all
          memory maps of the primary DPDK process. This can be helpful if
          secondary processes cannot start due to conflicts in address map.
        vfio_intr: Use specified interrupt mode for devices bound to VFIO
          kernel driver. (`legacy`, `msi`, or `msix`)
        create_uio_dev: Create `/dev/uioX` files for devices bound to igb_uio
         kernel driver (usually done by the igb_uio driver itself).
        extra_opt: Extra command-line options.

    Examples:
        Obtaining the DPDK configuration in command-line option format:

        >>> dpdk_config = DpdkConfig([0, 2], 4, pci_allow_list='05:00.0')
        >>> str(dpdk_config)
        '-l 0,2 -n 4 -a 05:00.0'
    """

    def __init__(
        self,
        cores: Iterable[int],
        mem_channels: Optional[int] = None,
        drivers: Optional[Iterable[str]] = None,
        mem_alloc: Optional[int] = None,
        mem_ranks: Optional[int] = None,
        xen_dom0: bool = False,
        syslog: bool = False,
        socket_mem: Optional[Iterable[int]] = None,
        huge_dir: Optional[str] = None,
        proc_type: Optional[str] = None,
        file_prefix: Optional[str] = None,
        pci_block_list: Optional[Iterable[str]] = None,
        pci_allow_list: Optional[Iterable[str]] = None,
        vdev: Optional[str] = None,
        vmware_tsc_map: bool = False,
        base_virtaddr: Optional[str] = None,
        vfio_intr: Optional[str] = None,
        create_uio_dev: bool = False,
        extra_opt: Optional[str] = None,
    ) -> None:
        self.cores = cores
        self.mem_channels = mem_channels
        self.drivers = drivers
        self.mem_alloc = mem_alloc
        self.mem_ranks = mem_ranks
        self.xen_dom0 = xen_dom0
        self.syslog = syslog
        self.socket_mem = socket_mem
        self.huge_dir = huge_dir
        self.proc_type = proc_type
        self.file_prefix = file_prefix
        self.pci_block_list = pci_block_list
        self.pci_allow_list = pci_allow_list
        self.vdev = vdev
        self.vmware_tsc_map = vmware_tsc_map
        self.base_virtaddr = base_virtaddr
        self.vfio_intr = vfio_intr
        self.create_uio_dev = create_uio_dev
        self.extra_opt = extra_opt

        if drivers is not None and not isinstance(drivers, list):
            self.drivers = [self.drivers]

        if pci_allow_list is not None and not isinstance(pci_allow_list, list):
            self.pci_allow_list = [self.pci_allow_list]

        if pci_block_list is not None and not isinstance(pci_block_list, list):
            self.pci_block_list = [self.pci_block_list]

    def __str__(self) -> str:
        opts = "-l " + ",".join(str(c) for c in self.cores)

        if self.mem_channels is not None:
            opts += f" -n {self.mem_channels}"

        if self.drivers is not None:
            for driver in self.drivers:
                opts += f" -d {driver}"

        if self.mem_alloc is not None:
            opts += f" -m {self.mem_alloc}"

        if self.mem_ranks is not None:
            opts += f" -r {self.mem_ranks}"

        if self.xen_dom0:
            opts += " --xen-dom0"

        if self.syslog:
            opts += " --syslog"

        if self.socket_mem is not None:
            opt = ",".join(str(sm) for sm in self.socket_mem)
            opts += f" --socket-mem {opt}"

        if self.huge_dir is not None:
            opts += f" --huge-dir {self.huge_dir}"

        if self.proc_type is not None:
            opts += f" --proc-type {self.proc_type}"

        if self.file_prefix is not None:
            opts += f" --file-prefix {self.file_prefix}"

        if self.pci_block_list is not None:
            for pci_block_list in self.pci_block_list:
                opts += f" -b {pci_block_list}"

        if self.pci_allow_list is not None:
            for pci_allow_list in self.pci_allow_list:
                opts += f" -a {pci_allow_list}"

        if self.vdev is not None:
            opts += f" --vdev {self.vdev}"

        if self.vmware_tsc_map:
            opts += " --vmware-tsc-map"

        if self.base_virtaddr is not None:
            opts += f" --base-virt-addr {self.base_virtaddr}"

        if self.vfio_intr is not None:
            opts += f" --vfio-intr {self.vfio_intr}"

        if self.create_uio_dev:
            opts += " --create-uio-dev"

        if self.extra_opt is not None:
            opts += self.extra_opt

        return opts
