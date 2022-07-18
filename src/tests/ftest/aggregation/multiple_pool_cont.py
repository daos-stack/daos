#!/usr/bin/python
"""
  (C) Copyright 2021-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import time
from ior_utils import run_ior
from ior_test_base import IorTestBase
from exception_utils import CommandFailure
from job_manager_utils import get_job_manager


class DaosAggregationMultiPoolCont(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description:

       Run IOR with same file option to verify the
       aggregation.

    :avocado: recursive
    """
    def setUp(self):
        super().setUp()
        self.free_space_dict = {}

    def save_free_space(self, space_tag, storage_index):
        """Save the storage space in free_space dictionary for each pool

        Args:
            space_tag (int): Free space tagged at various stages in testcase.
            0: Initial free space before IOR run.
            1: Free space after first IOR run.
            2: Free space after second IOR run.
            3. Free space after aggregation is complete.
            Example:
            self.free_space_dict[pool][0] = val [Initial free space]
            self.free_space_dict[pool][1] = val [After 1st IOR free space]
            self.free_space_dict[pool][2] = val [After 2nd IOR free space]
            self.free_space_dict[pool][3] = val [After aggregation complete]

            storage_index (int): index of the pool free space to obtain
        """
        for pool in self.pool:
            pool.get_info()
            value = pool.info.pi_space.ps_space.s_free[storage_index]
            if space_tag == 0:
                self.free_space_dict[pool.uuid] = {space_tag: value}
            else:
                self.free_space_dict[pool.uuid][space_tag] = value
            self.log.info("Free Space Information")
            self.log.info("======================")
            self.log.info(self.free_space_dict)

    def verify_free_space(self, space_tag1, space_tag2):
        """Verify whether the free space is decremented between two IOR runs.
           Asserts if free sapce with space_tag1 less than free space with
           space_tag2.

        Args:
            space_tag1 : IOR run tag (0,1,2)
            space_tag2 : IOR run (1,2,3)
        """
        for pool in self.pool:
            space_used_by_ior = (self.free_space_dict[pool.uuid][space_tag1] -
                                 self.free_space_dict[pool.uuid][space_tag2])
            self.log.info("Pool %s Space used by ior = %s", pool.uuid,
                          space_used_by_ior)
            self.assertGreater(self.free_space_dict[pool.uuid][space_tag1],
                               self.free_space_dict[pool.uuid][space_tag2],
                               "Free space did not decrease after IOR.")

    def longrun_aggregation(self, total_pools=1, total_containers_per_pool=1):
        """Jira ID: DAOS-7326
        Test Description:
            This is a common method which run IOR on the specified pools,
            container quanties and test aggregation.
        """
        # test params
        total_runtime = self.params.get('total_runtime', '/run/runtime/*')
        start_time = 0
        finish_time = 0

        job_manager = get_job_manager(self, "Mpirun", None, False, "mpich",
                                      self.get_remaining_time())
        # Create requested pools
        self.add_pool_qty(total_pools, connect=False)
        start_time = time.time()
        while int(finish_time - start_time) < int(total_runtime):
            # Since the transfer size is 1M, the objects will be inserted
            # directly into NVMe and hence storage_index = 1
            storage_index = 1

            self.save_free_space(0, storage_index)

            for pool in self.pool:
                # Disable the aggregation
                pool.set_property("reclaim", "disabled")
                # Create the containers requested per pool
                self.add_container_qty(total_containers_per_pool, pool)

            # Run ior on each container sequentially
            for i in [1, 2]:
                for container in self.container:
                    ior_log = "{}_{}_{}_ior1.log".format(self.test_id,
                                                         container.pool.uuid,
                                                         container.uuid)
                    try:
                        result = run_ior(self, job_manager, ior_log, self.hostlist_clients,
                                         self.workdir, None, self.server_group,
                                         container.pool, container,
                                         self.processes)
                        self.log.info(result)
                    except CommandFailure as error:
                        self.log.info(error)
                self.save_free_space(i, storage_index)
                self.verify_free_space((i-1), i)

            # Enable the aggregation
            for pool in self.pool:
                pool.set_property("reclaim", "time")

            # wait 90 seconds for files to get old enough for aggregation +
            # 30 seconds for aggregation to start and finish
            self.log.info("Waiting for 120 seconds for aggregation to start and finish")
            time.sleep(120)
            self.save_free_space(3, storage_index)

            # Verify space taken by second ior is reclaimed after aggregation
            # (logical locations will be overwritten as part of aggregation)
            # The free space should be equal to the free space almost close
            # to free space after 1st IOR run.
            for pool in self.pool:
                percentage = int((self.free_space_dict[pool.uuid][1] /
                                  self.free_space_dict[pool.uuid][3]) * 100)
                self.assertGreater(percentage, 97,
                                   "Aggregation did not reclaim the space")

            # Destroy all containers and pools and start once again.
            self.destroy_containers(self.container)
            # Let the GC (garbage collection) cleanup the space.
            time.sleep(60)
            # Initialize the container list
            self.container = []
            finish_time = time.time()

    def test_aggregation_single_pool(self):
        """Jira ID: DAOS-7326

        Test Description:
            Use a single pool and multiple containers to validate
            aggregation works.

        Test Steps:
             - Create pool and multiple containers.
             - Disable aggregation.
             - Get initial free space
             - Start IOR (use -k ior option to keep the data).
             - Get the free space after first IOR run
             - Overwrite IOR data once again.
             - Get the free space after second IOR run/
             - Enable Aggregation and wait for aggregation to complete.
             - Check the free space after aggregation.
             - Verify free space after aggregation is almost same
               as free space after first IOR run.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=aggregation
        :avocado: tags=aggregate_single_pool
        """
        self.longrun_aggregation(1, 2)

    def test_aggregation_multiple_pools(self):
        """Jira ID: DAOS-7326

        Test Description:
            Use multiple pools/containers to validate aggregation.

        Test Steps:
             - Create multiple pools and multiple containers.
             - Disable aggregation.
             - Get initial free space for each pool
             - Start IOR ion each pool(use -k ior option to keep the data).
             - Get the free space on each pool after first IOR run
             - Overwrite IOR data once again on each pools/containers.
             - Get the free space after second IOR run on each pool.
             - Enable Aggregation and wait for aggregation to complete.
             - Check the free space after aggregation on each pool
             - Verify free space after aggregation is almost same
               as free space after first IOR run.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=aggregation
        :avocado: tags=aggregate_multiple_pools
        """
        self.longrun_aggregation(2, 3)
