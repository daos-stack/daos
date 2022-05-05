#!/usr/bin/python
"""
  (C) Copyright 2019-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from rebuild_test_base import RebuildTestBase
from general_utils import DaosTestError
from daos_utils import DaosCommand
import re


class ContRedundancyFactor(RebuildTestBase):
    # pylint: disable=too-many-ancestors
    """Test cascading failures during rebuild.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a CascadingFailures object."""
        super().__init__(*args, **kwargs)
        self.mode = None
        self.daos_cmd = None

    def create_test_container(self):
        """Create a container and write objects."""
        self.log.info(
            "==>(1)Create pool and container with redundant factor,"
            " start background IO object write")
        self.container.create()
        self.container.write_objects(
            self.inputs.rank.value[0], self.inputs.object_class.value)

    def verify_rank_has_objects(self):
        """Verify the first rank to be excluded has at least one object."""
        rank_list = self.container.get_target_rank_lists(" before rebuild")
        objects = {
            rank: self.container.get_target_rank_count(rank, rank_list)
            for rank in self.inputs.rank.value
        }
        self.assertGreater(
            objects[self.inputs.rank.value[0]], 0,
            "#No objects written to rank {}".format(self.inputs.rank.value[0]))

    def verify_rank_has_no_objects(self):
        """Verify the excluded rank has zero objects."""
        rank_list = self.container.get_target_rank_lists(" after rebuild")
        objects = {
            rank: self.container.get_target_rank_count(rank, rank_list)
            for rank in self.inputs.rank.value
        }
        for rank in self.inputs.rank.value:
            self.assertEqual(
                objects[rank], 0,
                "#Excluded rank {} still has objects".format(rank))

    def verify_cont_rf_healthstatus(self, expected_rf, expected_health):
        """Verify the container redundancy factor and health status.

        Args:
            expected_rf (str): expected container redundancy factor.
            expect_cont_status (str): expected container health status.
        """
        actual_rf = None
        actual_health = None

        cont_props = self.daos_cmd.container_get_prop(
            pool=self.pool.uuid, cont=self.container.uuid, properties=["rf", "status"])
        for cont_prop in cont_props["response"]:
            if cont_prop["name"] == "rf":
                actual_rf = cont_prop["value"]
            elif cont_prop["name"] == "status":
                actual_health = cont_prop["value"]

        self.assertEqual(
            actual_rf, expected_rf,
            "#Container redundancy factor mismatch, actual: {},"
            " expected: {}.".format(actual_rf, expected_rf))
        self.assertEqual(
            actual_health, expected_health,
            "#Container health-status mismatch, actual: {},"
            " expected: {}.".format(actual_health, expected_health))

    def start_rebuild_cont_rf(self, rf):
        """Start the rebuild process and check for container properties.

        Args:
            rf (str): container redundancy factor.
        """
        self.log.info(
            "==>(2)Check for container rf and health-status "
            "before rebuild: HEALTHY")
        self.verify_cont_rf_healthstatus(rf, "HEALTHY")

        # Exclude the ranks from the pool to initiate rebuild simultaneously
        self.log.info(
            "==>(3)Start rebuild for all specified ranks simultaneously")
        self.server_managers[0].stop_ranks(
            self.inputs.rank.value, self.d_log)

    def execute_during_rebuild_cont_rf(self, rf, expect_cont_status="HEALTHY"):
        """Execute test steps during rebuild.

        Args:
            rf (str): container redundancy factor.
            expect_cont_status (str, optional):
                expected container health status.
        """
        # Wait for rebuild to start and check for container status
        self.pool.wait_for_rebuild(True, 1)
        self.log.info(
            "==>(4)Check for container rf and health-status "
            "after ranks rebuild started: %s", expect_cont_status)
        self.verify_cont_rf_healthstatus(rf, expect_cont_status)
        # Wait for rebuild completion and check for container status
        self.pool.wait_for_rebuild(False, 1)
        self.log.info(
            "==>(5)Check for container rf and health-status "
            "after rebuild completed: %s", expect_cont_status)
        self.verify_cont_rf_healthstatus(rf, expect_cont_status)

    def create_test_container_and_write_obj(self, negative_test=False):
        """Create a container and write objects
           for positive testcase, enable the exception with write objects
           for negative testcase, disable the exception with write objects
           and expecting failure with RC: -1003.

        Args:
            negative_test (Bool, optional): container write_objects to handle
                test Exception and parse for return error code.
        """
        der_inval = "RC: -1003"
        self.log.info(
            "==>(1)Create pool and container with redundant factor,"
            " start background IO object write")
        self.container.create()
        if not negative_test:
            self.container.write_objects(
                self.inputs.rank.value[0], self.inputs.object_class.value)
        else:
            try:
                self.container.write_objects_wo_failon(
                    self.inputs.rank.value[0], self.inputs.object_class.value)
                self.fail("#Container redundancy factor with an invallid "
                           "object_class traffic passed, expecting Fail")
            except DaosTestError as error:
                self.log.info(error)
                if der_inval in str(error):
                    self.log.info("==Negative test, expecting DaosApiError "
                                  "RC: -1003 found.")
                else:
                    self.fail("#Negative test, container redundancy factor "
                              "test failed, return error RC: -1003 not found")

    def execute_cont_rf_test(self, create_container=True):
        """Execute the rebuild test steps for container rf test.

        Args:
            create_container (bool, optional): should the test create a
                container. Defaults to True.
        """

        # Get the test params and var
        self.setup_test_pool()
        self.daos_cmd = DaosCommand(self.bin)
        if create_container:
            self.setup_test_container()
        oclass = self.inputs.object_class.value
        negative_test = True
        rf = ''.join(self.container.properties.value.split(":"))
        rf_num = int(re.search(r"rf([0-9]+)", rf).group(1))
        if "OC_SX" in oclass and rf_num < 1:
            negative_test = False
        elif ("OC_RP_2" in oclass and rf_num < 2) or (
            "OC_RP_3" in oclass and rf_num < 3):
            negative_test = False
        # Create a pool and verify the pool information before rebuild
        self.create_test_pool()
        # Create a container and write objects
        self.create_test_container_and_write_obj(negative_test)
        if self.mode == "cont_rf_with_rebuild":
            num_of_ranks = len(self.inputs.rank.value)
            if num_of_ranks > rf_num:
                expect_cont_status = "UNCLEAN"
            else:
                expect_cont_status = "HEALTHY"
            # Verify the rank to be excluded has at least one object
            self.verify_rank_has_objects()
            # Start the rebuild process
            self.start_rebuild_cont_rf(rf)
            # Execute the test steps during rebuild
            self.execute_during_rebuild_cont_rf(rf, expect_cont_status)
            # Refresh local pool and container
            self.log.info(
                "==>(6)Check for pool and container info after rebuild.")
            self.pool.check_pool_info()
            self.container.check_container_info()
            # Verify the excluded rank is no longer used with the objects
            self.verify_rank_has_no_objects()
            # Verify the pool information after rebuild
            if expect_cont_status == "HEALTHY":
                self.update_pool_verify()
                self.execute_pool_verify(" after rebuild")
                self.log.info(
                    "==>(7)Check for container data if the container"
                    " is healthy.")
                self.verify_container_data()
            self.log.info("Test passed")
        elif self.mode == "cont_rf_enforcement":
            self.log.info("Container rf test passed")
        else:
            self.fail("#Unsupported container_rf test mode")
