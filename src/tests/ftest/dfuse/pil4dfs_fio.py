"""
  (C) Copyright 2019-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os

from dfuse_test_base import DfuseTestBase
from fio_utils import FioCommand
from general_utils import human_to_bytes


class Pil4dfsFio(DfuseTestBase):
    """Test class Description: Runs Fio with in small config.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a FioPil4dfs object."""
        super().__init__(*args, **kwargs)

        self.fio_cmd = None
        self.fio_params = {"thread": "", "blocksize": "", "size": "", "numjobs": ""}
        self.ioengines = {"dfuse": "", "dfs": ""}
        self.bw_deltas = {"READ": 0, "WRITE": 0}

    def setUp(self):
        """Set up each test case."""
        # obtain separate logs
        self.update_log_file_names()

        # Start the servers and agents
        super().setUp()

        for name in self.fio_params:
            self.fio_params[name] = self.params.get(name, "/run/fio/job/*", "")
        for name in self.ioengines:
            self.ioengines[name] = self.params.get("ioengine", f"/run/fio/{name}/*", "")
        for name in self.bw_deltas:
            self.bw_deltas[name] = self.params.get(name.lower(), "/run/fio/bw_deltas/*", 0)

    def create_container(self):
        """Created a DAOS POSIX container"""
        self.log.info("Creating pool")
        self.assertIsNone(self.pool)
        self.add_pool()
        self.log.debug("Created pool %s", str(self.pool))

        self.log.info("Creating container")
        self.assertIsNone(self.container)
        self.add_container(self.pool)
        self.log.debug("Created container %s", str(self.container))

    def destroy_container(self):
        """Destroy DAOS POSIX container previously created"""
        self.assertIsNotNone(self.container)
        self.log.debug("Destroying container %s", str(self.container))
        self.destroy_containers(self.container)
        self.container = None

        self.assertIsNotNone(self.pool)
        self.log.debug("Destroying pool %s", str(self.pool))
        self.destroy_pools(self.pool)
        self.pool = None

    def get_bandwidths(self, fio_result):
        """Returns FIO bandwidths

        Args
            fio_result (RemoteCommandResult): results of a FIO command.

        Returns
            dict: FIO read and write bandwidths.

        """
        bws = {}
        fio_stdout = next(iter(fio_result.all_stdout.values()))
        for line in fio_stdout.splitlines()[-2:]:
            key, tmp = line.split()[:2]
            key = key.split(':')[0]
            bw_human = tmp.split('=')[1]
            bw_raw = human_to_bytes(bw_human.split('/')[0])
            bws[key] = bw_raw, bw_human

        return bws

    def run_fio_dfuse(self):
        """Run and return the result of running FIO over a DFuse mount point.

        Returns:
            RemoteCommandResult: Results of the FIO command.

        """
        self.create_container()
        self.log.info("Mounting DFuse mount point")
        self.start_dfuse(self.hostlist_clients, self.pool, self.container)
        self.log.debug("Mounted DFuse mount point %s", str(self.dfuse))

        self.fio_cmd = FioCommand()
        self.fio_cmd.get_params(self)
        self.fio_cmd.update(
            "global", "directory", self.dfuse.mount_dir.value,
            f"fio --name=global --directory={self.dfuse.mount_dir.value}")
        self.fio_cmd.update(
            "global", "ioengine", self.ioengines["dfuse"],
            f"fio --name=global --ioengine={self.ioengines['dfuse']}")
        self.fio_cmd.env['LD_PRELOAD'] = os.path.join(self.prefix, 'lib64', 'libpil4dfs.so')
        self.fio_cmd.hosts = self.hostlist_clients

        params = ", ".join(f"{name}={value}" for name, value in self.fio_params.items())
        self.log.info("Running FIO command: %s", params)
        self.log.debug(
            "FIO command: LD_PRELOAD=%s %s", self.fio_cmd.env['LD_PRELOAD'], str(self.fio_cmd))
        result = self.fio_cmd.run()
        bws = self.get_bandwidths(result)
        self.log.debug("DFuse bandwidths: %s", bws)

        self.assertIsNotNone(self.dfuse)
        self.log.debug("Stopping DFuse mount point %s", str(self.dfuse))
        self.stop_dfuse()
        self.destroy_container()

        return bws

    def run_fio_dfs(self):
        """Run and return the result of running FIO with DFS ioengine.

        Returns:
            RemoteCommandResult: Results of the FIO command.

        """
        self.create_container()

        self.fio_cmd = FioCommand()
        self.fio_cmd.get_params(self)
        self.fio_cmd.update(
            "global", "ioengine", self.ioengines["dfs"],
            f"fio --name=global --ioengine={self.ioengines['dfs']}")
        # NOTE DFS ioengine options must come after the ioengine that defines them is selected.
        self.fio_cmd.update(
            "job", "pool", self.pool.uuid,
            f"fio --name=job --pool={self.pool.uuid}")
        self.fio_cmd.update(
            "job", "cont", self.container.uuid,
            f"fio --name=job --cont={self.container.uuid}")
        self.fio_cmd.hosts = self.hostlist_clients

        params = ", ".join(f"{name}={value}" for name, value in self.fio_params.items())
        self.log.info("Running FIO command: %s", params)
        self.log.debug("FIO command: %s", str(self.fio_cmd))
        result = self.fio_cmd.run()
        bws = self.get_bandwidths(result)
        self.log.debug("DFS bandwidths: %s", bws)

        self.destroy_container()

        return bws

    def test_fio(self):
        """Jira ID: DAOS-14657.

        Test Description:
            Run FIO over DFUSE mount point
            Run FIO with DFS ioengine
            Check bandwidth consistency

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=piil4dfs,fio,thread,fork
        :avocado: tags=Pil4dfsFio,test_fio
        """
        self.log_step("Running FIO with DFuse")
        dfuse_bws = self.run_fio_dfuse()

        self.log_step("Running FIO with DFS")
        dfs_bws = self.run_fio_dfs()

        self.log_step("Comparing FIO bandwidths of DFuse and DFS")
        errors_count = 0
        # pylint: disable=consider-using-dict-items
        for name in dfuse_bws:
            dfuse_raw, dfuse_human = dfuse_bws[name]
            dfs_raw, dfs_human = dfs_bws[name]
            delta = abs(dfuse_raw - dfs_raw) * 100 / max(dfuse_raw, dfs_raw)
            self.log.debug(
                "Comparing %s bandwidths: delta=%.2f%%, DFuse=%s, DFS=%s",
                name, delta, dfuse_human, dfs_human)
            if self.bw_deltas[name] <= delta:
                self.log.info(
                    "FIO %s bandwidth difference should be < %i%%: got=%.2f%%",
                    name, self.bw_deltas[name], delta)
                errors_count += 1
        self.assertEqual(errors_count, 0, "Test failed: FIO bandwidths are not consistants")

        self.log_step("Test passed")
