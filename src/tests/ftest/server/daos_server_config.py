"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

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
        super().__init__(*args, **kwargs)
        self.start_agents_once = False
        self.start_servers_once = False
        self.setup_start_agents = False
        self.setup_start_servers = False

    def test_daos_server_config_basic(self):
        """JIRA ID: DAOS-1525.

        Test Description: Test daos_server start/stops properly.
        on the system.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=server,control,server_start,basic
        :avocado: tags=DaosServerConfigTest,test_daos_server_config_basic
        """
        # Setup the servers
        self.add_server_manager()
        self.configure_manager(
            "server", self.server_managers[0], self.hostlist_servers,
            self.hostfile_servers_slots)

        # Get the input to verify
        c_val = self.params.get("config_val", "/run/server_config_val/*/")

        if c_val[0] == "name":
            # Set the dmg system name to match the server in order to avoid
            # mismatch failures that aren't part of this test
            self.assertTrue(
                self.server_managers[-1].dmg.set_config_value(c_val[0], c_val[1]),
                "Error setting the '{}' config file parameter to '{}'".format(
                    c_val[0], c_val[1]))

        # Identify the attribute and modify its value to test value
        self.assertTrue(
            self.server_managers[0].set_config_value(c_val[0], c_val[1]),
            "Error setting the '{}' config file parameter to '{}'".format(
                c_val[0], c_val[1]))

        self.log.info(
            "Starting server with %s = %s, expected to %s",
            c_val[0], c_val[1], c_val[2])

        try:
            self.server_managers[0].start()
            exception = None
        except ServerFailed as err:
            exception = err

        # Verify
        fail_message = ""
        if c_val[2] == "FAIL" and exception is None:
            self.log.error("Server was expected to fail")
            fail_message = (
                "Server start completed successfully when it was expected to "
                "fail with {} = {}".format(c_val[0], c_val[1]))
        elif c_val[2] == "PASS" and exception is not None:
            self.log.error("Server was expected to start")
            fail_message = (
                "Server start failed when it was expected to complete "
                "successfully with {} = {}: {}".format(
                    c_val[0], c_val[1], exception))

        if fail_message:
            self.fail(fail_message)
        self.log.info("Test passed!")
