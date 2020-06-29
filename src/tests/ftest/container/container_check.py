#!/usr/bin/python
"""
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
"""

from command_utils import CommandFailure
from daos_utils import DaosCommand
from test_utils_pool import TestPool
from test_utils_container import TestContainer
from dfuse_utils import Dfuse
from apricot import TestWithServers

class DfuseContainerCheck(TestWithServers):
    """Base Dfuse Container check test class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DfuseContainerCheck object."""
        super(DfuseContainerCheck, self).__init__(*args, **kwargs)
        self.dfuse = None
        self.pool = None
        self.container = None

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super(DfuseContainerCheck, self).setUp()

    def tearDown(self):
        """Tear down each test case."""
        try:
            if self.dfuse:
                self.dfuse.stop()
        finally:
            # Stop the servers and agents
            super(DfuseContainerCheck, self).tearDown()

    def create_pool(self):
        """Create a TestPool object to use with ior."""
        # Get the pool params
        self.pool = TestPool(
            self.context, dmg_command=self.get_dmg_command())
        self.pool.get_params(self)

        # Create a pool
        self.pool.create()

    def start_dfuse(self):
        """Create a DfuseCommand object to start dfuse.
        """

        # Get Dfuse params
        self.dfuse = Dfuse(self.hostlist_clients, self.tmp)
        self.dfuse.get_params(self)

        # update dfuse params
        self.dfuse.set_dfuse_params(self.pool)
        self.dfuse.set_dfuse_cont_param(self.container)
        self.dfuse.set_dfuse_exports(self.server_managers[0], self.client_log)

        try:
            # start dfuse
            self.dfuse.run(False)
        except CommandFailure as error:
            self.log.error("Dfuse command %s failed on hosts %s",
                           str(self.dfuse),
                           self.dfuse.hosts,
                           exc_info=error)
            self.fail("Test was expected to pass but it failed.\n")

    def test_dfusecontainercheck(self):
        """Jira ID: DAOS-3635.

        Test Description:
            Purpose of this test is to try and mount different container types
            to dfuse and check the behavior.
        Use cases:
            Create pool
            Create container of type default
            Try to mount to dfuse and check the behaviour.
            Create container of type POSIX.
            Try to mount to dfuse and check the behaviour.
        :avocado: tags=all,small,full_regression,dfusecontainercheck
        """
        # get test params for cont and pool count
        cont_types = self.params.get("cont_types", '/run/container/*')

        # Create a pool and start dfuse.
        self.create_pool()

        for cont_type in cont_types:
            # Get container params
            self.container = TestContainer(
                self.pool, daos_command=DaosCommand(self.bin))
            self.container.get_params(self)
            # create container
            if cont_type == "POSIX":
                self.container.type.update(cont_type)
            self.container.create()
            try:
                # mount fuse
                self.start_dfuse()
                # check if fuse got mounted
                self.dfuse.check_running()
                # fail the test if fuse mounts with non-posix type container
                if cont_type == "":
                    self.fail("Non-Posix type container got mounted over dfuse")
            except CommandFailure as error:
                # expected to throw CommandFailure exception for non-posix type
                # container
                if cont_type == "":
                    self.log.info("Expected behaviour: Default container type \
                        is expected to fail on dfuse mount: %s", str(error))
                # fail the test if exception is caught for POSIX type container
                elif cont_type == "POSIX":
                    self.log.error("Posix Container dfuse mount \
                        failed: %s", str(error))
                    self.fail("Posix container type was expected to mount \
                        over dfuse")
            # stop fuse and container for next iteration
            if not cont_type == "":
                self.dfuse.stop()
            self.container.destroy(1)
