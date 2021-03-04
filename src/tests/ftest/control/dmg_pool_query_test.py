#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from __future__ import print_function

from apricot import skipForTicket
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
        """Set up for dmg pool query."""
        super(DmgPoolQueryTest, self).setUp()

        # Init the pool
        self.pool = TestPool(self.context, self.dmg)
        self.pool.get_params(self)
        self.pool.create()
        self.uuid = self.pool.pool.get_uuid_str()

    def get_pool_query_info(self, uuid):
        """Get the information from the dmg pool query command.

        Args:
            uuid (str): UUID of the pool for which to collect information

        Returns:
            dict: the pool information stored in a dictionary

        """
        self.log.info("==>   Running dmg pool query:")
        return self.dmg.pool_query(uuid)

    @skipForTicket("DAOS-6452")
    def test_pool_query_basic(self):
        """
        JIRA ID: DAOS-2976

        Test Description: Test basic dmg functionality to query pool info on
        the system. Provided a valid pool UUID, verify the output received from
        pool query command.

        :avocado: tags=all,small,daily_regression,hw,dmg,pool_query,basic
        :avocado: tags=poolquerybasic
        """
        self.log.info("==>   Verify dmg output against expected output:")
        dmg_info = self.get_pool_query_info(self.uuid)

        # Get the expected pool query values from the test yaml.  This should be
        # as simple as:
        #   exp_info = self.params.get("exp_vals", path="/run/*", default={})
        # but this yields an empty dictionary (the default), so it needs to be
        # defined manually:
        exp_info = {
            "uuid": self.uuid.upper(),
            "ntarget": self.params.get("ntarget", path="/run/exp_vals/*"),
            "disabled": self.params.get("disabled", path="/run/exp_vals/*"),
            "leader": self.params.get("leader", path="/run/exp_vals/*"),
            "version": self.params.get("version", path="/run/exp_vals/*"),
            "target_count": self.params.get(
                "target_count", path="/run/exp_vals/*"),
            "scm": {
                "total": self.params.get("total", path="/run/exp_vals/scm/*"),
                "free": self.params.get("free", path="/run/exp_vals/scm/*"),
                "free_min": self.params.get(
                    "free_min", path="/run/exp_vals/scm/*"),
                "free_max": self.params.get(
                    "free_max", path="/run/exp_vals/scm/*"),
                "free_mean": self.params.get(
                    "free_mean", path="/run/exp_vals/scm/*"),
            },
            "nvme": {
                "total": self.params.get("total", path="/run/exp_vals/nvme/*"),
                "free": self.params.get("free", path="/run/exp_vals/nvme/*"),
                "free_min": self.params.get(
                    "free_min", path="/run/exp_vals/nvme/*"),
                "free_max": self.params.get(
                    "free_max", path="/run/exp_vals/nvme/*"),
                "free_mean": self.params.get(
                    "free_mean", path="/run/exp_vals/nvme/*"),
            },
            "rebuild": {
                "status": self.params.get(
                    "status", path="/run/exp_vals/rebuild/*"),
                "objects": self.params.get(
                    "objects", path="/run/exp_vals/rebuild/*"),
                "records": self.params.get(
                    "records", path="/run/exp_vals/rebuild/*"),
            }
        }

        self.assertDictEqual(
            dmg_info, exp_info,
            "Found difference in dmg pool query output and the expected values")

        self.log.info("All expect values found in dmg pool query output.")

    def test_pool_query_inputs(self):
        """
        JIRA ID: DAOS-2976

        Test Description: Test basic dmg functionality to query pool info on
        the system. Verify the inputs that can be provided to 'query --pool'
        argument of the dmg pool subcommand.

        :avocado: tags=all,small,daily_regression,hw,dmg,pool_query,basic
        :avocado: tags=poolqueryinputs
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

        :avocado: tags=all,small,daily_regression,hw,dmg,pool_query,basic
        :avocado: tags=poolquerywrite
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
        bytes_orig_val = human_to_bytes(out_b["nvme"]["free"])
        bytes_curr_val = human_to_bytes(out_a["nvme"]["free"])
        if bytes_orig_val <= bytes_curr_val:
            self.fail("NVMe free space should be < {}".format(
                out_b["nvme_info"][1]))
