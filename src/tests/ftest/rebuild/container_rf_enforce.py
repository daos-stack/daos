#!/usr/bin/python
"""
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from daos_utils import DaosCommand
from general_utils import DaosTestError
from container_redundancy import RbldContRedundancyFactor
import re

class RbldContRedundancyFactorEnforce(RbldContRedundancyFactor):
    # pylint: disable=too-many-ancestors
    """Test Container redundancy factor enforcement with rebuild.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a Rebuild Container RF with ObjClass Write object."""
        super().__init__(*args, **kwargs)
        self.mode = None
        self.daos_cmd = None

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
                    self.log.info("==Negative test, excepting DaosApiError "
                                  "RC: -1003 found.")
                else:
                    self.fail("#Negative test, container redundancy factor "
                              "test failed, return error RC: -1003 not found")

    def execute_rf_enforce_test(self, create_container=True):
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
        num_of_ranks = len(self.inputs.rank.value)
        rf = ''.join(self.container.properties.value.split(":"))
        rf_num = int(re.search(r"rf([0-9]+)", rf).group(1))
        if num_of_ranks > rf_num:
            expect_cont_status = "UNCLEAN"
        else:
            expect_cont_status = "HEALTHY"
        negative_test = True
        if ("OC_SX" in oclass and rf_num < 1) or (
                "OC_RP_2" in oclass and rf_num < 2) or (
                    "OC_RP_3" in oclass and rf_num < 3):
            negative_test = False
        # Create a pool and verify the pool information before rebuild
        self.create_test_pool()
        # Create a container and write objects
        self.create_test_container_and_write_obj(negative_test)
        if negative_test:
            self.log.info("Negative test passed")
        else:
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

    def test_container_redundancy_factor_enforcement(self):
        """Jira ID:
        DAOS-6267: Verify that a container can be created with and enforces
                   a redundancy factor of 0.
        DAOS-6268: Verify container with RF 0 created, deleted and error
                   reported as target fails.
        DAOS-6269: Verify container with RF 2 enforces object creation limits.
        Description:
            Test step:
                (1)Create pool and container with redundant factor
                   Start background IO object write, and verify the
                   container redundant factor vs. different object_class
                   traffic.
                   Continue following steps for the positive testcases.
                (2)Check for container initial rf and health-status.
                (3)Rank rebuild start.
                (4)Check for container rf and health-status after the
                   rebuild started.
                (5)Check for container rf and health-status after the
                   rebuild completed.
                (6)Check for pool and container info after rebuild.
                (7)Verify container io object write if the container is
                   healthy.
        Use Cases:
            Verify container RF enforcement with different object class
            traffic, positive test of rebuild with server failure and read
            write io verification.

        :avocado: tags=all,full_regression
        :avocado: tags=container,rebuild
        :avocado: tags=container_rf_enforce
        """
        self.execute_rf_enforce_test()
