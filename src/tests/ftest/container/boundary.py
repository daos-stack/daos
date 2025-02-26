"""
  (C) Copyright 2022-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import itertools
import random
import time

from apricot import TestWithServers
from avocado.core.exceptions import TestFail
from general_utils import DaosTestError
from thread_manager import ThreadManager


class BoundaryTest(TestWithServers):
    """
    Epic: Create system level tests that cover boundary tests and functionality.
    Testcase:
          DAOS-8464: Test lots of pools and connections
    Test Class Description:
          Start DAOS servers, create pools and containers to the support limit.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a BoundaryTest object."""
        super().__init__(*args, **kwargs)
        self.with_io = False
        self.io_run_time = None
        self.io_rank = None
        self.io_obj_classs = None

    def setUp(self):
        """Set Up BoundaryTest"""
        super().setUp()
        self.pool = []
        self.with_io = self.params.get("with_io", '/run/boundary_test/*')
        self.io_run_time = self.params.get("run_time", '/run/container/execute_io/*')
        self.io_rank = self.params.get("rank", '/run/container/execute_io/*')
        self.io_obj_classs = self.params.get("obj_classs", '/run/container/execute_io/*')
        self.dmg = self.get_dmg_command()

    def create_pool(self):
        """Get a test pool object and append to list.

        Returns:
            TestPool: the created test pool object.

        """
        pool = self.get_pool(dmg=self.dmg.copy())
        self.pool.append(pool)
        return pool

    def create_container_and_test(self, pool=None, cont_num=1):
        """To create single container on pool.

        Args:
            pool (str): pool handle to create container.
            container_num (int): container number to create.

        """
        try:
            container = self.get_container(pool)
        except (DaosTestError, TestFail) as err:
            self.fail(
                "#(3.{}.{}) container create failed. err={}".format(pool.label, cont_num, err))

        if self.with_io:
            try:
                _ = container.execute_io(self.io_run_time, self.io_rank, self.io_obj_classs)
            except (DaosTestError, TestFail) as err:
                self.fail(
                    "#(3.{}.{}) container IO failed, err: {}".format(pool.label, cont_num, err))
        time.sleep(2)  # to sync containers before close

        try:
            container.close()
        except (DaosTestError, TestFail) as err:
            self.fail(
                "#(4.{}.{}) container close failed, err: {}".format(pool.label, cont_num, err))

    def create_pools(self, num_pools, num_containers):
        """To create number of pools and containers in parallel.

        Args:
            num_pools (int): number of pools to create.
            num_containers (int): number of containers to create.

        """
        # Create pools in parallel
        pool_manager = ThreadManager(self.create_pool, self.get_remaining_time() - 30)
        for _ in range(num_pools):
            pool_manager.add()
        self.log.info('Creating %d pools', num_pools)
        # Explicitly elevate log mask to DEBUG for multiple pool create scenario and
        # restore it after.
        self.dmg.server_set_logmasks("DEBUG", raise_exception=False)
        result = pool_manager.run()
        self.dmg.server_set_logmasks(raise_exception=False)
        num_failed = len(list(filter(lambda r: not r.passed, result)))
        if num_failed > 0:
            self.fail('{} pool create threads failed'.format(num_failed))
        self.log.info('Created %d pools', num_pools)

        # Create all containers for all pools in parallel
        container_manager = ThreadManager(
            self.create_container_and_test, self.get_remaining_time() - 30)
        all_pool_cont_args = list(itertools.product(self.pool, range(num_containers)))
        random.shuffle(all_pool_cont_args)
        for pool, cont_num in all_pool_cont_args:
            container_manager.add(pool=pool, cont_num=cont_num)
        self.log.info('Creating %d containers for each pool', num_containers)
        self.log.info("==Launching %d create_container_and_test threads", container_manager.qty)
        result = container_manager.run()
        num_failed = len(list(filter(lambda r: not r.passed, result)))
        if num_failed > 0:
            self.fail('{} container create threads failed'.format(num_failed))
        self.log.info('Created %d * %d containers', num_pools, num_containers)

    def test_container_boundary(self):
        """JIRA ID: DAOS-8464 Test lots of pools and containers in parallel.
        Test Description:
            Testcase 1: Test 1 pool with containers boundary condition in parallel.
            Testcase 2: Test large number of pools and containers in parallel.
            Testcase 3: Test pools and containers with io.
            log.info: (a.b.c) a: test-step,  b: pool.label,  c: container_number
        Use case:
            0. Bring up DAOS server.
            1. Create pools and create containers_test by ThreadManager.
            2. Create containers and test under each pool by sub ThreadManager.
            3. Launch io and sync-up each container.
            4. Close container.
        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=container,pool,boundary_test
        :avocado: tags=BoundaryTest,test_container_boundary
        """
        num_pools = self.params.get("num_pools", '/run/boundary_test/*')
        num_containers = self.params.get("num_containers", '/run/boundary_test/*')
        self.create_pools(num_pools=num_pools, num_containers=num_containers)
        self.log.info("===>Boundary test passed.")
