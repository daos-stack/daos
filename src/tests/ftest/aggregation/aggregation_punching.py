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

        :avocado: tags=all,pr,hw,medium,ib2,aggregation,mdtest
        :avocado: tags=aggregatepunching
        """

        if self.pool is None:
            self.create_pool()
        self.pool.connect()

        storage_index = 1 # SSD
        pool_info = self.pool.pool.pool_query()
        initial_free_space =\
            pool_info.pi_space.ps_space.s_free[storage_index]

        # Disable the aggregation
        self.log.info("Disabling aggregation")
        self.pool.set_property("reclaim", "disabled")

        # local params
        params = self.params.get("mdtest_params", "/run/mdtest/*")
        processes = self.params.get("np", "/run/mdtest/*")
        # update mdtest params
        self.mdtest_cmd.api.update(params[0])
        self.mdtest_cmd.write_bytes.update(params[1])
        self.mdtest_cmd.branching_factor.update(params[2])
        # if branching factor is 1 use num_of_files_dirs
        # else use items option of mdtest
        self.mdtest_cmd.num_of_files_dirs.update(params[3])
        self.mdtest_cmd.depth.update(params[4])

        # write bytes * num_dir_size * num_of_client_processes
        mdtest_data_size = params[1] * params[3] * processes
        # run mdtest
        self.execute_mdtest()

        pool_info = self.pool.pool.pool_query()
        free_space_after_mdtest =\
            pool_info.pi_space.ps_space.s_free[storage_index]

        self.log.info("Free space after mdtest == initial free space" +
                      " - mdtest_data")
        self.log.info("{} == {}".format(
            free_space_after_mdtest, initial_free_space-mdtest_data_size))
        self.assertTrue(free_space_after_mdtest < initial_free_space)

        # Enable the aggregation
        self.log.info("Enabling aggregation")
        self.pool.set_property("reclaim", "time")

        counter = 0
        self.log.info("Verifying the aggregation deleted the punched" +
                      " records and space is reclaimed")

        # For the given mdtest configuration, the aggregation should
        # be done in less than 120 seconds.
        while counter < 4:
            pool_info = self.pool.pool.pool_query()
            final_free_space =\
                pool_info.pi_space.ps_space.s_free[storage_index]

            if final_free_space == initial_free_space:
                break
            else:
                self.log.info("Space is not reclaimed yet !")
                self.log.info("Sleeping for 30 seconds")
                time.sleep(30)
                counter += 1

        pool_info = self.pool.pool.pool_query()
        final_free_space =\
            pool_info.pi_space.ps_space.s_free[storage_index]

        self.log.info("{} == {}".format(
            final_free_space, initial_free_space))
        self.assertTrue(final_free_space == initial_free_space)
