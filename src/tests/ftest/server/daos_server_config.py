#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from __future__ import print_function

from apricot import TestWithServers
from server_utils import ServerFailed


class DaosServerConfigTest(TestWithServers):
    """Daos server configuration tests.

    Test Class Description:
        Simple test to verify that the daos_server starts/stops properly given
        positive and negative values to its configuration file.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DaosServerConfigTest object."""
        super(DaosServerConfigTest, self).__init__(*args, **kwargs)
        self.setup_start_agents = False
        self.setup_start_servers = False

    def test_daos_server_config_basic(self):
        """JIRA ID: DAOS-1525.

        Test Description: Test daos_server start/stops properly.
        on the system.

        :avocado: tags=all,small,control,daily_regression,server_start,basic
        """
        # Setup the servers
        self.add_server_manager()
        self.configure_manager(
            "server", self.server_managers[0], self.hostlist_servers,
            self.hostfile_servers_slots)

        # Get the input to verify
        c_val = self.params.get("config_val", "/run/server_config_val/*/")

        # Identify the attribute and modify its value to test value
        self.assertTrue(
            self.server_managers[0].set_config_value(c_val[0], c_val[1]),
            "Error setting the '{}' config file parameter to '{}'".format(
                c_val[0], c_val[1]))

        self.log.info("Starting server with %s = %s", c_val[0], c_val[1])

        try:
            self.server_managers[0].start()
            exception = None
        except ServerFailed as err:
            exception = err

        # Verify
        if c_val[2] == "FAIL" and exception is None:
            self.log.error("Server was expected to fail")
            self.fail("{}".format(exception))
        elif c_val[2] == "PASS" and exception is not None:
            self.log.error("Server was expected to start")
            self.fail("{}".format(exception))
