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

import re

from dmg_utils import pool_query
from apricot import TestWithServers
from test_utils import TestPool, check_pool_space


class DmgPoolQueryTest(TestWithServers):
    """Test Class Description:
    Simple test to verify the pool query command of dmg tool.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DmgPoolQueryTest object."""
        super(DmgPoolQueryTest, self).__init__(*args, **kwargs)
        self.pool = None

    def test_pool_query_basic(self):
        """
        JIRA ID: DAOS-2976
        Test Description: Test basic dmg functionality to query pool info on
        the system. Provided a valid pool UUID, verify the output received from
        pool query command.
        :avocado: tags=all,tiny,pr,hw,dmg,pool_query,basic
        """
        self.pool = TestPool(self.context)
        self.pool.get_params(self)
        self.pool.create()
        self.pool.connect(1 << 1)

        # Update hostlist value for dmg command
        port = self.params.get("port", "/run/server_config/*")
        host_p = ["{}:{}".format(host, port) for host in self.hostlist_servers]

        self.log.info("Running dmg pool query")
        dmg_out = pool_query(self.bin, host_p, self.pool.uuid)

        # Parse output
        d_info = {}
        d_info["dmg_t_cnt"] = re.findall(
            r"Target\(VOS\) count:(.+)", dmg_out.stdout)
        d_info["dmg_t_size"] = re.findall(r"Total size: (.+)", dmg_out.stdout)
        d_info["dmg_mem_info"] = re.findall(
            r"Free: (.+), min:(.+), max:(.+), mean:(.+)", dmg_out.stdout)
        d_info["dmg_r_info"] = re.findall(
            r"Rebuild (.+), (.+) objs, (.+) recs", dmg_out.stdout)

        print("d_info: {}".format(d_info))
        # Get data from API to verify dmg output.
        e_info = {}
        e_info["exp_t_cnt"] = None
        e_info["exp_t_size"] = None
        e_info["exp_mem_info"] = None
        e_info["exp_r_info"] = None

        # Verify
        for k, e, d in zip(e_info, e_info.values(), d_info.values()):
            if e != d:
                self.fail("dmg pool query expected output: {}:{}".format(k, e))

    def test_pool_query_inputs(self):
        """
        JIRA ID: DAOS-2976
        Test Description: Test basic dmg functionality to query pool info on
        the system. Verify the inputs that can be provided to 'query --pool'
        argument of the dmg pool subcommand.
        :avocado: tags=all,tiny,pr,hw,dmg,pool_query,basic
        """
        # Get test UUID
        exp_out = []
        uuid = self.params.get("uuid", '/run/pool_uuids/*/')
        exp_out.append(uuid[1])
        self.log.info("Using test UUID: %s", uuid)

        # Update hostlist value for dmg command
        port = self.params.get("port", "/run/server_config/*")
        host_p = ["{}:{}".format(host, port) for host in self.hostlist_servers]

        self.log.info("Running dmg pool query")
        dmg_out = pool_query(self.bin, host_p, uuid)

        # Verify
        self.log.info("Test expected to finish with: %s", exp_out[-1])
        if dmg_out.exit_status != exp_out[-1]:
            self.fail("Test failed, dmg pool query finished with: {}".format(
                dmg_out.exit_status))
        elif dmg_out is None:
            self.fail("Test failed, dmg command failed while executing.")

    def test_pool_query_pool_size(self):
        """
        JIRA ID: DAOS-2976
        Test Description: Test that pool query command will properly and
        accurately show the size changes once there is content in the pool.
        :avocado: tags=all,tiny,pr,hw,dmg,pool_query,basic
        """
        transfer_block_size = self.params.get("transfer_block_size",
                                              '/run/ior/iorflags/*')
