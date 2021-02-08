#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from dfuse_test_base import DfuseTestBase
from fio_utils import FioCommand
# from daos_utils import DaosCommand


class FioBase(DfuseTestBase):
    # pylint: disable=too-many-ancestors
    """Base fio class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a FioBase object."""
        super(FioBase, self).__init__(*args, **kwargs)
        self.fio_cmd = None
        self.processes = None
        self.manager = None

    def setUp(self):
        """Set up each test case."""
        # obtain separate logs
        self.update_log_file_names()

        # Start the servers and agents
        super(FioBase, self).setUp()

        # Get the parameters for Fio
        self.fio_cmd = FioCommand()
        self.fio_cmd.get_params(self)
        self.processes = self.params.get("np", '/run/fio/client_processes/*')
        self.manager = self.params.get("manager", '/run/fio/*', "MPICH")

    def execute_fio(self, directory=None, stop_dfuse=True):
        """Runner method for Fio.

        Args:
            directory (str): path for fio run dir
            stop_dfuse (bool): Flag to stop or not stop dfuse as part of this
                               method.
        """
        # Create a pool if one does not already exist
        if self.pool is None:
            self.add_pool(connect=False)

        # start dfuse if api is POSIX
        if self.fio_cmd.api.value == "POSIX":
            if directory:
                self.fio_cmd.update(
                    "global", "directory", directory,
                    "fio --name=global --directory")
            else:
                self.add_container(self.pool)
                self.start_dfuse(
                    self.hostlist_clients, self.pool, self.container)
                self.fio_cmd.update(
                    "global", "directory", self.dfuse.mount_dir.value,
                    "fio --name=global --directory")

        # Run Fio
        self.fio_cmd.hosts = self.hostlist_clients
        self.fio_cmd.run()

        if stop_dfuse:
            self.stop_dfuse()
