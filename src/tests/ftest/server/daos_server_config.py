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

from apricot import TestWithServers
from server_utils import ServerManager, ServerFailed


class DaosServerConfigTest(TestWithServers):
    """The daos_server configuration file test cases.

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

        Test Description:
            Test daos_server start/stops properly on the system.

        :avocado: tags=all,tiny,control,pr,server_start,basic
        """
        # Setup the servers
        self.server_managers.append(ServerManager(self.bin))
        self.server_managers[-1].get_params(self)
        self.server_managers[-1].hosts = (
            self.hostlist_servers, self.workdir, self.hostfile_servers_slots)

        # Get the input to verify
        c_val = self.params.get("config_val", "/run/server_config_val/*/")

        # Identify the attribute and modify its value to test value
        yml_params = self.server_managers[-1].runner.job.yaml_params
        if c_val[3] == "gen":
            getattr(yml_params, c_val[0]).value = c_val[1]
        elif c_val[3] == "server":
            getattr(yml_params.server_params[-1], c_val[0]).value = c_val[1]

        self.log.info("Starting server changing %s with %s", c_val[0], c_val[1])
        yamlfile = os.path.join(self.tmp, "daos_avocado_test.yaml")
        try:
            self.server_managers[-1].start(yamlfile)
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
