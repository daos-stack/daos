"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
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
        aggr_threshold = self.params.get("aggregation_threshold", '/run/ior/*', default=2000)
        self.log_step("Create pool")

        # 2.
        self.log_step("Disable aggregation")
        self.pool.set_property("reclaim", "disabled")
        # Get initial total free space (scm+nvme)
        initial_free_space = self.pool.get_total_free_space(refresh=True)
        self.log.info("initial pool free space: %s", initial_free_space)

        # 3.
        self.log_step("Run IOR write all EC object data to container")
        self.ior_write_dataset(operation="Auto_Write", percent=self.percent)
        free_space_after_ior = self.pool.get_total_free_space(refresh=True)
        self.log.info("pool free space after IOR write: %s", free_space_after_ior)

        if agg_check == "Restart_before_agg":
            # step-4 for Restart_before_agg test
            self.log_step("Shutdown the servers and restart")
            self.get_dmg_command().system_stop(True)
            self.get_dmg_command().system_start()

        # 4.  step-5 for Restart_before_agg test
        self.log_step("Enable aggregation")
        self.pool.set_property("reclaim", "time")
        # Aggregation will start in 20 seconds after it sets to time mode. So wait for 20
        # seconds and restart all the servers.
        time.sleep(20)

        # 5.  step-6 for Restart_before_agg test
        self.log_step("Rerun IOR")
        self.ior_write_dataset(operation="Auto_Write", percent=self.percent)
        init_free_space = self.pool.get_total_free_space(refresh=True)
        self.log.info("pool free space after aggregation started: %s", init_free_space)

        # 6.  step-7 for Restart_before_agg test
        self.log_step("Verify aggregation triggered")
        aggregation_detected = False
        timed_out = False
        start_time = time.time()
        while not aggregation_detected and not timed_out:
            current_free_space = self.pool.get_total_free_space(refresh=True)
            self.log.info("pool free space during Aggregation = %s", current_free_space)
            if current_free_space > init_free_space + aggr_threshold:
                self.log.info("Aggregation Detectted .....")
                aggregation_detected = True
            else:
                init_free_space = current_free_space
                time.sleep(5)
                if time.time() - start_time > 180:
                    timed_out = True
        if not aggregation_detected:
            self.fail("#Aggregation failed to start..")

        if agg_check == "Restart_after_agg":
            # 7.
            self.log_step("Shutdown the servers and restart")
            self.get_dmg_command().system_stop(True)
            self.get_dmg_command().system_start()

        # 8.
        self.log_step("run IOR read to verify data")
        self.ior_read_dataset(operation="Auto_Read", percent=self.percent)
        self.log.info("Test passed")

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
            7. Verify aggregation triggered
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
            6. Verify aggregation triggered
            7. Shutdown the servers and restart
            8. run IOR read to verify data

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=ec,ec_array,ec_server_restart,ec_aggregation
        :avocado: tags=EcodServerRestart,test_ec_restart_after_agg
        """
        self.execution(agg_check="Restart_after_agg")
