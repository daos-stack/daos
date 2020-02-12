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
from __future__ import print_function

import os

from command_daos_utils import CommonConfig
from agent_utils import (DaosAgentYamlParameters, DaosAgentCommand,
                         DaosAgentManager, DaosAgentTransportCredentials,
                         include_local_host)
from apricot import TestWithServers
from server_utils import ServerManager, ServerFailed


class DaosAgentConfigTest(TestWithServers):
    """Test Class Description:
    Simple test to verify that the daos_agent starts/stops properly given
    positive and negative values to its configuration file.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DaosAgentConfigTest object."""
        super(DaosAgentConfigTest, self).__init__(*args, **kwargs)
        self.setup_start_agents = False

    def test_daos_server_config_basic(self):
        """
        JIRA ID: DAOS-1508
        Test Description: Test daos_agent start/stops properly.
        on the system.
        :avocado: tags=all,tiny,control,pr,agent_start,basic
        """
        # Setup the agents
        transport = DaosAgentTransportCredentials()

        # Use the unique agent group name to create a unique yaml file
        config_file = self.get_config_file(self.server_group, "agent")

        # Get the input to verify
        c_val = self.params.get("config_val", "/run/agent_config_val/*/")

        # Setup the access points with the server hosts
        common_cfg = CommonConfig(self.server_group, transport)
        self.add_agent_manager(config_file, common_cfg)
        self.configure_manager(
            "agent",
            self.agent_managers[-1],
            self.hostlist_clients,
            self.hostfile_clients_slots,
            self.hostlist_servers)

        self.log.info("Starting server changing %s with %s", c_val[0], c_val[1])
        self.start_agent_managers()

        # Verify
        if c_val[2] == "FAIL" and exception is None:
            self.log.error("Server was expected to fail")
            self.fail("{}".format(exception))
        elif c_val[2] == "PASS" and exception is not None:
            self.log.error("Server was expected to start")
            self.fail("{}".format(exception))
