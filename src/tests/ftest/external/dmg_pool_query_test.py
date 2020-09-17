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

from ior_test_base import IorTestBase
from test_utils_pool import TestPool
from control_test_base import ControlTestBase
from general_utils import human_to_bytes


class DmgPoolQueryTest(ControlTestBase, IorTestBase):
    """Test Class Description:
    Simple test to verify the pool query command of dmg tool.
    :avocado: recursive
    """
    # pylint: disable=too-many-ancestors
    def setUp(self):
        "Set up for dmg pool query."
        super(DmgPoolQueryTest, self).setUp()

        # Init the pool
        self.pool = TestPool(self.context, dmg_command=self.dmg)
        self.pool.get_params(self)
        self.pool.create()
        self.uuid = self.pool.pool.get_uuid_str()

    def get_pool_query_info(self, uuid):
        """Get the information from the dmg pool query command."""
        self.log.info("==>   Running dmg pool query:")
        kwargs = {"pool": uuid}
        pool_query = self.get_dmg_output("pool_query", **kwargs)

        # Clean up empty string from each list. 'pool_query' should look like:
        # [['217afbae-534f-4d8f-9e49-74cbfb8ad155', '8', '0'],
        # ['8'],
        # ['2.0 GB', '2.0 GB', '250 MB', '250 MB', '250 MB'],
        # ['16 GB', '16 GB', '2.0 GB', '2.0 GB', '2.0 GB'],
        # ['0', '0']]
        pool_query_info = {}
        if pool_query:
            for idx, info in enumerate(pool_query):
                pool_query[idx] = [i for i in info if i]

            pool_query_info = {
                "pool_info": pool_query[0],
                "target_cnt": pool_query[1],
                "scm_info": pool_query[2],
                "nvme_info": pool_query[3],
                "rebuild_info": pool_query[4]
            }
        return pool_query_info

    def test_pool_query_basic(self):
        """
        JIRA ID: DAOS-2976

        Test Description: Test basic dmg functionality to query pool info on
        the system. Provided a valid pool UUID, verify the output received from
        pool query command.

        :avocado: tags=all,small,pr,hw,dmg,pool_query,basic,poolquerybasic
        """
        self.log.info("==>   Verify dmg output against expected output:")
        dmg_info = self.get_pool_query_info(self.uuid)
        exp_info = {
            "pool_info": self.params.get("pool_info", "/run/exp_vals/"),
            "target_cnt": self.params.get("target_cnt", "/run/exp_vals/"),
            "scm_info": self.params.get("scm_info", "/run/exp_vals/"),
            "nvme_info": self.params.get("nvme_info", "/run/exp_vals/"),
            "rebuild_info": self.params.get("rebuild_info", "/run/exp_vals/"),
        }

        # Add the expected uuid value
        exp_info["pool_info"].insert(0, self.uuid.upper())

        if exp_info != dmg_info:
            self.log.info("==>   Found difference in dmg output and expected.")
            self.fail("dmg pool-query: \n{} \nexpected: \n{}".format(
                dmg_info, exp_info))
        else:
            self.log.info("==>   Expect values found in dmg pool query output.")

    def test_pool_query_inputs(self):
        """
        JIRA ID: DAOS-2976

        Test Description: Test basic dmg functionality to query pool info on
        the system. Verify the inputs that can be provided to 'query --pool'
        argument of the dmg pool subcommand.

        :avocado: tags=all,small,pr,hw,dmg,pool_query,basic,poolqueryinputs
        """
        # Get test UUIDs
        errors_list = []
        uuids = self.params.get("uuids", '/run/pool_uuids/*')

        # Disable raising an exception if the dmg command fails
        self.dmg.exit_status_exception = False

        for uuid in uuids:
            self.log.info("\n==>   Using test UUID: %s", uuid[0])
            self.log.info("==>   Test is expected to finish with: %s", uuid[1])

            # Verify
            out = self.get_pool_query_info(uuid[0])
            if out:
                exception = None
            elif not out:
                exception = 1

            if uuid[1] == "FAIL" and exception is None:
                errors_list.append("==>   Test expected to fail:" + uuid[0])

        # Enable exceptions again for dmg.
        self.dmg.exit_status_exception = True

        # Report errors and fail test if needed.
        if errors_list:
            for err in errors_list:
                self.log.error("==>   Failure: %s", err)
            self.fail("Failed dmg pool query input test.")

    def test_pool_query_ior(self):
        """
        JIRA ID: DAOS-2976

        Test Description: Test that pool query command will properly and
        accurately show the size changes once there is content in the pool.

        :avocado: tags=all,small,pr,hw,dmg,pool_query,basic,poolquerywrite
        """
        # Store original pool info
        out_b = self.get_pool_query_info(self.uuid)
        self.log.info("==>   Pool info before write: \n%s", out_b)

        #  Run ior
        self.log.info("==>   Write data to pool.")
        self.run_ior_with_pool()

        # Check pool written data
        out_a = self.get_pool_query_info(self.uuid)
        self.log.info("==>   Pool info after write: \n%s", out_a)

        # The file should have been written into nvme, compare info
        bytes_orig_val = human_to_bytes(out_b["nvme_info"][1])
        bytes_curr_val = human_to_bytes(out_a["nvme_info"][1])
        if bytes_orig_val <= bytes_curr_val:
            self.fail("NVMe free space should be < {}".format(
                out_b["nvme_info"][1]))
