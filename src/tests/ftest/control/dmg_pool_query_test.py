#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""


from ior_test_base import IorTestBase
from test_utils_pool import TestPool
from control_test_base import ControlTestBase


class DmgPoolQueryTest(ControlTestBase, IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test dmg query command.

    Test Class Description:
        Simple test to verify the pool query command of dmg tool.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for dmg pool query."""
        super().setUp()

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

    def test_pool_query_basic(self):
        """
        JIRA ID: DAOS-2976

        Test Description: Test basic dmg functionality to query pool info on
        the system. Provided a valid pool UUID, verify the output received from
        pool query command.

        :avocado: tags=all,daily_regression
        :avocado: tags=small,hw
        :avocado: tags=dmg,pool_query,basic
        :avocado: tags=pool_query_basic
        """
        self.log.info("==>   Verify dmg output against expected output:")
        dmg_info = self.get_pool_query_info(self.uuid)
        # We won't be testing free, min, max, and mean because the values
        # fluctuate across test runs. In addition, they're related to object
        # placement and testing them wouldn't be straightforward, so we'll need
        # some separate test cases.
        del dmg_info["response"]["scm"]["free"]
        del dmg_info["response"]["scm"]["min"]
        del dmg_info["response"]["scm"]["max"]
        del dmg_info["response"]["scm"]["mean"]
        del dmg_info["response"]["nvme"]["free"]
        del dmg_info["response"]["nvme"]["min"]
        del dmg_info["response"]["nvme"]["max"]
        del dmg_info["response"]["nvme"]["mean"]

        # Get the expected pool query values from the test yaml.  This should be
        # as simple as:
        #   exp_info = self.params.get("exp_vals", path="/run/*", default={})
        # but this yields an empty dictionary (the default), so it needs to be
        # defined manually:
        exp_info = {
            "status": self.params.get("pool_status", path="/run/exp_vals/*"),
            "uuid": self.uuid.upper(),
            "total_targets": self.params.get(
                "total_targets", path="/run/exp_vals/*"),
            "active_targets": self.params.get(
                "active_targets", path="/run/exp_vals/*"),
            "total_nodes": self.params.get(
                "total_nodes", path="/run/exp_vals/*"),
            "disabled_targets": self.params.get(
                "disabled_targets", path="/run/exp_vals/*"),
            "version": self.params.get("version", path="/run/exp_vals/*"),
            "leader": self.params.get("leader", path="/run/exp_vals/*"),
            "scm": {
                "total": self.params.get("total", path="/run/exp_vals/scm/*")
            },
            "nvme": {
                "total": self.params.get("total", path="/run/exp_vals/nvme/*")
            },
            "rebuild": {
                "status": self.params.get(
                    "rebuild_status", path="/run/exp_vals/rebuild/*"),
                "state": self.params.get(
                    "state", path="/run/exp_vals/rebuild/*"),
                "objects": self.params.get(
                    "objects", path="/run/exp_vals/rebuild/*"),
                "records": self.params.get(
                    "records", path="/run/exp_vals/rebuild/*")
            }
        }

        self.assertDictEqual(
            dmg_info["response"], exp_info,
            "Found difference in dmg pool query output and the expected values")

        self.log.info("All expect values found in dmg pool query output.")

    def test_pool_query_inputs(self):
        """
        JIRA ID: DAOS-2976

        Test Description: Test basic dmg functionality to query pool info on
        the system. Verify the inputs that can be provided to 'query --pool'
        argument of the dmg pool subcommand.

        :avocado: tags=all,daily_regression
        :avocado: tags=small,hw
        :avocado: tags=dmg,pool_query,basic
        :avocado: tags=pool_query_inputs
        """
        # Get test UUIDs
        errors_list = []
        uuids = self.params.get("uuids", '/run/pool_uuids/*')

        # Add a pass case to verify test is working
        uuids.append([self.uuid, "PASS"])

        # Disable raising an exception if the dmg command fails
        self.dmg.exit_status_exception = False

        for uuid in uuids:
            # Verify pool query status
            data = self.get_pool_query_info(uuid[0])
            error = data["error"] if "error" in data else None

            self.log.info("")
            self.log.info("==>  Using test UUID:                   %s", uuid[0])
            self.log.info("==>  Pool query command is expected to: %s", uuid[1])
            self.log.info("==>  Error from dmp pool query:         %s", error)
            self.log.info("")

            if uuid[1] == "FAIL" and error is None:
                errors_list.append("==>   Test expected to fail:" + uuid[0])
            elif uuid[1] == "PASS" and error is not None:
                errors_list.append("==>   Test expected to pass:" + uuid[0])

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

        :avocado: tags=all,daily_regression
        :avocado: tags=small,hw
        :avocado: tags=dmg,pool_query,basic
        :avocado: tags=pool_query_write
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
        bytes_orig_val = int(out_b["response"]["nvme"]["free"])
        bytes_curr_val = int(out_a["response"]["nvme"]["free"])
        if bytes_orig_val <= bytes_curr_val:
            self.fail(
                "Current NVMe free space should be smaller than {}".format(
                    out_b["response"]["nvme"]["free"]))
