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

from apricot import TestWithServers
from command_utils_base import CommandFailure


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

        :avocado: tags=all,small,control,daily_regression,control_start,basic
        """
        # Get the input to verify
        c_val = self.params.get("config_val", "/run/control_config_val/*/")

        # Identify the attribute and modify its value to test value
        self.assertTrue(
            self.server_managers[-1].dmg.set_config_value(c_val[0], c_val[1]),
            "Error setting the '{}' config file parameter to '{}'".format(
                c_val[0], c_val[1]))

        # Setup the access points with the server hosts
        self.log.info("Executing dmg config with %s = %s", c_val[0], c_val[1])

        try:
            self.server_managers[-1].dmg.storage_scan()
            exception = None
        except CommandFailure as err:
            exception = err

        # Verify
        if c_val[2] == "FAIL" and exception is None:
            self.log.error("dmg was expected to fail")
            self.fail("{}".format(exception))
        elif c_val[2] == "PASS" and exception is not None:
            self.log.error("dmg was expected to start")
            self.fail("{}".format(exception))
