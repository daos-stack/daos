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
from general_utils import bytes_to_human, percent_change


class Pil4dfsFio(TestWithServers):
    """Test class Description: Runs Fio with in small config.

    :avocado: recursive
    """

    _FIO_RW_NAMES = ["write", "read"]

    def __init__(self, *args, **kwargs):
        """Initialize a FioPil4dfs object."""
        super().__init__(*args, **kwargs)

        self.fio_numjobs = 0
        self.fio_cpus_allowed = ""
        self.fio_pil4dfs_ioengines = []

    def setUp(self):
        """Set up each test case."""
        # obtain separate logs
        self.update_log_file_names()

        # Start the servers and agents
        super().setUp()

        self.fio_pil4dfs_ioengines = self.params.get(
            "pil4dfs_ioengines", "/run/fio/job/params/*", default=[])

        cpu_info = CpuInfo(self.log, self.hostlist_clients)
        cpu_info.scan()
        _, arch = cpu_info.get_architectures()[0]

        self.fio_numjobs = self.params.get("numjobs", "/run/fio/job/params/*")
        if self.fio_numjobs is None:
            self.fio_numjobs = int(arch.quantity / arch.threads_core)

        if self.fio_numjobs % arch.numas != 0:
            self.fail(
                "Client with unsupported quantity of NUMA nodes:"
                f" Number of jobs ({self.fio_numjobs})"
                f" is not a multiple of the NUMA node quantity ({arch.numas})")

        cpus = []
        cores_quantity = int(self.fio_numjobs / arch.numas)
        for numa_idx in range(arch.numas):
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
            fio_result (CommandResult): results of a FIO command.
            rw (str): Type of I/O pattern.

        Returns:
            int: Bandwidth of the FIO command.

        """
        fio_stdout = next(iter(fio_result.all_stdout.values()))
        # NOTE With dfuse and pil4dfs some junk messages could eventually be printed
        if fio_stdout[0] != '{':
            fio_stdout = '{' + fio_stdout.partition('{')[2]
        fio_json = json.loads(fio_stdout)
        return fio_json["jobs"][0][rw]['bw_bytes']

    def _run_fio_pil4dfs(self, ioengine):
        """Run and return the result of running FIO with the PIL4DFS interception library.

        Args:
            ioengine (str): Name of the IO engine to use.

        Returns:
            dict: Read and Write bandwidths of the FIO command.

        """
        container = self._create_container()

        self.log.info("Mounting DFuse mount point")
        dfuse = get_dfuse(self, self.hostlist_clients)
        start_dfuse(self, dfuse, container.pool, container)

        fio_cmd = FioCommand()
        fio_cmd.get_params(self)
        fio_cmd.update_directory(dfuse.mount_dir.value)
        fio_cmd.update("global", "ioengine", ioengine, f"fio --name=global --ioengine='{ioengine}'")
        fio_cmd.update(
            "global", "numjobs", self.fio_numjobs,
            f"fio --name=global --numjobs={self.fio_numjobs}")
        fio_cmd.update(
            "global", "cpus_allowed", self.fio_cpus_allowed,
            f"fio --name=global --cpus_allowed={self.fio_cpus_allowed}")
        fio_cmd.env['LD_PRELOAD'] = os.path.join(self.prefix, 'lib64', 'libpil4dfs.so')
        fio_cmd.env['CRT_TIMEOUT'] = 10
        fio_cmd.hosts = self.hostlist_clients

        bws = {}
        for rw in Pil4dfsFio._FIO_RW_NAMES:
            fio_cmd.update("job", "rw", rw, f"fio --name=job --rw={rw}")

            self.log.debug("FIO command: LD_PRELOAD=%s %s", fio_cmd.env['LD_PRELOAD'], str(fio_cmd))
            result = fio_cmd.run()
            bws[rw] = self._get_bandwidth(result, rw)
            self.log.info(
                "FIO bandwidths with PIL4DFS: ioengine=%s, rw=%s, bw=%s", ioengine, rw, bws[rw])

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
            "job", "numjobs", self.fio_numjobs, f"fio --name=job --numjobs={self.fio_numjobs}")
        fio_cmd.update(
            "job", "cpus_allowed", self.fio_cpus_allowed,
            f"fio --name=job --cpus_allowed={self.fio_cpus_allowed}")
        # NOTE DFS ioengine options must come after the ioengine that defines them is selected.
        fio_cmd.update(
            "job", "pool", container.pool.uuid, f"fio --name=job --pool={container.pool.uuid}")
        fio_cmd.update("job", "cont", container.uuid, f"fio --name=job --cont={container.uuid}")
        fio_cmd.hosts = self.hostlist_clients

        bws = {}
        for rw in Pil4dfsFio._FIO_RW_NAMES:
            fio_cmd.update("job", "rw", rw, f"fio --name=job --rw={rw}")

            self.log.debug("FIO command: %s", str(fio_cmd))
            result = fio_cmd.run()
            bws[rw] = self._get_bandwidth(result, rw)
            self.log.info("FIO bandwidths with DFS: rw=%s, bw=%s", rw, bws[rw])

        container.destroy()
        container.pool.destroy()

        return bws

    def test_pil4dfs_vs_dfs(self):
        """Jira ID: DAOS-14657.

        Test Description:
            Run FIO with DFS ioengine
            Run FIO with the PIL4DFS interception library and some ioengines
            Check bandwidth consistency of FIO DFS ioengine and the PIL4DFS interception library

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=pil4dfs,dfuse,dfs,fio
        :avocado: tags=Pil4dfsFio,test_pil4dfs_vs_dfs
        """
        bw_deltas = {}
        for name in self._FIO_RW_NAMES:
            bw_deltas[name] = self.params.get(
                name.lower(), "/run/test_pil4dfs_vs_dfs/bw_deltas/*", 0)

        self.log_step("Running FIO with DFS")
        dfs_bws = self._run_fio_dfs()

        dfuse_bws = {}
        for ioengine in self.fio_pil4dfs_ioengines:
            self.log_step(f"Running FIO with {ioengine} and the PIL4DFS interception library")
            dfuse_bws = self._run_fio_pil4dfs(ioengine)

            self.log_step(f"Comparing FIO bandwidths of DFuse with {ioengine} ioengine and DFS")
            for rw in Pil4dfsFio._FIO_RW_NAMES:
                delta = 100 * percent_change(dfs_bws[rw], dfuse_bws[rw])
                self.log.debug(
                    "Comparing %s bandwidths: delta=%.2f%%, DFuse=%s (%iB), DFS=%s (%iB)",
                    rw, delta, bytes_to_human(dfuse_bws[rw]), dfuse_bws[rw],
                    bytes_to_human(dfs_bws[rw]), dfs_bws[rw])
                if bw_deltas[rw] <= abs(delta):
                    self.log.info(
                        "FIO %s bandwidth difference should be < %i%%: got=%.2f%%",
                        rw, bw_deltas[rw], abs(delta))

        self.log_step("Test passed")
