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

    def verify_aggreation(self, first_ior_free_space):
        self.log.info("Waiting for aggregation to complete")
        if not wait_for_result(self.log, 150, check_free_space_equals, expected=first_ior_free_space):
            self.fail("aggregation completion not detected")

    def check_free_space_equals(self, expected):
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
                # Skip the object type if server count does not meet the minimum EC object server count
                if oclass[1] > self.server_count:
                    continue
                ec_object = get_data_parity_number(self.log, oclass)
                containers.append(
                    self.get_container(
                        self.pool, daos_command=self.get_daos_command(), oclass=oclass,
                        properties="rd_fac:{}".format(ec_object['parity']))

        # 2.
        self.log_step("Disable aggregation")
        self.pool.set_property("reclaim", "disabled")

        # 3.
        self.log_step("Get initial pool free space (dmg pool query)")
        initial_free_space = self.pool.get_total_free_space(refresh=True)

        # 4.
        self.log_step("Run IOR write all EC object data to container")
#        self.ior_write_dataset(operation="Auto_Write", percent=self.percent)
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

#        if agg_check == "Restart_before_agg":
#            # step-4 for Restart_before_agg test
#            self.log_step("Shutdown the servers and restart")
#            self.get_dmg_command().system_stop(True)
#            self.get_dmg_command().system_start()
        # 8.
        self.log_step("Enable aggregation")
        self.pool.set_property("reclaim", "time")

        if agg_check == "Restart_after_agg":
            # setp-9 for Restart_after_agg
            self.log_step("Verify aggregation is complete before engine restart")
            self.verify_aggreation()

        # 9.
        self.log_step("Stop the engines (dmg system stop)")
        self.get_dmg_command().system_stop(True)

        # 10.
        self.log_step("Restart the engines (dmg system start)")
        self.get_dmg_command().system_start()

        # 11.
        if agg_check == "Restart_before_agg":
            self.log_step("Verify aggregation is complete after engine restart")
            self.verify_aggreation()

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




        # 4.  step-5 for Restart_before_agg test
        self.log_step("Enable aggregation")
        self.pool.set_property("reclaim", "time")

        # 5.  step-6 for Restart_before_agg test
        self.log_step("Rerun IOR")
        self.ior_write_dataset(storage='SCM', operation="Auto_Write", percent=self.percent)
        init_total_free_space = self.pool.get_total_free_space(refresh=True)
        pool_info = self.pool.get_pool_daos_space()
        init_scm_free_space = pool_info["s_free"][0]
        init_nvme_free_space = pool_info["s_free"][1]
        self.log.info("After aggregation started: total_free: %s, scm_free: %s, nvme_free: %s",
                      init_total_free_space, init_scm_free_space, init_nvme_free_space)



        # 6.  step-7 for Restart_before_agg test
        self.log_step("Verify aggregation triggered, scm free space move to nvme")
        aggregation_detected = False
        timed_out = False
        start_time = time.time()
        while not aggregation_detected and not timed_out:
            pool_info = self.pool.get_pool_daos_space()
            current_scm_free_space = pool_info["s_free"][0]
            current_nvme_free_space = pool_info["s_free"][1]
            diff_scm_free_space = current_scm_free_space - init_scm_free_space
            diff_nvme_free_space = init_nvme_free_space - current_nvme_free_space
            self.log.info(
                "pool scm free space during Aggregation = %s, diff_scm_free_space = %s",
                current_scm_free_space, diff_scm_free_space)
            self.log.info(
                "pool nvme free space during Aggregation = %s, diff_nvme_free_space = %s",
                current_nvme_free_space, diff_nvme_free_space)
            if diff_scm_free_space > aggr_threshold and diff_nvme_free_space > aggr_threshold:
                self.log.info("Aggregation Detectted .....")
                aggregation_detected = True
            else:
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
