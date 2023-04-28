"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
import general_utils
from ec_utils import ErasureCodeIor


class EcodServerRestart(ErasureCodeIor):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: To validate Erasure code object data after restarting all servers.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for test case."""
        super().setUp()
        self.percent = self.params.get("size", '/run/ior/data_percentage/*')

    def verify_aggreation(self, expected_free_space):
        """Verify aggrgation by checking pool free space is expected.

        Args:
            expected_free_space (int): expected free space after aggregation.
        """
        self.log.info("Waiting for aggregation to complete")
        if not wait_for_result(
            self.log, check_free_space_equals, 150, delay=5, expected=expected_free_space):
            self.fail("#aggregation completion not detected.")

    def check_free_space_equals(self, expected):
        """Check for pool free space.

        return:
            bool: if the result was of pool free space match the expected.
        """
        return expected == self.pool.get_total_free_space(refresh=True)


    def execution(self, agg_check=None):
        """Execute test.

        Common test execution method to write data, verify aggregation, restart
        all the servers and read data.

        Args:
            agg_check: When to check Aggregation status.Either before restarting all the servers or
                       after.Default is None so not to check aggregation, but wait for 20 seconds
                       and restart the servers.
        """
        # 1.
        aggr_threshold = self.params.get("aggregation_threshold", '/run/ior/*', default=300)
        self.log_step("Create pool and containers")
        containers = []
        for oclass in self.obj_class:
            for sizes in self.ior_chu_trs_blk_size:
                # Skip the object type if server count does not meet the minimum EC object
                # server count
                if oclass[1] > self.server_count:
                    continue
                ec_object = get_data_parity_number(self.log, oclass)
                containers.append(
                    self.get_container(
                        self.pool, daos_command=self.get_daos_command(), oclass=oclass,
                        properties="rd_fac:{}".format(ec_object['parity'])))

        # 2.
        self.log_step("Disable aggregation")
        self.pool.set_property("reclaim", "disabled")

        # 3.
        self.log_step("Get initial pool free space (dmg pool query)")
        initial_free_space = self.pool.get_total_free_space(refresh=True)

        # 4.
        self.log_step("Run IOR write all EC object data to container")
        for container in containers:
            ior_kwargs['container'] = container
            try:
                result = run_ior(**ior_kwargs)
            except CommandFailure as error:
                self.fail('ior failed')

        # 5.
        self.log_step("Check for free space after IOR write all EC object data to container")
        free_space_after_ior = self.pool.get_total_free_space(refresh=True)
        self.log.info("pool free space after IOR write: %s", free_space_after_ior)
        self.assertLess(free_space_after_ior, initial_free_space,
                        "Pool free space has not been reduced by IOR writes")

        # 6.
        self.log_step("Run IOR write again with the same data to each container")
        for container in containers:
            ior_kwargs['container'] = container
            try:
                result = run_ior(**ior_kwargs)
            except CommandFailure as error:
                self.fail('ior failed')
        free_space_after_second_ior = self.pool.get_total_free_space(refresh=True)

        # 7.
        self.log_step("Verify the free space after second ior is less at least twice the size of"
                      " space_used_by_ior from initial_free_space")
        self.assertLessEqual(
            free_space_after_second_ior, (initial_free_space - space_used_by_ior * 2),
            "Pool free space has not been reduced by double after dual ior writes")

        # 8.
        self.log_step("Enable aggregation")
        self.pool.set_property("reclaim", "time")

        if agg_check == "Restart_after_agg":
            # setp-9 for Restart_after_agg
            self.log_step("Verify aggregation is complete before engine restart")
            self.verify_aggreation(free_space_after_ior)

        # 9.
        self.log_step("Stop the engines (dmg system stop)")
        self.get_dmg_command().system_stop(True)

        # 10.
        self.log_step("Restart the engines (dmg system start)")
        self.get_dmg_command().system_start()

        # 11.
        if agg_check == "Restart_before_agg":
            self.log_step("Verify aggregation is complete after engine restart")
            self.verify_aggreation(free_space_after_ior)

        # 12.
        self.log_step("Verify data after aggregation (ior read)")
        ior_kwargs['flags'] = ior_read_flags
        for container in containers:
            ior_kwargs['container'] = container
            try:
                result = run_ior(**ior_kwargs)
            except CommandFailure as error:
                self.fail('ior failed')

        self.log_step("Test passed")

    def test_ec_restart_before_agg(self):
        """Jira ID: DAOS-7337.

        Test Description: Test Erasure code object with IOR after all server restart and Aggregation
                            trigger before restart.
        Use Case: Create the pool, run IOR with supported EC object type class for small and
                    large transfer sizes.Verify aggregation starts, Restart all the servers.
                    Read and verify all IOR data.

        Test steps:
            1. Create pool
            2. Disable aggregation
            3. Run IOR write all EC object data to container
            4. Shutdown the servers and restart
            5. Enable aggregation
            6. Rerun IOR write
            7. Verify aggregation triggered, scm free space move to nvme
            8. run IOR read to verify data

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=ec,ec_array,ec_server_restart,ec_aggregation
        :avocado: tags=EcodServerRestart,test_ec_restart_before_agg
        """
        self.execution(agg_check="Restart_before_agg")

    def test_ec_restart_after_agg(self):
        """Jira ID: DAOS-7337.

        Test Description: Test Erasure code object with IOR after all server restart and Aggregation
                            trigger after restart.
        Use Case: Create the pool, run IOR with supported EC object type class for small and
                large transfer sizes.Restart all servers. Verify Aggregation trigger after it's
                start. Read and verify all IOR data.

        Test steps:
            1. Create pool
            2. Disable aggregation
            3. Run IOR write all EC object data to container
            4. Enable aggregation
            5. Rerun IOR write
            6. Verify aggregation triggered, scm free space move to nvme
            7. Shutdown the servers and restart
            8. run IOR read to verify data

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=ec,ec_array,ec_server_restart,ec_aggregation
        :avocado: tags=EcodServerRestart,test_ec_restart_after_agg
        """
        self.execution(agg_check="Restart_after_agg")
