"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from agent_utils import include_local_host
from exception_utils import CommandFailure


class DaosAgentConfigTest(TestWithServers):
    """Test Class Description:
    Simple test to verify that the daos_agent starts/stops properly given
    positive and negative values to its configuration file.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DaosAgentConfigTest object."""
        super().__init__(*args, **kwargs)
        self.start_agents_once = False
        self.start_servers_once = False
        self.setup_start_agents = False
        self.setup_start_servers = False

    def test_daos_agent_config_basic(self):
        """
        JIRA ID: DAOS-1508

        Test Description: Test daos_agent start/stops properly.
        on the system.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,basic
        :avocado: tags=DaosAgentConfigTest,test_daos_agent_config_basic
        """
        # Setup the agents
        self.add_agent_manager()
        self.configure_manager(
            "agent",
            self.agent_managers[-1],
            include_local_host(self.hostlist_clients),
            self.hostfile_clients_slots)

        # Get the input to verify
        c_val = self.params.get("config_val", "/run/agent_config_val/*/")

        # Identify the attribute and modify its value to test value
        self.assertTrue(
            self.agent_managers[-1].set_config_value(c_val[0], c_val[1]),
            "Error setting the '{}' config file parameter to '{}'".format(
                c_val[0], c_val[1]))

        # Setup the access points with the server hosts
        self.log.info(
            "Starting agent with %s = %s, expecting it to %s",
            c_val[0], c_val[1], c_val[2])

        try:
            self.agent_managers[-1].start()
            exception = None
        except CommandFailure as err:
            exception = err

        # Verify
        if c_val[2] == "FAIL" and exception is None:
            self.log.error("Agent was expected to fail")
            self.fail(
                "Starting agent completed successfully when it was expected to "
                "fail with {} = {}".format(c_val[0], c_val[1]))
        elif c_val[2] == "PASS" and exception is not None:
            self.log.error("Agent was expected to start")
            self.fail(
                "Starting agent failed when it was expected to complete "
                "successfully with {} = {}: {}".format(
                    c_val[0], c_val[1], exception))

        self.log.info(
            "Test passed - starting the agent with %s = %s %sed",
            c_val[0], c_val[1], c_val[2].lower())
