"""
  (C) Copyright 2019-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import json
import os

from apricot import TestWithServers
from ClusterShell.NodeSet import NodeSet
from cpu_utils import CpuInfo
from dfuse_utils import get_dfuse, start_dfuse
from fio_utils import FioCommand
from general_utils import bytes_to_human


class Pil4dfsFio(TestWithServers):
    """Test class Description: Runs Fio with in small config.

    :avocado: recursive
    """

    _FIO_RW_NAMES = ["write", "read"]

    def __init__(self, *args, **kwargs):
        """Initialize a FioPil4dfs object."""
        super().__init__(*args, **kwargs)

        self.fio_params = {"thread": "", "blocksize": "", "size": ""}
        self.fio_numjobs = 0
        self.fio_cpus_allowed = ""

    def setUp(self):
        """Set up each test case."""
        # obtain separate logs
        self.update_log_file_names()

        # Start the servers and agents
        super().setUp()

        for name in self.fio_params:
            self.fio_params[name] = self.params.get(name, "/run/fio/job/*", "")

        cpu_info = CpuInfo(self.log, self.hostlist_clients)
        cpu_info.scan()
        _, arch = cpu_info.get_architectures()[0]
        if arch.numas != 2:
            self.fail(f"Client with unsupported quantity of NUMA nodes: want=2, got={arch.numas}")
        self.fio_numjobs = int(arch.quantity / arch.threads_core)
        cpus = []
        cores_quantity = int(self.fio_numjobs / 2)
        for numa_idx in range(2):
            cpus += arch.get_numa_cpus(numa_idx)[:cores_quantity]
        self.fio_cpus_allowed = str(NodeSet(str(cpus)))[1:-1]

    def _create_container(self):
        """Created a DAOS POSIX container"""
        self.log.info("Creating pool")
        pool = self.get_pool()

        self.log.info("Creating container")
        return self.get_container(pool)

    def _get_bandwidth(self, fio_result, rw):
        """Returns FIO bandwidth of a given I/O pattern

        Args:
            fio_result (RemoteCommandResult): results of a FIO command.
            rw (str): Type of I/O pattern.

        Returns:
            int: Bandwidth of the FIO command.

        """
        fio_stdout = next(iter(fio_result.all_stdout.values()))
        # NOTE With dfuse and pil4dfs some junk messages could be eventually printed
        if fio_stdout[0] != '{':
            fio_stdout = '{' + fio_stdout.partition('{')[2]
        fio_json = json.loads(fio_stdout)
        return fio_json["jobs"][0][rw]['bw_bytes']

    def _run_fio_dfuse(self):
        """Run and return the result of running FIO over a DFuse mount point.

        Returns:
            dict: Read and Write bandwidths of the FIO command.

        """
        container = self._create_container()

        self.log_step("Mounting DFuse mount point")
        dfuse = get_dfuse(self, self.hostlist_clients)
        start_dfuse(self, dfuse, container.pool, container)
        self.log.debug("Mounted DFuse mount point %s", str(dfuse))

        fio_cmd = FioCommand()
        fio_cmd.get_params(self)
        fio_cmd.update_directory(dfuse.mount_dir.value)
        fio_cmd.update("global", "ioengine", "psync", "fio --name=global --ioengine='psync'")
        fio_cmd.update(
            "global", "numjobs", self.fio_numjobs,
            f"fio --name=global --numjobs={self.fio_numjobs}")
        fio_cmd.update(
            "global", "cpus_allowed", self.fio_cpus_allowed,
            f"fio --name=global --cpus_allowed={self.fio_cpus_allowed}")
        fio_cmd.env['LD_PRELOAD'] = os.path.join(self.prefix, 'lib64', 'libpil4dfs.so')
        fio_cmd.hosts = self.hostlist_clients

        bws = {}
        for rw in Pil4dfsFio._FIO_RW_NAMES:
            fio_cmd.update("job", "rw", rw, f"fio --name=job --rw={rw}")

            params = ", ".join(f"{name}={value}" for name, value in self.fio_params.items())
            self.log.info("Running FIO command: rw=%s, %s", rw, params)
            self.log.debug("FIO command: LD_PRELOAD=%s %s", fio_cmd.env['LD_PRELOAD'], str(fio_cmd))
            result = fio_cmd.run()
            bws[rw] = self._get_bandwidth(result, rw)
            self.log.debug("DFuse bandwidths for %s: %s", rw, bws[rw])

        dfuse.stop()
        container.destroy()
        container.pool.destroy()

        return bws

    def _run_fio_dfs(self):
        """Run and return the result of running FIO with DFS ioengine.

        Returns:
            dict: Read and Write bandwidths of the FIO command.

        """
        container = self._create_container()

        fio_cmd = FioCommand()
        fio_cmd.get_params(self)
        fio_cmd.update("global", "ioengine", "dfs", "fio --name=global --ioengine='dfs'")
        fio_cmd.update(
            "global", "numjobs", self.fio_numjobs,
            f"fio --name=global --numjobs={self.fio_numjobs}")
        fio_cmd.update(
            "global", "cpus_allowed", self.fio_cpus_allowed,
            f"fio --name=global --cpus_allowed={self.fio_cpus_allowed}")
        # NOTE DFS ioengine options must come after the ioengine that defines them is selected.
        fio_cmd.update(
            "job", "pool", container.pool.uuid, f"fio --name=job --pool={container.pool.uuid}")
        fio_cmd.update("job", "cont", container.uuid, f"fio --name=job --cont={container.uuid}")
        fio_cmd.hosts = self.hostlist_clients

        bws = {}
        for rw in Pil4dfsFio._FIO_RW_NAMES:
            fio_cmd.update("job", "rw", rw, f"fio --name=job --rw={rw}")

            params = ", ".join(f"{name}={value}" for name, value in self.fio_params.items())
            self.log.info("Running FIO command: rw=%s, %s", rw, params)
            self.log.debug("FIO command: %s", str(fio_cmd))
            result = fio_cmd.run()
            bws[rw] = self._get_bandwidth(result, rw)
            self.log.debug("DFS bandwidths for %s: %s", rw, bws[rw])

        container.destroy()
        container.pool.destroy()

        return bws

    def test_pil4dfs_vs_dfs(self):
        """Jira ID: DAOS-14657.

        Test Description:
            Run FIO over DFUSE mount point with PIL4DFS interception library
            Run FIO with DFS ioengine
            Check bandwidth consistency

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=pil4dfs,dfuse,dfs,fio
        :avocado: tags=Pil4dfsFio,test_pil4dfs_vs_dfs
        """
        bw_deltas = {}
        for name in self._FIO_RW_NAMES:
            bw_deltas[name] = self.params.get(
                name.lower(), "/run/test_pil4dfs_vs_dfs/bw_deltas/*", 0)

        self.log_step("Running FIO with DFuse")
        dfuse_bws = self._run_fio_dfuse()

        self.log_step("Running FIO with DFS")
        dfs_bws = self._run_fio_dfs()

        self.log_step("Comparing FIO bandwidths of DFuse and DFS")
        for rw in Pil4dfsFio._FIO_RW_NAMES:
            delta = abs(dfuse_bws[rw] - dfs_bws[rw]) * 100 / max(dfuse_bws[rw], dfs_bws[rw])
            self.log.debug(
                "Comparing %s bandwidths: delta=%.2f%%, DFuse=%s (%iB), DFS=%s (%iB)",
                rw, delta, bytes_to_human(dfuse_bws[rw]), dfuse_bws[rw],
                bytes_to_human(dfs_bws[rw]), dfs_bws[rw])
            if bw_deltas[rw] <= delta:
                self.log.info(
                    "FIO %s bandwidth difference should be < %i%%: got=%.2f%%",
                    rw, bw_deltas[rw], delta)

        self.log_step("Test passed")
