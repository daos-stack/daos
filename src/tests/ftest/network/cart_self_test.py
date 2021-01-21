#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from apricot import TestWithServers

from command_utils_base import \
    EnvironmentVariables, FormattedParameter, CommandFailure
from command_utils import ExecutableCommand
from job_manager_utils import Orterun
from general_utils import get_log_file


class SelfTest(ExecutableCommand):
    """Defines a CaRT self test command."""

    def __init__(self, path=""):
        """Create a SelfTest object.

        Uses Avocado's utils.process module to run self_test with parameters.

        Args:
            path (str, optional): path to location of command binary file.
                Defaults to "".
        """
        super(SelfTest, self).__init__("/run/self_test/*", "self_test", path)

        self.group_name = FormattedParameter("--group-name {}")
        self.endpoint = FormattedParameter("--endpoint {0}")
        self.message_sizes = FormattedParameter("--message-sizes {0}")
        self.max_inflight_rpcs = FormattedParameter("--max-inflight-rpcs {0}")
        self.repetitions = FormattedParameter("--repetitions {0}")
        self.attach_info = FormattedParameter("--path {0}")


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
        share_addr = self.params.get("share_addr", "/run/test_params/*")

        # Configure the daos server
        config_file = self.get_config_file(self.server_group, "server")
        self.add_server_manager(config_file)
        self.configure_manager(
            "server",
            self.server_managers[-1],
            self.hostlist_servers,
            self.hostfile_servers_slots,
            self.hostlist_servers)
        self.assertTrue(
            self.server_managers[-1].set_config_value(
                "crt_ctx_share_addr", share_addr),
            "Error updating daos_server 'crt_ctx_share_addr' config setting")

        # Setup additional environment variables for the server orterun command
        self.cart_env["CRT_CTX_SHARE_ADDR"] = str(share_addr)
        self.cart_env["CRT_CTX_NUM"] = "8"
        self.cart_env["CRT_PHY_ADDR_STR"] = \
            self.server_managers[0].get_config_value("provider")
        self.cart_env["OFI_INTERFACE"] = \
            self.server_managers[0].get_config_value("fabric_iface")
        self.server_managers[0].manager.assign_environment(self.cart_env, True)

        # Start the daos server
        self.start_server_managers()

        # Generate a uri file using daos_agent dump-attachinfo
        attachinfo_file = "{}.attach_info_tmp".format(self.server_group)
        self.uri_file = get_log_file(attachinfo_file)
        agent_cmd = self.agent_managers[0].manager.job
        agent_cmd.dump_attachinfo(self.uri_file)

    def test_self_test(self):
        """Run a few CaRT self-test scenarios.

        :avocado: tags=all,pr,daily_regression,smoke,unittest,tiny,cartselftest
        """
        # Setup the orterun command
        orterun = Orterun(SelfTest(self.bin))
        orterun.map_by.update(None, "orterun/map_by")
        orterun.enable_recovery.update(False, "orterun/enable_recovery")

        # Get the self_test command line parameters
        orterun.job.get_params(self)
        orterun.job.group_name.update(self.server_group, "group_name")
        orterun.job.attach_info.update(
            os.path.dirname(self.uri_file), "attach_info")

        # Setup the environment variables for the self_test orterun command
        orterun.assign_environment(self.cart_env)

        # Run the test
        try:
            orterun.run()
        except CommandFailure as error:
            self.test_log.info(
                "CaRT self_test returned non-zero: %s", str(error))
            self.fail("CaRT self_test returned non-zero")
