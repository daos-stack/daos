"""
  (C) Copyright 2022-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import statistics
import time

from apricot import TestWithServers
from avocado.core.exceptions import TestFail
from general_utils import DaosTestError
from thread_manager import ThreadManager


class DuplicateRpcDetection(TestWithServers):
    """Compare metadata handling performance between pools with and without duplicate rpc
       detection feature enabled.

    Test Class Description:
        Create pools run test with and without duplicate rpc detection feature and verify time
        consuming with metadata workload before and after svc ops full.

    :avocado: recursive
    """

    def metadata_workload_test(self, pool, num_of_cont, workload_cycles, test_loops):
        """To create single container and perform metadata workload tests.

        Args:
            pool (str): pool handle to create container.
            num_of_cont (int): Number of container to be created.
            workload_cycles (int): Number of metadata workload test cycles per test loop.
            test_loops (int): Number of metadata workload test loops.

        Returns:
            list: List of time consumed per test loops of metadata workload test cycles.

        """
        test_time = []
        try:
            daos = self.get_daos_command()
            daos.verbose = False
            container = self.get_container(pool, daos=daos)
            self.log.info("Successfully created #%s container", num_of_cont)
        except (DaosTestError, TestFail) as err:
            self.fail(
                "#({}.{}) container create failed. err={}".format(pool.label, num_of_cont, err))
        for ind in range(test_loops):
            start = time.time()
            for _ in range(workload_cycles):
                container.open()
                container.close()
            elapsed_time = time.time() - start
            self.log.info("Completed container Metadata test-loop: %d, elapsed_time: %f",
                          ind + 1, elapsed_time)
            test_time.append(elapsed_time)
        for ind in range(test_loops):
            self.log.info("Test time of Metadata test-loop: %d,  %f",
                          ind, test_time[ind])
        return test_time

    def test_metadata_dup_rpc(self):
        """JIRA ID: DAOS-15937 metadata duplicate rpc detection time consuming.

        Test Steps:
            1. Bring up DAOS server.
            2. Create pool1 with specified property svc_ops_entry_age.
            3. Create containers by ThreadManager.
            4. Run specified metadata workload cycles in multiple test loops (N cycles per loop).
            5. Create pool2 with property svc_ops_enable:0.
            6. Create containers by ThreadManager on pool2.
            7. To establish a "baseline" time (without duplicate rpc detection), perform test
               step 4 on pool2, calculating average time per loop executed.
            8. Compare all metadata workload times (with duplicate rpc detection) to the average
               baseline time (without duplicate rpc).

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,md_on_ssd
        :avocado: tags=server,metadata
        :avocado: tags=DuplicateRpcDetection,test_metadata_dup_rpc
        """
        num_of_cont = self.params.get("number_thread", '/run/metadata/*', default=1)
        w_cycles = self.params.get("workload_test_cycles", '/run/metadata/*', default=5000)
        t_loops = self.params.get("test_loops", '/run/metadata/*', default=10)
        threshold_factor = self.params.get("threshold_factor", '/run/metadata/*', default=1.75)

        self.log_step("Create pool with properties svc_ops_entry_age.")
        pool1 = self.get_pool(dmg=self.get_dmg_command().copy())

        self.log_step("Create containers by ThreadManager.")
        container_manager = ThreadManager(
            self.metadata_workload_test, self.get_remaining_time() - 30)
        container_manager.add(
            pool=pool1, num_of_cont=num_of_cont, workload_cycles=w_cycles, test_loops=t_loops)

        self.log_step("Run specified metadata workload cycles in multiple test loops.")
        results = container_manager.run()
        num_failed = len(list(filter(lambda r: not r.passed, results)))
        if num_failed > 0:
            self.fail('#{} container create threads failed'.format(num_failed))

        self.log_step("Create pool2 with property svc_ops_enable:0.")
        self.add_pool(properties="svc_ops_enabled:0")

        self.log_step("Create containers by ThreadManager on pool2.")
        container_manager = ThreadManager(
            self.metadata_workload_test, self.get_remaining_time() - 30)
        container_manager.add(
            pool=self.pool, num_of_cont=num_of_cont, workload_cycles=w_cycles, test_loops=t_loops)

        self.log_step(
            "To establish a baseline time without duplicate rpc detection, ",
            "calculating average time per loop executed on pool2.")
        base_results = container_manager.run()
        num_failed = len(list(filter(lambda r: not r.passed, base_results)))
        if num_failed > 0:
            self.fail('#{} container create threads failed'.format(num_failed))
        base_average_time = statistics.mean(base_results[0].result)

        self.log_step(
            "Compare metadata workload test time with and without duplicate rpc detection.")
        self.log.info("pool1 results = %s", results[0].result)
        self.log.info("baseline results = %s", base_results[0].result)
        self.log.info("average baseline result= %s", base_average_time)
        for result in results[0].result:
            if result > base_average_time * threshold_factor:
                self.fail(
                    "#Dup rpc detection time {} > baseline_time {} * threshold_factor {}".format(
                        result, base_average_time, threshold_factor))
        self.log.info("Test passed")
