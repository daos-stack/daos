"""
(C) Copyright 2020-2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from agent_utils import include_local_host
from apricot import TestWithServers
from dfuse_utils import get_dfuse, start_dfuse
from exception_utils import CommandFailure


class DfuseTestBase(TestWithServers):
    """Runs Dfuse test suites.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a TestWithServers object."""
        super().__init__(*args, **kwargs)
        self.dfuse = None

    def setUp(self):
        """Set up the test case."""
        super().setUp()
        # using localhost as client if client list is empty
        if not self.hostlist_clients:
            self.hostlist_clients = include_local_host(None)

    def stop_job_managers(self):
        """Stop the test job manager followed by dfuse.

        Returns:
            list: a list of exceptions raised stopping the agents

        """
        error_list = super().stop_job_managers()
        try:
            self.stop_dfuse()
        except CommandFailure as error:
            error_list.append("Error stopping dfuse: {}".format(error))
        return error_list

    def load_dfuse(self, hosts, namespace=None):
        """Create a DfuseCommand object

        Args:
            hosts (NodeSet): hosts on which to start Dfuse
            namespace (str, optional): dfuse namespace. Defaults to None
        """
        self.dfuse = get_dfuse(self, hosts, namespace)

    def start_dfuse(self, hosts, pool=None, container=None, **params):
        """Create a DfuseCommand object and use it to start Dfuse.

        Args:
            hosts (NodeSet): hosts on which to start Dfuse
            pool (TestPool, optional): pool to mount. Defaults to None
            container (TestContainer, optional): container to mount. Defaults to None
            params (Object, optional): Dfuse command arguments to update
        """
        if self.dfuse is None:
            self.load_dfuse(hosts)
        start_dfuse(self, self.dfuse, pool=pool, container=container, **params)

    def stop_dfuse(self):
        """Stop Dfuse and unset the DfuseCommand object."""
        if self.dfuse:
            self.dfuse.stop()
            self.dfuse = None
