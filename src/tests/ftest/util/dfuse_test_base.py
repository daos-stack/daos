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
The Government's rights to use, modify, reproduce, release, perform,
display, or disclose this software are subject to the terms of the Apache
License as provided in Contract No. B609815.
Any reproduction of computer software, computer software documentation, or
portions thereof marked with this legend must also reproduce the markings.
"""
from ClusterShell.NodeSet import NodeSet

from apricot import TestWithServers
from command_utils_base import CommandFailure
from dfuse_utils import Dfuse


class DfuseTestBase(TestWithServers):
    """Runs HDF5 vol test suites.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a TestWithServers object."""
        super(DfuseTestBase, self).__init__(*args, **kwargs)
        self.dfuse = None

    def tearDown(self):
        """Tear down each test case."""
        try:
            self.stop_dfuse()
        finally:
            # Stop the servers and agents
            super(DfuseTestBase, self).tearDown()

    def start_dfuse(self, hosts, pool, container):
        """Create a DfuseCommand object and use it to start Dfuse.

        Args:
            hosts (list): list of hosts on which to start Dfuse
            pool (TestPool): pool to use with Dfuse
            container (TestContainer): container to use with Dfuse
        """
        self.dfuse = Dfuse(hosts, self.tmp)
        self.dfuse.get_params(self)

        # Update dfuse params
        self.dfuse.set_dfuse_params(pool)
        self.dfuse.set_dfuse_cont_param(container)
        self.dfuse.set_dfuse_exports(self.server_managers[0], self.client_log)

        try:
            # Start dfuse
            self.dfuse.run()
        except CommandFailure as error:
            self.log.error(
                "Dfuse command %s failed on hosts %s", str(self.dfuse),
                str(NodeSet.fromlist(self.dfuse.hosts)), exc_info=error)
            self.fail("Test was expected to pass but it failed.")

    def stop_dfuse(self):
        """Stop Dfuse and unset the DfuseCommand object."""
        if self.dfuse:
            self.dfuse.stop()
            self.dfuse = None
