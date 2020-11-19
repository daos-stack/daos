#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""

import time
from ior_test_base import IorTestBase


class DaosAggregationBasic(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description:

       Run IOR with same file option to verify the
       aggregation happens in the background. Check for the
       space usage before and after the aggregation to verify
       the overwritten space is reclaimed.

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

    def test_basic_aggregation(self):
        """Jira ID: DAOS-3451

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

        :avocado: tags=all,pr,hw,large,aggregate,daosio,aggregatebasic
        :avocado: tags=DAOS_5610
        """

        # Create pool and container
        self.update_ior_cmd_with_pool()

        # Since the transfer size is 1M, the objects will be inserted
        # directly into NVMe and hence storage_index = 1
        storage_index = 1

        initial_free_space = self.get_free_space(storage_index)

        # Disable the aggregation
        self.pool.set_property("reclaim", "disabled")

        # Run ior with -k option to retain the file and not delete it
        self.run_ior_with_pool()
        free_space_after_first_ior = self.get_free_space(storage_index)

        space_used_by_ior = initial_free_space - free_space_after_first_ior

        self.log.info("Space used by first ior = %s", space_used_by_ior)
        self.log.info(
            "Free space after first ior = %s", free_space_after_first_ior)
        self.assertTrue(free_space_after_first_ior < initial_free_space,
                        "IOR run was not successful.")

        # Run ior the second time on the same pool and container, so another
        # copy of the file is inserted in DAOS.
        self.run_ior_with_pool(create_pool=False)
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
