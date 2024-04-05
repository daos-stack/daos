"""
  (C) Copyright 2019-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import json
import os

from ClusterShell.NodeSet import NodeSet
from cpu_utils import CpuInfo
from dfuse_test_base import DfuseTestBase
from fio_utils import FioCommand
from general_utils import bytes_to_human


class Pil4dfsFio(DfuseTestBase):
    """Test class Description: Runs Fio with in small config.

    :avocado: recursive
    """

    _FIO_RW_NAMES = ["write", "read"]

    def __init__(self, *args, **kwargs):
        """Initialize a FioPil4dfs object."""
        super().__init__(*args, **kwargs)

        self.fio_cmd = None
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
        self.assertIsNone(self.pool, "Unexpected pool before starting test")
        self.add_pool()

        self.log.info("Creating container")
        self.assertIsNone(self.container, "Unexpected container before starting test")
        self.add_container(self.pool)

    def _destroy_container(self):
        """Destroy DAOS POSIX container previously created"""
        if self.container is not None:
            self.log.debug("Destroying container %s", str(self.container))
            self.destroy_containers(self.container)
            self.container = None

        if self.pool is not None:
            self.log.debug("Destroying pool %s", str(self.pool))
            self.destroy_pools(self.pool)
            self.pool = None

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
        self._create_container()
        self.log.info("Mounting DFuse mount point")
        self.start_dfuse(self.hostlist_clients, self.pool, self.container)
        self.log.debug("Mounted DFuse mount point %s", str(self.dfuse))

        self.fio_cmd = FioCommand()
        self.fio_cmd.get_params(self)
        self.fio_cmd.update(
            "global", "directory", self.dfuse.mount_dir.value,
            f"fio --name=global --directory={self.dfuse.mount_dir.value}")
        self.fio_cmd.update("global", "ioengine", "psync", "fio --name=global --ioengine='psync'")
        self.fio_cmd.update(
            "global", "numjobs", self.fio_numjobs,
            f"fio --name=global --numjobs={self.fio_numjobs}")
        self.fio_cmd.update(
            "global", "cpus_allowed", self.fio_cpus_allowed,
            f"fio --name=global --cpus_allowed={self.fio_cpus_allowed}")
        self.fio_cmd.env['LD_PRELOAD'] = os.path.join(self.prefix, 'lib64', 'libpil4dfs.so')
        self.fio_cmd.hosts = self.hostlist_clients

        bws = {}
        for rw in Pil4dfsFio._FIO_RW_NAMES:
            self.fio_cmd.update("job", "rw", rw, f"fio --name=job --rw={rw}")

            params = ", ".join(f"{name}={value}" for name, value in self.fio_params.items())
            self.log.info("Running FIO command: rw=%s, %s", rw, params)
            self.log.debug(
                "FIO command: LD_PRELOAD=%s %s", self.fio_cmd.env['LD_PRELOAD'], str(self.fio_cmd))
            result = self.fio_cmd.run()
            bws[rw] = self._get_bandwidth(result, rw)
            self.log.debug("DFuse bandwidths for %s: %s", rw, bws[rw])

        if self.dfuse is not None:
            self.log.debug("Stopping DFuse mount point %s", str(self.dfuse))
            self.stop_dfuse()
            self._destroy_container()

        return bws

    def _run_fio_dfs(self):
        """Run and return the result of running FIO with DFS ioengine.

        Returns:
            dict: Read and Write bandwidths of the FIO command.

        """
        self._create_container()

        self.fio_cmd = FioCommand()
        self.fio_cmd.get_params(self)
        self.fio_cmd.update("global", "ioengine", "dfs", "fio --name=global --ioengine='dfs'")
        self.fio_cmd.update(
            "global", "numjobs", self.fio_numjobs,
            f"fio --name=global --numjobs={self.fio_numjobs}")
        self.fio_cmd.update(
            "global", "cpus_allowed", self.fio_cpus_allowed,
            f"fio --name=global --cpus_allowed={self.fio_cpus_allowed}")
        # NOTE DFS ioengine options must come after the ioengine that defines them is selected.
        self.fio_cmd.update(
            "job", "pool", self.pool.uuid,
            f"fio --name=job --pool={self.pool.uuid}")
        self.fio_cmd.update(
            "job", "cont", self.container.uuid,
            f"fio --name=job --cont={self.container.uuid}")
        self.fio_cmd.hosts = self.hostlist_clients

        bws = {}
        for rw in Pil4dfsFio._FIO_RW_NAMES:
            self.fio_cmd.update("job", "rw", rw, f"fio --name=job --rw={rw}")

            params = ", ".join(f"{name}={value}" for name, value in self.fio_params.items())
            self.log.info("Running FIO command: rw=%s, %s", rw, params)
            self.log.debug("FIO command: %s", str(self.fio_cmd))
            result = self.fio_cmd.run()
            bws[rw] = self._get_bandwidth(result, rw)
            self.log.debug("DFS bandwidths for %s: %s", rw, bws[rw])

        self._destroy_container()

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
