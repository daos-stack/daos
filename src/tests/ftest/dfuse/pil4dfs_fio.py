"""
  (C) Copyright 2019-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os

from dfuse_test_base import DfuseTestBase
from fio_utils import FioCommand


class Pil4dfsFio(DfuseTestBase):
    """Test class Description: Runs Fio with in small config.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a FioPil4dfs object."""
        super().__init__(*args, **kwargs)

        self.fio_cmd = None
        self.fio_params = {"thread": "", "blocksize": "", "size": "", "numjobs": ""}

    def setUp(self):
        """Set up each test case."""
        # obtain separate logs
        self.update_log_file_names()

        # Start the servers and agents
        super().setUp()

        self.fio_cmd = FioCommand()
        self.fio_cmd.get_params(self)
        for name in self.fio_params:
            self.fio_params[name] = self.params.get(name, "/run/fio/job/*")

    def tearDown(self):
        """Tear down each test case"""
        if self.dfuse is not None:
            self.log.debug("Stopping dfuse mount point %s", str(self.dfuse))
            self.stop_dfuse()

        if self.container is not None:
            self.log.debug("Destroying container %s", str(self.container))
            self.destroy_containers(self.container)
            self.container = None

        if self.pool is not None:
            self.log.debug("Destroying pool %s", str(self.pool))
            self.destroy_pools(self.pool)
            self.pool = None

    def test_fio(self):
        """Jira ID: DAOS-14657.

        Test Description:
            TODO

        Use Case:
            - TODO

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=piil4dfs,fio,thread,fork
        :avocado: tags=Pil4dfsFio,test_fio
        """
        self.log_step("Creating pool")
        self.assertIsNone(self.pool)
        self.add_pool()
        self.log.debug("Created pool %s", str(self.pool))

        self.log_step("Creating container")
        self.assertIsNone(self.container)
        self.add_container(self.pool)
        self.log.debug("Created container %s", str(self.container))

        self.log_step("Mounting dfuse mount point")
        self.start_dfuse(self.hostlist_clients, self.pool, self.container)
        self.log.debug("Mounted dfuse mount point %s", str(self.dfuse))

        self.fio_cmd.update(
            "global", "directory", self.dfuse.mount_dir.value,
            f"fio --name=global --directory={self.dfuse.mount_dir.value}")
        self.fio_cmd.env['LD_PRELOAD'] = os.path.join(self.prefix, 'lib64', 'libpil4dfs.so')
        self.fio_cmd.hosts = self.hostlist_clients

        params = ", ".join(f"{name}={value}" for name, value in self.fio_params.items())
        self.log_step(f"Running fio command: {params}")
        self.log.debug("fio command: %s", str(self.fio_cmd))
        self.log.debug("LD_PRELOAD=%s", self.fio_cmd.env['LD_PRELOAD'])
        self.fio_cmd.run()

        self.log_step("Test passed")
