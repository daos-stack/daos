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

from dmg_utils import pool_query, get_pool_query_info
from ior_test_base import IorTestBase
from test_utils_pool import TestPool


class DmgPoolQueryTest(IorTestBase):
    """Test Class Description:
    Simple test to verify the pool query command of dmg tool.
    :avocado: recursive
    """
    # pylint: disable=too-many-ancestors
    def setUp(self):
        "Set up for dmg pool query."
        super(DmgPoolQueryTest, self).setUp()

        # Init the pool
        self.pool = TestPool(self.context, dmg_command=self.get_dmg_command())
        self.pool.get_params(self)
        self.pool.create()

        # Get the host list with port to provide the dmg command
        self.port = self.params.get("port", "/run/server_config/*")
        self.host_p = ["{}:{}".format(host, self.port)
                       for host in self.hostlist_servers]

    def test_pool_query_basic(self):
        """
        JIRA ID: DAOS-2976
        Test Description: Test basic dmg functionality to query pool info on
        the system. Provided a valid pool UUID, verify the output received from
        pool query command.
        :avocado: tags=all,tiny,pr,hw,dmg,pool_query,basic,poolquerybasic
        """
        self.log.info("Running dmg pool query")
        dmg_out = pool_query(self.bin, self.host_p, self.pool.pool.uuid)

        # Parse output
        d_info = get_pool_query_info(dmg_out.stdout)
        self.log.info("dmg values found: %s", d_info)

        e_info = self.params.get("exp_vals", "/run/*")
        self.log.info("Expected values are: %s", e_info)

        # Verify
        if d_info != e_info:
            self.fail("dmg pool query expected output: {}".format(e_info))

    def test_pool_query_inputs(self):
        """
        JIRA ID: DAOS-2976
        Test Description: Test basic dmg functionality to query pool info on
        the system. Verify the inputs that can be provided to 'query --pool'
        argument of the dmg pool subcommand.
        :avocado: tags=all,tiny,pr,hw,dmg,pool_query,basic,poolqueryinputs
        """
        # Get test UUID
        uuid = self.params.get("uuid", '/run/pool_uuids/*/')
        self.log.info("Using test UUID: %s", uuid[0])

        self.log.info("Running dmg pool query")
        dmg_out = pool_query(self.bin, self.host_p, uuid[0])

        # Verify
        self.log.info("Test is expected to finish with: %s", uuid[1])
        if dmg_out is None:
            exception = 2
        else:
            if dmg_out.exit_status == 0:
                exception = None
            else:
                exception = 1

        if uuid[1] == "FAIL" and exception is None:
            self.log.error("Command was expected to fail")
            self.fail("Test failed, dmg pool query command output: {}".format(
                dmg_out.stdout))
        elif uuid[1] == "PASS" and exception is not None:
            self.log.error("Command was expected to pass")
            if exception == 2:
                self.fail("Test failed, dmg command failed while executing.")
            if exception == 1:
                self.fail("Test failed, dmg command failed with {}".format(
                    dmg_out.stdout))

    def test_pool_query_ior(self):
        """
        JIRA ID: DAOS-2976
        Test Description: Test that pool query command will properly and
        accurately show the size changes once there is content in the pool.
        :avocado: tags=all,tiny,pr,hw,dmg,pool_query,basic,poolqueryior
        """
        # Store orignal pool info and run ior
        self.log.info("Getting pool info before writting data")
        out_before_ior = pool_query(self.bin, self.host_p, self.pool.pool.uuid)
        self.run_ior_with_pool()

        # Check pool written data
        self.log.info("Getting pool info after ior run")
        out_after_ior = pool_query(self.bin, self.host_p, self.pool.pool.uuid)

        # Parse output of dmg command before running ior
        orig_pool_info = get_pool_query_info(out_before_ior.stdout)
        self.log.info("dmg values found: %s", orig_pool_info)

        # Parse output of dmg command after running ior
        curr_pool_info = get_pool_query_info(out_after_ior.stdout)
        self.log.info("dmg values found: %s", curr_pool_info)

        # Compare info
        for mem in ["s_free", "n_free"]:
            orig_value, orig_unit = parse_space_value(orig_pool_info[mem])
            curr_value, curr_unit = parse_space_value(curr_pool_info[mem])
            if orig_value <= curr_value:
                self.fail("Free space should be less than: {}".format(
                    orig_value))
            elif orig_unit != curr_unit:
                self.fail("Free space unit don't seem right: {}".format(
                    orig_unit))


def parse_space_value(space_str):
    """ Parse a string and return the value and unit individually.

    Args:
        space_str (str): string representing space information. i.e. 3GB

    Returns:
        value, unit (int, str): returns to the user the values parsed.

    """
    value, unit = ""
    for i in space_str:
        if i.isdigit():
            value += i
        elif i.isalpha():
            unit += i
    return int(value), unit
