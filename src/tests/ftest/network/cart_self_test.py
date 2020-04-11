#!/usr/bin/python
"""
  (C) Copyright 2018-2019 Intel Corporation.

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
import os

from apricot import TestWithServers

from command_utils_base import \
    EnvironmentVariables, FormattedParameter, CommandFailure
from command_utils import ExecutableCommand
from job_manager_utils import Orterun


class SelfTest(ExecutableCommand):
    """Defines a CaRT self test command."""

    def __init__(self, path=""):
        """Create a SelfTest object.

        Uses Avocado's utils.process module to run self_test with parameters.

        Args:
            namespace (str): yaml namespace (path to parameters)
            command (str): string of the command to be executed.
            path (str, optional): path to location of command binary file.
                Defaults to "".
            subprocess (bool, optional): whether the command is run as a
                subprocess. Defaults to False.
        """
        super(SelfTest, self).__init__("/run/self_test/*", "self_test", path)

        self.group_name = FormattedParameter("--group-name {}")
        self.endpoint = FormattedParameter("--endpoint {0}")
        self.message_sizes = FormattedParameter("--message-sizes {0}")
        self.max_inflight_rpcs = FormattedParameter("--max-inflight-rpcs {0}")
        self.repetitions = FormattedParameter("--repetitions {0}")


class CartSelfTest(TestWithServers):
    """Runs a few variations of CaRT self-test.

    Ensures network is in a stable state prior to testing.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a CartSelfTest object."""
        super(CartSelfTest, self).__init__(*args, **kwargs)
        self.setup_start_servers = False
        self.uri_file = None
        self.cart_env = EnvironmentVariables()

    def setUp(self):
        """Set up each test case."""
        super(CartSelfTest, self).setUp()

        # Configure the daos server
        config_file = self.get_config_file(self.server_group, "server")
        self.add_server_manager(config_file)
        self.configure_manager(
            "server",
            self.server_managers[-1],
            self.hostlist_servers,
            self.hostfile_servers_slots,
            self.hostlist_servers)

        # Configure the daos server to use a uri file - if supported by the
        # daos_server job manager
        if hasattr(self.server_managers[0].manager, "report_uri"):
            self.uri_file = os.path.join(self.tmp, "uri.txt")
            self.server_managers[0].manager.report_uri.value = self.uri_file

        # Setup additional environment variables for the server orterun command
        share_addr = self.params.get("share_addr", "/run/test/*")
        self.cart_env["CRT_CTX_SHARE_ADDR"] = str(share_addr)
        self.cart_env["CRT_CTX_NUM"] = "8"
        self.cart_env["CRT_PHY_ADDR_STR"] = \
            self.server_managers[0].get_config_value("provider")
        self.cart_env["OFI_INTERFACE"] = \
            self.server_managers[0].get_config_value("fabric_iface")
        self.server_managers[0].manager.assign_environment(self.cart_env, True)

        # Start the daos server
        self.start_server_managers()

    def test_self_test(self):
        """Run a few CaRT self-test scenarios.

        :avocado: tags=all,smoke,unittest,tiny,cartselftest
        """
        # Setup the orterun command
        orterun = Orterun(SelfTest(self.cart_bin))
        orterun.ompi_server.update(
            "file:{}".format(self.uri_file), "orterun/ompi_server")
        orterun.map_by.update(None, "orterun/map_by")
        orterun.enable_recovery.update(False, "orterun/enable_recovery")

        # Get the self_test command line parameters
        orterun.job.get_params(self)
        orterun.job.group_name.value = self.server_group

        # Setup the environment variables for the self_test orterun command
        orterun.assign_environment(self.cart_env)

        # Run the test
        try:
            orterun.run()
        except CommandFailure as error:
            self.test_log.info(
                "CaRT self_test returned non-zero: %s", str(error))
            self.fail("CaRT self_test returned non-zero")
