#!/usr/bin/python
"""
(C) Copyright 2020-2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
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
        super().__init__(*args, **kwargs)
        self.dfuse = None

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
        self.dfuse = Dfuse(hosts)
        self.dfuse.get_params(self)
        try:
            self.dfuse.start(
                self.server_managers[0], self.client_log, pool, container,
                mount_dir)
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
