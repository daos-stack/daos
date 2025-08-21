"""
  (C) Copyright 2019-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import re

from general_utils import DaosTestError
from oclass_utils import extract_redundancy_factor
from rebuild_test_base import RebuildTestBase


class ContRedundancyFactor(RebuildTestBase):
    """Test cascading failures during rebuild.

    :avocado: recursive
    """
    def verify_rank_has_objects(self, container, ranks):
        """Verify the first rank to be excluded has at least one object.

        Args:
            container (TestContainer): container to verify
            ranks (int/list): single rank or list of ranks to verify

        """
        if not isinstance(ranks, list):
            ranks = [ranks]
        rank_list = container.get_target_rank_lists(" before rebuild")
        objects = {
            rank: container.get_target_rank_count(rank, rank_list)
            for rank in ranks
        }
        self.assertGreater(
            objects[ranks[0]], 0,
            "#No objects written to rank {}".format(ranks[0]))

    def verify_cont_rf_healthstatus(self, container, expected_rf, expected_health):
        """Verify the container redundancy factor and health status.

        Args:
            container (TestContainer): container to verify
            expected_rf (str): expected container redundancy factor.
            expect_cont_status (str): expected container health status.
        """
        actual_rf = None
        actual_health = None

        cont_props = container.get_prop(properties=["rd_fac", "status"])
        for cont_prop in cont_props["response"]:
            if cont_prop["name"] == "rd_fac":
                actual_rf = cont_prop["value"]
            elif cont_prop["name"] == "status":
                actual_health = cont_prop["value"]

        self.assertEqual(
            actual_rf, expected_rf,
            "#Container redundancy factor mismatch, actual: {}, expected: {}.".format(
                actual_rf, expected_rf))
        self.assertEqual(
            actual_health, expected_health,
            "#Container health-status mismatch, actual: {}, expected: {}.".format(
                actual_health, expected_health))

    def start_rebuild_cont_rf(self, container, rd_fac):
        """Start the rebuild process and check for container properties.

        Args:
            container (TestContainer): container to verify
            rd_fac (str): container redundancy factor.
        """
        self.log.info("==>(2)Check for container rd_fac and health-status before rebuild: HEALTHY")
        self.verify_cont_rf_healthstatus(container, rd_fac, "HEALTHY")

        # Exclude the ranks from the pool to initiate rebuild simultaneously
        self.log.info("==>(3)Start rebuild for all specified ranks simultaneously")
        self.server_managers[0].stop_ranks(self.inputs.rank.value, force=True)

    def execute_during_rebuild_cont_rf(self, container, rd_fac, expect_cont_status="HEALTHY"):
        """Execute test steps during rebuild.

        Args:
            container (TestContainer): container to verify
            rd_fac (str): container redundancy factor.
            expect_cont_status (str, optional): expected container health status.
        """
        # Wait for rebuild to start and check for container status
        container.pool.wait_for_rebuild_to_start(1)
        self.log.info(
            "==>(4)Check for container rd_fac and health-status after ranks rebuild started: %s",
            expect_cont_status)
        self.verify_cont_rf_healthstatus(container, rd_fac, expect_cont_status)
        # Wait for rebuild completion and check for container status
        container.pool.wait_for_rebuild_to_end(1)
        self.log.info(
            "==>(5)Check for container rd_fac and health-status after rebuild completed: %s",
            expect_cont_status)
        self.verify_cont_rf_healthstatus(container, rd_fac, expect_cont_status)

    def create_test_container_and_write_obj(self, container, expect_fail=False):
        """Create a container and write objects

           for positive testcase, enable the exception with write objects
           for negative testcase, disable the exception with write objects
           and expecting failure with RC: -1003.

        Args:
            container (TestContainer): container to write to
            expect_fail (bool, optional): container write_objects to handle
                test Exception and parse for return error code.
        """
        der_inval = "RC: -1003"
        self.log.info(
            "==>(1)Create pool and container with redundant factor,"
            " start background IO object write")
        container.create()
        if not expect_fail:
            container.write_objects(
                self.inputs.rank.value[0], self.inputs.object_class.value)
        else:
            try:
                container.write_objects(
                    self.inputs.rank.value[0], self.inputs.object_class.value)
                self.fail("#Container redundancy factor with an invalid "
                          "object_class traffic passed, expecting Fail")
            except DaosTestError as error:
                self.log.info(error)
                if der_inval in str(error):
                    self.log.info("==Negative test, expecting DaosApiError RC: -1003 found.")
                else:
                    self.fail("#Negative test, container redundancy factor "
                              "test failed, return error RC: -1003 not found")

    def execute_cont_rf_test(self, mode=None):
        """Execute the rebuild test steps for container rd_fac test.

        Args:
            mode (str): either "cont_rf_with_rebuild" or "cont_rf_enforcement"
        """
        # Create a pool and verify the pool information before rebuild
        pool = self.get_pool()
        self.verify_pool_info_before_rebuild(pool)
        container = self.get_container(pool, create=False)
        oclass = self.inputs.object_class.value
        rd_fac = ''.join(container.properties.value.split(":"))
        cont_rf_match = re.search(r"rd_fac([0-9]+)", rd_fac)
        if cont_rf_match is None:
            self.fail("Redundancy factor is not available in container properties")
        cont_rf = int(cont_rf_match.group(1))
        oclass_rf = extract_redundancy_factor(oclass)
        # Negative testing pertains to RF enforcement when creating objects - not rebuild
        # If the RF of the oclass is less than the RF set on the container, IO is expected to fail.
        expect_fail = oclass_rf < cont_rf
        # Create a container and write objects
        self.create_test_container_and_write_obj(container, expect_fail)

        if mode == "cont_rf_with_rebuild":
            num_of_ranks = len(self.inputs.rank.value)
            if num_of_ranks > cont_rf:
                expect_cont_status = "UNCLEAN"
            else:
                expect_cont_status = "HEALTHY"
            # Verify the rank to be excluded has at least one object
            self.verify_rank_has_objects(container, self.inputs.rank.value)
            # Start the rebuild process
            self.start_rebuild_cont_rf(container, rd_fac)
            # Execute the test steps during rebuild
            self.execute_during_rebuild_cont_rf(container, rd_fac, expect_cont_status)
            # Refresh local pool and container
            self.log.info("==>(6)Check for pool and container info after rebuild.")
            pool.check_pool_info()
            container.query()
            # Verify the excluded rank is no longer used with the objects
            self.verify_rank_has_no_objects(container, self.inputs.rank.value)
            # Verify the pool information after rebuild
            if expect_cont_status == "HEALTHY":
                self.verify_pool_info_after_rebuild(pool)
                self.log.info("==>(7)Check for container data if the container is healthy.")
                self.verify_container_data(container)
            else:
                container.close()
                container.skip_cleanup()
            self.log.info("Test passed")
        elif mode == "cont_rf_enforcement":
            self.log.info("Container rd_fac test passed")
        else:
            self.fail(f"Unsupported container_rf test mode: {mode}")
