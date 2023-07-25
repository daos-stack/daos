"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""


from apricot import TestWithServers
from exception_utils import CommandFailure


class DaosControlConfigTest(TestWithServers):
    """Test Class Description:
    Simple test to verify dmg execution given positive and negative values
    to its configuration file.
    :avocado: recursive
    """

    def test_daos_control_config_basic(self):
        """
        JIRA ID: DAOS-1508

        Test Description: Test dmg tool executes with variant positive and
        negative inputs to its configuration file.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,basic
        :avocado: tags=DaosControlConfigTest,test_daos_control_config_basic
        """
        # Get the input to verify
        c_val = self.params.get("config_val", "/run/control_config_val/*/")

        # Save off the current dmg config value to restore later
        restore = self.server_managers[-1].dmg.get_config_value(c_val[0])
        self.assertIsNotNone(
            restore,
            "Error obtaining current {} dmg config value".format(c_val[0]))

        # Identify the attribute and modify its value to test value
        self.assertTrue(
            self.server_managers[-1].dmg.set_config_value(c_val[0], c_val[1]),
            "Error setting the '{}' config file parameter to '{}'".format(
                c_val[0], c_val[1]))

        # Setup the access points with the server hosts
        self.log.info(
            "Executing dmg config with %s = %s, expecting to %s",
            c_val[0], c_val[1], c_val[2])

        try:
            self.server_managers[-1].dmg.storage_scan()
            exception = None
        except CommandFailure as err:
            exception = err

        # Verify
        fail_message = ""
        if c_val[2] == "FAIL" and exception is None:
            self.log.error("dmg was expected to fail")
            fail_message = (
                "Dmg command completed successfully when it was expected to "
                "fail with {} = {}".format(c_val[0], c_val[1]))
        elif c_val[2] == "PASS" and exception is not None:
            self.log.error("dmg was expected to start")
            fail_message = (
                "Dmg command failed when it was expected to complete "
                "successfully with {} = {}: {}".format(
                    c_val[0], c_val[1], exception))

        # Restore the modified dmg config value
        self.assertTrue(
            self.server_managers[-1].dmg.set_config_value(c_val[0], restore),
            "Error restoring the '{}' config file parameter to '{}'".format(
                c_val[0], restore))

        if fail_message:
            self.fail(fail_message)
        self.log.info("Test passed!")
