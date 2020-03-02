#!/usr/bin/python
'''
  (C) Copyright 2020 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
'''
from __future__ import print_function

import subprocess

from ClusterShell.NodeSet import NodeSet
from apricot import TestWithServers
from test_utils_pool import TestPool
from test_utils_container import TestContainer
from fio_utils import FioCommand
from command_utils import CommandFailure
from dfuse_utils import Dfuse


class FioBase(TestWithServers):
    """Base fio class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a FioBase object."""
        super(FioBase, self).__init__(*args, **kwargs)
        self.fio_cmd = None
        self.processes = None
        self.manager = None
        self.dfuse = None
        self.co_prop = None

    def setUp(self):
        """Set up each test case."""
        # obtain separate logs
        self.update_log_file_names()

        # Start the servers and agents
        super(FioBase, self).setUp()

        # removing runner node from hostlist_client, only need one client node.
        self.hostlist_clients = self.hostlist_clients[:-1]

        # Get the parameters for Fio
        self.fio_cmd = FioCommand()
        self.fio_cmd.get_params(self)
        self.processes = self.params.get("np", '/run/fio/client_processes/*')
        self.manager = self.params.get("manager", '/run/fio/*', "MPICH")
        self.co_prop = self.params.get("container_properties",
                                       "/run/container/*")

    def tearDown(self):
        """Tear down each test case."""
        try:
            self.dfuse = None
        finally:
            # Stop the servers and agents
            super(FioBase, self).tearDown()

    def _create_pool(self):
        """Create a pool and execute Fio."""
        # Get the pool params
        # pylint: disable=attribute-defined-outside-init
        self.pool = TestPool(self.context, dmg_command=self.get_dmg_command())
        self.pool.get_params(self)

        # Create a pool
        self.pool.create()

    def _create_cont(self):
        """Create a TestContainer object to be used to create container."""
        # TO-DO: Enable container using TestContainer object,
        # once DAOS-3355 is resolved.
        # Get Container params
        self.container = TestContainer(self.pool)
        self.container.get_params(self)

        # create container
        self.container.create(con_in=self.co_prop)


    def _start_dfuse(self):
        """Create a DfuseCommand object to start dfuse."""
        # Get Dfuse params
        self.dfuse = Dfuse(self.hostlist_clients, self.tmp, self.basepath)
        self.dfuse.get_params(self)

        # update dfuse params
        self.dfuse.set_dfuse_params(self.pool)
        self.dfuse.set_dfuse_cont_param(self.container)

        try:
            # start dfuse
            self.dfuse.run()
        except CommandFailure as error:
            self.log.error("Dfuse command %s failed on hosts %s",
                           str(self.dfuse), str(
                               NodeSet.fromlist(self.dfuse.hosts)),
                           exc_info=error)
            self.fail("Unable to launch Dfuse.\n")

    def execute_fio(self):
        """Runner method for Fio."""
        # Create a pool if one does not already exist
        if self.pool is None:
            self._create_pool()

        # start dfuse if api is POSIX
        if self.fio_cmd.api.value == "POSIX":
            # Connect to the pool, create container and then start dfuse
            # Uncomment below two lines once DAOS-3355 is resolved
            self.pool.connect()
            self._create_cont()
            self._start_dfuse()
            self.fio_cmd.update(
                "global", "directory", self.dfuse.mount_dir.value,
                "fio --name=global --directory")

        # Run Fio
        self.fio_cmd.hosts = self.hostlist_clients
        self.fio_cmd.run()
