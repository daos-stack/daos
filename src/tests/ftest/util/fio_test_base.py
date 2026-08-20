"""
  (C) Copyright 2020-2024 Intel Corporation.
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from fio_utils import get_fio


class FioBase(TestWithServers):
    """Base fio class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a FioBase object."""
        super().__init__(*args, **kwargs)
        self.fio_cmd = None
        self.processes = None
        self.manager = None

    def setUp(self):
        """Set up each test case."""
        # obtain separate logs
        self.update_log_file_names()

        # Start the servers and agents
        super().setUp()

        # Get the parameters for Fio
        self.fio_cmd = get_fio(self, self.hostlist_clients)
        self.processes = self.params.get("np", '/run/fio/client_processes/*')
        self.manager = self.params.get("manager", '/run/fio/*', "MPICH")

    def execute_fio(self):
        """Runner method for Fio."""
        self.fio_cmd.run()
