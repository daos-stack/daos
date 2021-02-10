#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import time
from mdtest_test_base import MdtestBase


class AggregationPunching(MdtestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs Mdtest with in small config and
       verify aggregation after punching.

    :avocado: recursive
    """

    def test_aggregation_punching(self):
        """Jira ID: DAOS-3443

        Test Description:
            Test the aggregation feature after punching records.

        Use Cases:
            Disable aggregation
            Create a POSIX container and run mdtest
            Enable the aggregation run and verify the space is reclaimed.

        :avocado: tags=all,pr,daily_regression,hw,medium,ib2,aggregation,mdtest
        :avocado: tags=aggregatepunching
        """

        if self.pool is None:
            self.add_pool(connect=False)
        self.pool.connect()

        storage_index = 1 # SSD
        pool_info = self.pool.pool.pool_query()
        initial_free_space =\
            pool_info.pi_space.ps_space.s_free[storage_index]

        # Disable the aggregation
        self.log.info("Disabling aggregation")
        self.pool.set_property("reclaim", "disabled")

        write_bytes = self.params.get("write_bytes", "/run/mdtest/*")
        num_files = self.params.get("num_of_files_dirs", "/run/mdtest/*")
        processes = self.params.get("np", "/run/mdtest/*")

        # write bytes * num_of_files_dirs * num_of_client_processes
        mdtest_data_size = write_bytes * num_files * processes
        # run mdtest
        self.execute_mdtest()

        pool_info = self.pool.pool.pool_query()
        free_space_after_mdtest =\
            pool_info.pi_space.ps_space.s_free[storage_index]

        self.log.info("free_space_after_mdtest <= initial_free_space" +
                      " - mdtest_data_size")
        self.log.info("mdtest_data_size = %s", mdtest_data_size)
        self.log.info("Storage Index = %s",
                      "NVMe" if storage_index else "SCM")
        self.log.info("%s <= %s", free_space_after_mdtest,
                      initial_free_space-mdtest_data_size)
        self.assertTrue(free_space_after_mdtest <=
                        initial_free_space - mdtest_data_size)

        # Enable the aggregation
        self.log.info("Enabling aggregation")
        self.pool.set_property("reclaim", "time")

        counter = 0
        self.log.info("Verifying the aggregation deleted the punched" +
                      " records and space is reclaimed")

        expected_free_space = free_space_after_mdtest + mdtest_data_size

        # For the given mdtest configuration, the aggregation should
        # be done in less than 150 seconds.
        while counter < 5:
            pool_info = self.pool.pool.pool_query()
            final_free_space =\
                pool_info.pi_space.ps_space.s_free[storage_index]

            if final_free_space >= expected_free_space:
                break
            else:
                self.log.info("Space is not reclaimed yet !")
                self.log.info("Sleeping for 30 seconds")
                time.sleep(30)
                counter += 1

        pool_info = self.pool.pool.pool_query()
        final_free_space =\
            pool_info.pi_space.ps_space.s_free[storage_index]

        self.log.info("Checking if space is reclaimed")
        self.log.info("final_free_space >=" +
                      " free_space_after_mdtest + mdtest_data_size")
        self.log.info("%s >= %s", final_free_space, expected_free_space)
        self.assertTrue(final_free_space >= expected_free_space)
