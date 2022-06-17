#!/usr/bin/python3
"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import time
from apricot import TestWithServers
from general_utils import DaosTestError
from avocado.core.exceptions import TestFail
from thread_manager import ThreadManager

class BoundaryTest(TestWithServers):
    """
    Epic: Create system level tests that cover boundary tests and
          functionality.
    Testcase:
          DAOS-8464: Test lots of pools and connections
    Test Class Description:
          Start DAOS servers, create pools and containers to the support limit.
    :avocado: recursive
    """

    def create_container_and_test(self, pool=None, pool_num=1, cont_num=1, with_io=False):
        """To create single container on pool.
        Args:
            pool (str): pool handle to create container.
            pool_num (int): pool number.
            container_num (int): container number to create.
            with_io (bool): enable container test with execute_io.
        """

        try:
            container = self.get_container(pool)
        except (DaosTestError, TestFail) as err:
            msg = "#(3.{}.{}) container create failed. err={}".format(pool_num, cont_num, err)
            self.fail(msg)

        self.log.info("===(3.%d.%d)create_container_and_test, container %s created..",
            pool_num, cont_num, container)
        if with_io:
            io_run_time = self.params.get("run_time", '/run/container/execute_io/*')
            rank = self.params.get("rank", '/run/container/execute_io/*')
            obj_classs = self.params.get("obj_classs", '/run/container/execute_io/*')
            try:
                data_bytes = container.execute_io(io_run_time, rank, obj_classs)
                self.log.info(
                    "===(3.%d.%d)Wrote %d bytes to container %s", pool_num, cont_num,
                    data_bytes, container)
            except (DaosTestError, TestFail) as err:
                msg = "#(3.{}.{}) container IO failed, err: {}".format(pool_num, cont_num, err)
                self.fail(msg)
        time.sleep(2)  #to sync-up containers before close

        try:
            self.log.info("===(4.%d.%d)create_container_and_test, container closing.",
                pool_num, cont_num)
            container.close()
            self.log.info("===(4.%d.%d)create_container_and_test, container closed.",
                pool_num, cont_num)
        except (DaosTestError, TestFail) as err:
            msg = "#(4.{}.{}) container close fail, err: {}".format(pool_num, cont_num, err)
            self.fail(msg)

    def create_containers(self, pool=None, pool_num=10, num_containers=100, with_io=False):
        """To create number of containers in parallel on pool.
        Args:
            pool(str): pool handle.
            pool_num (int): pool number to create containers.
            num_containers (int): number of containers to create.
            with_io (bool): enable container test with execute_io.
        """

        self.log.info("==(2.%d)create_containers start.", pool_num)
        thread_manager = ThreadManager(self.create_container_and_test, self.timeout - 30)

        for cont_num in range(num_containers):
            thread_manager.add(
                pool=pool, pool_num=pool_num, cont_num=cont_num, with_io=with_io)

        # Launch the create_container_and_test threads
        self.log.info("==Launching %d create_container_and_test threads", thread_manager.qty)
        failed_thread_count = thread_manager.check_run()
        self.log.info(
            "==(2.%d) after thread_manager_run, %d containers created.", pool_num, num_containers)
        if failed_thread_count > 0:
            msg = "#(2.{}) FAILED create_container_and_test Threads".format(failed_thread_count)
            self.d_log.error(msg)
            self.fail(msg)

    def create_pools(self, num_pools=10, num_containers=100, with_io=False):
        """To create number of pools and containers in parallel.
        Args:
            num_pools (int): number of pools to create.
            num_containers (int): number of containers to create.
            with_io (bool): enable container test with execute_io.
        """

        # Setup the thread manager
        thread_manager = ThreadManager(self.create_containers, self.timeout - 30)

        for pool_number in range(num_pools):
            pool = self.get_pool()
            thread_manager.add(
                pool=pool, pool_num=pool_number, num_containers=num_containers, with_io=with_io)
            self.log.info("=(1.%d) pool created, %d.", pool_number, pool)

        # Launch the create_containers threads
        self.log.info("=Launching %d create_containers threads", thread_manager.qty)
        failed_thread_count = thread_manager.check_run()
        if failed_thread_count > 0:
            msg = "#(1.{}) FAILED create_containers Threads".format(failed_thread_count)
            self.d_log.error(msg)
            self.fail(msg)

    def test_container_boundary(self):
        """JIRA ID: DAOS-8464 Test lots of pools and containers in parallel.
        Test Description:
            Testcase 1: Test 1 pool with containers boundary condition in parallel.
            Testcase 2: Test large number of pools and containers in parallel.
            Testcase 3: Test pools and containers with io.
            log.info: (a.b.c) a: test-step,  b: pool_number,  c: container_number
        Use case:
            0. Bring up DAOS server.
            1. Create pools and create containers_test by ThreadManager.
            2. Create containers and test under each pool by sub ThreadManager.
            3. Launch io and syncup each container.
            4. Close container.
        :avocado: tags=all,full_regression
        :avocado: tags=container, pool
        :avocado: tags=hw,medium,ib2
        :avocado: tags=boundary_test
        :avocado: tags=container_boundary,pool_boundary
        """

        num_pools = self.params.get("num_pools", '/run/boundary_test/*')
        num_containers = self.params.get("num_containers", '/run/boundary_test/*')
        io = self.params.get("with_io", '/run/boundary_test/*')
        self.create_pools(num_pools=num_pools, num_containers=num_containers, with_io=io)
        self.log.info("===>Boundary test passed.")
