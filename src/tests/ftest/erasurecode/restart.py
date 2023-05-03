"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from ec_utils import get_data_parity_number
from exception_utils import CommandFailure
from general_utils import wait_for_result
from ior_utils import run_ior
from job_manager_utils import get_job_manager


class EcodServerRestart(TestWithServers):
    """
    Test Class Description: To validate Erasure code object data after restarting all servers.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for test case."""
        super().setUp()
        self.percent = self.params.get("size", "/run/ior/data_percentage/*")
        engine_count = self.server_managers[0].get_config_value("engines_per_host")
        self.server_count = len(self.hostlist_servers) * engine_count

    def verify_aggreation(self, expected_free_space):
        """Verify aggregation by checking pool free space is greater than expected.

        Args:
            expected_free_space (int): expected free space after aggregation.
        """
        self.log.info("Waiting for aggregation to complete..")
        if not wait_for_result(self.log, self.check_free_space, 180, delay=5,
                               expected_free=expected_free_space):
            self.fail("aggregation completion not detected.")

    def check_free_space(self, expected_free):
        """Check for pool free space.

        Args:
            expected_free (int): expected free space to check.

        Returns:
            bool: if the result was of pool free space equal to or greater than the expected free.
        """
        current_free = self.pool.get_total_free_space(refresh=True)
        self.log.info("=current_free  space: %s", f"{current_free:,}")
        self.log.info("=expected_free space: %s", f"{expected_free:,}")
        return expected_free <= current_free

    def execution(self, agg_check=None):
        """Execute test.

        Common test execution method to write data, verify aggregation, restart
        all the servers and read data.

        Args:
            agg_check: When to check Aggregation status.Either before restarting all the servers or
                       after. Default to None.
        """
        # 1.
        ior_w_flags = self.params.get("flags", "/run/ior/iorflags/*")
        ior_read_flags = self.params.get("read_flags", "/run/ior/iorflags/*")
        block_transfer_sizes = self.params.get("block_transfer_sizes", "/run/ior/*")[0]
        obj_class = self.params.get("dfs_oclass_list", "/run/ior/objectclass/*")
        self.log_step("Create pool and containers")
        self.pool = self.get_pool()
        containers = []
        for oclass in obj_class:
            # Skip the object type if server count does not meet the minimum EC object
            # server count
            if oclass[1] > self.server_count:
                continue
            ec_object = get_data_parity_number(self.log, oclass[0])
            containers.append(
                self.get_container(
                    self.pool, daos_command=self.get_daos_command(), oclass=oclass[0],
                    properties="rd_fac:{}".format(ec_object["parity"])))

        # 2.
        self.log_step("Disable aggregation")
        self.pool.set_property("reclaim", "disabled")

        # 3.
        self.log_step("Get initial pool free space from dmg pool query")
        initial_free_space = self.pool.get_total_free_space(refresh=True)
        self.log.info("=Initial pool free space: %s", initial_free_space)

        # 4.
        self.log_step("Run IOR write all EC object data to container")
        job_manager = get_job_manager(self, subprocess=None, timeout=120)
        for oclass, container in zip(obj_class, containers):
            ior_kwargs = {
                "test": self,
                "manager": job_manager,
                "log": "ior_write_container_test_{}.log".format(oclass[0]),
                "hosts": self.hostlist_clients,
                "path": self.workdir,
                "slots": None,
                "group": self.server_group,
                "pool": self.pool,
                "container": container,
                "processes": self.params.get("np", "/run/ior/client_processes/*"),
                "ppn": self.params.get("ppn", "/run/ior/client_processes/*", default=2),
                "intercept": None,
                "plugin_path": None,
                "dfuse": None,
                "display_space": True,
                "fail_on_warning": False,
                "namespace": "/run/ior/*",
                "ior_params": {
                    "dfs_oclass": oclass[0],
                    "dfs_dir_oclass": oclass[0],
                    "flags": ior_w_flags,
                    "transfer_size": block_transfer_sizes[1],
                    "block_size": block_transfer_sizes[0]
                }
            }
            try:
                run_ior(**ior_kwargs)
            except CommandFailure as error:
                self.fail("IOR write failed, {}".format(error))

        # 5.
        self.log_step("Check for free space after IOR write all EC object data to container")
        free_space_after_ior = self.pool.get_total_free_space(refresh=True)
        space_used_by_ior = initial_free_space - free_space_after_ior
        self.log.info("=After 1st IOR write, pool free space: %s", free_space_after_ior)
        self.log.info("=Space used by 1st IOR: %s", space_used_by_ior)
        self.assertLess(free_space_after_ior, initial_free_space,
                        "Pool free space has not been reduced by IOR writes")

        # 6.
        self.log_step("Run IOR write again with the same data to each container")
        for container in containers:
            ior_kwargs["container"] = container
            try:
                run_ior(**ior_kwargs)
            except CommandFailure as error:
                self.fail("IOR write failed, {}".format(error))
        free_space_after_second_ior = self.pool.get_total_free_space(refresh=True)
        space_used_by_2nd_ior = free_space_after_ior - free_space_after_second_ior
        self.log.info("=After 2nd IOR write with same data, pool free space: %s",
                      free_space_after_second_ior)
        self.log.info("=Space used by 1st IOR: %s", space_used_by_ior)
        self.log.info("=Space used by 2nd IOR: %s", space_used_by_2nd_ior)

        # 7.
        self.log_step(
            "Verify the free space after 2nd IOR is less at least twice the size of "
            "space used by 1st IOR from the initial free space")
        self.assertLessEqual(
            free_space_after_second_ior, (initial_free_space - space_used_by_ior * 2),
            "Pool free space has not been reduced by double after dual ior writes")

        if agg_check == "Restart_after_agg":
            # setp-8 for Restart_after_agg
            self.log_step("Enable aggregation")
            self.pool.set_property("reclaim", "time")

            # setp-9 for Restart_after_agg
            self.log_step("Verify aggregation is completed before engine restart")
            self.verify_aggreation(free_space_after_ior)

        # 8.
        self.log_step("Stop the engines (dmg system stop)")
        # setp-10 for Restart_after_agg
        self.get_dmg_command().system_stop(True)

        # 9.
        self.log_step("Restart the engines (dmg system start)")
        # setp-11 for Restart_after_agg
        self.get_dmg_command().system_start()

        if agg_check == "Restart_before_agg":
            # 10.
            self.log_step("Enable aggregation")
            self.pool.set_property("reclaim", "time")

            # 11.
            self.log_step("Verify aggregation is completed after engine restart")
            self.verify_aggreation(free_space_after_ior)

        # 12.
        self.log_step("Verify data after aggregation (ior read)")
        ior_kwargs["ior_params"]["flags"] = ior_read_flags
        for container in containers:
            ior_kwargs["container"] = container
            try:
                run_ior(**ior_kwargs)
            except CommandFailure as error:
                self.fail("IOR read verification failed, {}".format(error))

        self.log_step("Test passed")

    def test_ec_restart_before_agg(self):
        """Jira ID: DAOS-7337.

        Test Description: Test Erasure code object with IOR after all server restart and Aggregation
                            trigger before restart.
        Use Case: Create the pool, run IOR with supported EC object type class for small and
                    large transfer sizes.Verify aggregation starts, Restart all the servers.
                    Read and verify all IOR data.

        Test steps:
            1. Create pool and containers
            2. Disable aggregation
            3. Get initial pool free space (dmg pool query)
            4. Run IOR write all EC object data to container
            5. Check for free space after IOR write all EC object data to container
            6. Run IOR write again with the same data to each container
            7. Verify the free space after 2nd IOR is less at least twice the size of space used by
               1st IOR from the initial free space
            8. Stop the engines (dmg system stop)
            9. Restart the engines (dmg system start)
            10. Enable aggregation
            11. Verify aggregation is completed after engine restart
            12. Verify data after aggregation (ior read)

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
            1. Create pool and containers
            2. Disable aggregation
            3. Get initial pool free space (dmg pool query)
            4. Run IOR write all EC object data to container
            5. Check for free space after IOR write all EC object data to container
            6. Run IOR write again with the same data to each container
            7. Verify the free space after 2nd IOR is less at least twice the size of space used by
               1st IOR from the initial free space
            8. Enable aggregation
            9. Verify aggregation is completed before engine restart
            10. Stop the engines (dmg system stop)
            11. Restart the engines (dmg system start)
            12. Verify data after aggregation (ior read)

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=ec,ec_array,ec_server_restart,ec_aggregation
        :avocado: tags=EcodServerRestart,test_ec_restart_after_agg
        """
        self.execution(agg_check="Restart_after_agg")
