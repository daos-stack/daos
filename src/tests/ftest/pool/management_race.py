#!/usr/bin/python3
"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import time
import random
from apricot import TestWithServers
from thread_manager import ThreadManager
from command_utils_base import CommandFailure
from pydaos.raw import DaosApiError

class PoolManagementRace(TestWithServers):
    """
    Epic: Create system level tests that cover pool management race boundary tests and
          functionality.
    Testcase:
          DAOS-8463: Pool management integrity with race boundary conditions
    Test Class Description:
          Start DAOS servers, create pools and delete the pool while query and list.
    :avocado: recursive
    """

    def del_recreate_query_and_list_pools(
            self, del_recreate=True, test_pool=None, thread_num=1, test_loop=2):
        """ To delete and recreate pool with the same label, to query and list pools.
        Args:
            del_recreate (bool): flag to delete and recreate pool with the same label.
            test_pool (TestPool): the pool to delete/create/query.
            thread_num (int): thread number.
            test_loop (int): number of test loop to be executed.
        """

        wait_time = self.params.get("wait_time", '/run/boundary_test/*')
        max_query_time = self.params.get("max_query_time", '/run/boundary_test/*')
        pool_id = test_pool.identifier
        for test_num in range(test_loop):
            if del_recreate:
                test_pool.destroy()
                self.log.info(
                    "--(%d.2.%s.%d)Pool %s deleted.\n", thread_num, pool_id, test_num, pool_id)
                test_pool.create()
                self.log.info("--(%d.3.%s.%d)Pool %s recreated.\n",
                              thread_num, pool_id, test_num, pool_id)
                # pool stays with a random time before destroy
                pool_stay_time = random.randint(1, 3) #nosec
                time.sleep(pool_stay_time)
            else:
                completed = False
                start = float(time.time())
                while not completed:
                    try:
                        self.log.info("--(%d.4.%s.%d)--pool querying.. \n",
                                      thread_num, pool_id, test_num)
                        self.get_dmg_command().pool_query(pool=pool_id)
                        daos_tool = self.get_daos_command()
                        daos_tool.exit_status_exception = False
                        daos_tool.pool_query(pool=pool_id)
                        completed = True
                    except (DaosApiError, CommandFailure) as error:
                        self.log.info(
                            "-->Pool %s deleted by another thread, "
                            "race condition detected by dmg, retry.. %s", pool_id, error)
                        time.sleep(wait_time)
                        if float(time.time()) - start > max_query_time:
                            self.fail("#({}.5.{}.{})Pool query failed and retry timeout.".format(
                                      thread_num, pool_id, test_num))
                daos_tool.exit_status_exception = True
                self.log.info("-->Test thread %d, test_loop %d completed.", thread_num, test_num)

    def test_pool_management_race(self):
        """JIRA ID: DAOS-8463: Pool management integrity with race boundary conditions
        Test Description:
            Verify pool management actions create/delete/query are well behaved when multiple
            queries while deleting and recreate.
            Using 2 thread_managers, 1st thread_manager performs repeating pool_delete and
            pool_recreate, 2nd thread_manager (with multiple threads) perform pool_query and
            pool_list.
        Use case:
            1. Create num_pools pools.
            2. Setup the thread manager for delete and recreate the pool with same label.
            3. Setup multiple threads query and list pools, retry when the pool been deleted
               by the other Thread, until timeout.
            4. Launch all the threads with number of test loops.
            5. Check for failure from thread_manager
        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=pool,boundary_test
        :avocado: tags=pool_mgmt_race
        Args:
        """

        num_pools = self.params.get("num_pools", '/run/boundary_test/*')
        test_loop = self.params.get("test_loop", '/run/boundary_test/*')
        num_query_threads = self.params.get("num_query_threads", '/run/boundary_test/*')
        self.pool = []
        for pool_number in range(num_pools):
            self.pool.append(self.get_pool())
            self.log.info("==(1.%d) pool created, %s.", pool_number, self.pool[-1].identifier)

        # Randomly select a pool for delete, recreate and query
        pool_number = random.randint(0, len(self.pool)-1)   # nosec

        # Setup the thread manager for del_and_recreate_pool
        thread_manager = ThreadManager(self.del_recreate_query_and_list_pools, self.timeout - 30)
        for index, t_pool in enumerate(self.pool):
            thread_manager.add(
                del_recreate=False, test_pool=t_pool, thread_num=index, test_loop=test_loop)
        # Setup the thread manager for del_and_recreate_pool
        thread_manager.add(
            del_recreate=True, test_pool=self.pool[pool_number], thread_num=index, test_loop=test_loop)

        # Launch all the threads
        self.log.info("==Launching %d delete_and_recreate_pool and query_and_list_pools threads",
                      thread_manager.qty)
        failed_thread_count = thread_manager.check_run()
        if failed_thread_count > 0:
            msg = "#(6.{}) FAILED del_and_recreate_pool Threads".format(failed_thread_count)
            self.d_log.error(msg)
            self.fail(msg)
        self.log.info("===>Pool management race test passed.")
