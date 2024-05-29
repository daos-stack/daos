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
    """Create system level tests that cover dulicate rpc detection tests and functionality.

    Test Class Description:
        Create pools run test with and without duplicate rpc detection feature and verify time
        consuming with metadata workload before and after svc ops full.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DuplicateRpcDetectionTest object."""
        super().__init__(*args, **kwargs)

    def setUp(self):
        """Set Up DuplicateRpcDetectionTest"""
        super().setUp()
        self.dmg = self.get_dmg_command()

    def metadata_workload_test(self, pool=None, cont_num=1, workload_cycles=5000, test_loops=1):
        """To create single container and perform metadata workload tests.

        Args:
            pool (str): pool handle to create container. Defaults to None.
            container_num (int): container number to create. Defaults to 1.
            workload_cycles (int): Number of metadata workload test cycles per test loop.
                Defaults to 5000.
            test_loops (int): Number of metadata workload test loops. Defaults to 1.

        Returns:
            test_time: List of time consumed per test loops of metadata workload test cycles.

        """
        test_time = []
        try:
            container = self.get_container(pool, create=False)
            if container.daos:
                container.daos.verbose = False
            container.create()
            self.log.info("Successfully created #%s container", cont_num)
        except (DaosTestError, TestFail) as err:
            self.fail(
                "#({}.{}) container create failed. err={}".format(pool1.label, cont_num, err))
        for ind in range(test_loops):
            start = time.time()
            for _ in range(workload_cycles):
                container.close()
                container.open()
            elapsed_time = time.time() - start
            self.log.info("Completed container Metadata test-loop: %d, elapsed_time: %f",
                          ind+1, elapsed_time)
            test_time.append(elapsed_time)
        for ind in range(test_loops):
            self.log.info("Test time of Metadata test-loop: %d,  %f",
                          ind, test_time[ind])
        container.close()
        return test_time

    def test_metadata_dup_rpc(self):
        """JIRA ID: DAOS-15937 metadata duplicate rpc detection time consuming.

        Test Steps:
            1. Bring up DAOS server.
            2. Create pool1 with specified property svc_ops_entry_age.
            3. Create containers by ThreadManager.
            4. Run specified metadata workload cycles in multiple test loops (N cycles per loop),
               calculate average time per loop, for those loops executed after svc_ops_entry_age
               time has passed.
            5. Create pool2 with property svc_ops_enable:0.
            6. Create containers by ThreadManager on pool2.
            7. Repeat test step 4 on pool2.
            8. Compare metadata workload test time with and without duplicate rpc detection.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,md_on_ssd
        :avocado: tags=server,metadata
        :avocado: tags=DuplicateRpcDetection,test_metadata_dup_rpc
        """
        self.dmg.server_set_logmasks("DEBUG", raise_exception=False)
        cont_num = self.params.get("number_thread", '/run/metadata/*', default=1)
        w_cycles = self.params.get("workload_test_cycles", '/run/metadata/*', default=5000)
        t_loops = self.params.get("test_loops", '/run/metadata/*', default=10)
        threshold_factor = self.params.get("threshold_factor", '/run/metadata/*', default=1.75)

        self.log_step("Create pool with properties svc_ops_entry_age.")
        pool1 = self.get_pool(dmg=self.dmg.copy())

        self.log_step("Create containers by ThreadManager.")
        container_manager = ThreadManager(
            self.metadata_workload_test, self.get_remaining_time() - 30)
        container_manager.add(
            pool=pool1, cont_num=cont_num, workload_cycles=w_cycles, test_loops=t_loops)

        self.log_step("Run specified metadata workload cycles in multiple test loops.")
        results = container_manager.run()
        num_failed = len(list(filter(lambda r: not r.passed, results)))
        if num_failed > 0:
            self.fail('#{} container create threads failed'.format(num_failed))

        self.log_step("Create pool2 with property svc_ops_enable:0.")
        params = {}
        params['properties'] = "svc_ops_enabled:0"
        self.add_pool(**params)

        self.log_step("Create containers by ThreadManager on pool2.")
        container_manager = ThreadManager(
            self.metadata_workload_test, self.get_remaining_time() - 30)
        container_manager.add(
            pool=self.pool, cont_num=cont_num, workload_cycles=w_cycles, test_loops=t_loops)

        self.log_step("Run specified metadata workload cycles in multiple test loops on pool2.")
        base_results = container_manager.run()
        average_time = statistics.mean(base_results[0].result)
        num_failed = len(list(filter(lambda r: not r.passed, base_results)))
        if num_failed > 0:
            self.fail('#{} container create threads failed'.format(num_failed))

        self.log_step(
            "Compare metadata workload test time with and without duplicate rpc detection.")
        self.log.info("===>pool1 results = %s", results[0].result)
        self.log.info("===>baseline results = %s", base_results[0].result)
        self.log.info("===>average baseline result= %s", average_time)
        for result in results[0].result:
            if result > average_time * threshold_factor:
                self.fail(
                    "#Dup rpc dectection time consuming {} > threshold {} * {}".format(result,
                    average_time, threshold_factor))
        self.log.info("Test passed")
