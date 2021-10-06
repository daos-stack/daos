#!/usr/bin/python
"""
(C) Copyright 2020-2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from ClusterShell.NodeSet import NodeSet

from apricot import TestWithServers
from agent_utils import include_local_host
from command_utils_base import CommandFailure
from dfuse_utils import Dfuse


class DfuseTestBase(TestWithServers):
    """Runs Dfuse test suites.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a TestWithServers object."""
        super().__init__(*args, **kwargs)
        self.dfuse = None

    def setUp(self):
        """Set up each test case."""
        super().setUp()
        if self.hostlist_clients is None:
            self.hostlist_clients = include_local_host(None)
        self.dfuse = Dfuse(self.hostlist_clients)
        self.dfuse.get_params(self)

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

    def start_dfuse(self, hosts, pool=None, container=None, mount_dir=None):
        """Create a Dfuse object and use it to start Dfuse.

        Args:
            hosts (list): list of hosts on which to start Dfuse
            pool (TestPool, optional): pool to use with Dfuse
            container (TestContainer, optional): container to use with Dfuse
            mount_dir (str, optional): updated mount dir name. Defaults to None.
        """
        self.dfuse.hosts = hosts
        if mount_dir:
            self.dfuse.mount_dir.update(mount_dir)
        try:
            self.dfuse.start(self.server_managers[0], self.client_log, pool, container)
        except CommandFailure as error:
            self.log.error(
                "Dfuse command %s failed on hosts %s", str(self.dfuse),
                str(NodeSet.fromlist(self.dfuse.hosts)), exc_info=error)
            self.fail("Test was expected to pass but it failed.")

    def stop_dfuse(self):
        """Stop Dfuse and unset the DfuseCommand object."""
        if self.dfuse.started:
            self.dfuse.stop()
