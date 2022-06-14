#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import time
from ior_utils import run_ior
from ior_test_base import IorTestBase
from exception_utils import CommandFailure
from job_manager_utils import get_job_manager


class DaosECAggregationBasic(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description:

       Run IOR with same file option to verify the
       aggregation.

    :avocado: recursive
    """
    def get_free_space(self, storage_index):
        """Get the pool information free space from the specified storage index.

        Args:
            storage_index (int): index of the pool free space to obtain

        Returns:
            int: pool free space for the specified storage index

        """
        self.pool.get_info()
        return self.pool.info.pi_space.ps_space.s_free[storage_index]

    def test_ec_aggregation(self, total_pools=1, total_containers_per_pool=1):
        """Jira ID: DAOS-7326

        Test Description:
            This is a common method which run IOR on the specified pools,
            container quanties and test aggregation.
        """
        # test params
        test_oclass = self.params.get("oclass", '/run/aggregation_test/*')
        processes = self.params.get("np", '/run/ior/*')
        total_runtime = self.params.get("total_runtime",
                                        '/run/aggregation_test/*')
        pool_qty = total_pools
        container_qty = total_containers_per_pool
        start_time = 0
        finish_time = 0
        start_time = time.time()
        while int(finish_time - start_time) < total_runtime:

            job_manager = get_job_manager(self, "Mpirun", None, False, "mpich",
                                          self.get_remaining_time())

            # Create X pools
            self.add_pool_qty(pool_qty, connect=False)
            # Since the transfer size is 1M, the objects will be inserted
            # directly into NVMe and hence storage_index = 1
            storage_index = 1

            initial_free_space = self.get_free_space(storage_index)

            # Disable the aggregation
            self.pool.set_property("reclaim", "disabled")

            # Create Y containers per pool
            for pool in self.pool:
                self.add_container_qty(container_qty, pool)

            # Run ior on each container sequentially
            for container in self.container:
                ior_log = "{}_{}_{}_ior1.log".format(self.test_id,
                                                    container.pool.uid,
                                                    container.uuid)
                try:
                    self.ior_cmd.dfs_oclass.update(test_oclass)
                    self.ior_cmd.dfs_dir_oclass.update(test_oclass)
                    result = run_ior(self, job_manager, ior_log, self.hostlist_clients,
                                     self.workdir, None, self.server_group,
                                     container.pool, container, processes)
                    self.log.info(result)
                except CommandFailure as error:
                    self.log.info(error)
            free_space_after_first_ior = self.get_free_space(storage_index)
            space_used_by_ior = initial_free_space - free_space_after_first_ior

            self.log.info("Space used by first ior = %s", space_used_by_ior)
            self.log.info("Free space after first ior = %s", free_space_after_first_ior)
            self.assertTrue(free_space_after_first_ior < initial_free_space,
                            "IOR run was not successful.")

            # Run ior on each container sequentially
            for container in self.container:
                ior_log = "{}_{}_{}_ior2.log".format(self.test_id,
                                                    container.pool.uid,
                                                    container.uuid)
                try:
                    self.ior_cmd.dfs_oclass.update(test_oclass)
                    self.ior_cmd.dfs_dir_oclass.update(test_oclass)
                    # Run ior the second time on the same pool and container, so another
                    # copy of the file is inserted in DAOS.
                    result = run_ior(self, job_manager, ior_log, self.hostlist_clients,
                                     self.workdir, None, self.server_group,
                                     container.pool, container, processes)
                    self.log.info(result)
                except CommandFailure as error:
                    self.log.info(error)
            free_space_after_second_ior = self.get_free_space(storage_index)

            self.log.info(
                "Free space after second ior = %s", free_space_after_second_ior)

            # Verify the free space after second ior is less at least twice the
            # size of space_used_by_ior from initial_free_space
            self.assertTrue(free_space_after_second_ior <=
                            (initial_free_space - space_used_by_ior * 2),
                            "Running IOR the 2nd time using same file option \
                            did not succeed.")

            # Enable the aggregation
            self.pool.set_property("reclaim", "time")
            # wait 90 seconds for files to get old enough for aggregation +
            # 30 seconds for aggregation to start and finish
            self.log.info("Waiting for 120 seconds for aggregation to start \
                and finish")
            time.sleep(120)
            free_space_after_aggregate = self.get_free_space(storage_index)
            self.log.info(
                "Free space after aggregation = %s", free_space_after_aggregate)

            # Verify the space taken by second ior is reclaimed after aggregation
            # (logical locations will be overwritten as part of aggregation)
            # The free space should be equal to the free space after initial run.
            self.assertTrue(free_space_after_aggregate ==
                            free_space_after_first_ior,
                            "Aggregation did not reclaim the space")
            finish_time = time.time()

    def test_ec_aggregation_single(self):
        """Jira ID: DAOS-7326

        Test Description:
            Purpose of this test is to run ior using DFS api to write to
            the DAOS and rerun it again with the same file option and
            enable the aggregation and verify the aggregation reclaims
            the overwritten space.

        Use case:
            Run ior with read, write, CheckWrite, CheckRead
            Run ior with read, write, CheckWrite, CheckRead with same file
            Enable aggregation and wait for 90 seconds to let the files
            old enough to be marked for aggregation.
            Wait for aggregation to complete.
            Check the utilized capacity of the container/pool. It should be
            roughly the same as after the initial run since aggregation has
            reclaimed the overwritten capacity.

        :avocado: tags=all,manual
        :avocado: tags=hw,large
        :avocado: tags=aggregate
        :avocado: tags=ecaggregatetimed
        :avocado: tags=DAOS_7326
        """
        self.test_ec_aggregation(1, 2)

    def test_ec_aggregation_multiple(self):
        """Jira ID: DAOS-7326

        Test Description:
            Purpose of this test is to run ior using DFS api to write to
            the DAOS and rerun it again with the same file option and
            enable the aggregation and verify the aggregation reclaims
            the overwritten space.

        Use case:
            Run ior with read, write, CheckWrite, CheckRead
            Run ior with read, write, CheckWrite, CheckRead with same file
            Enable aggregation and wait for 90 seconds to let the files
            old enough to be marked for aggregation.
            Wait for aggregation to complete.
            Check the utilized capacity of the container/pool. It should be
            roughly the same as after the initial run since aggregation has
            reclaimed the overwritten capacity.

        :avocado: tags=all,manual
        :avocado: tags=hw,large
        :avocado: tags=aggregate
        :avocado: tags=ecaggregatetimed
        :avocado: tags=DAOS_7326
        """
        self.test_ec_aggregation(2, 3)
