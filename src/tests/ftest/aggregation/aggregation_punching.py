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
            Create a POSIX container and run mdtest with '-C' option
            to just create records
            Punch those records using '-r' in the next run
            Let the aggregation run and verify the space is reclaimed.

        :avocado: tags=all,pr,hw,medium,ib2,aggregation,mdtest
        :avocado: tags=aggregatepunching
        """

        if self.pool is None:
            self.create_pool()
        self.pool.connect()

        storage_index = 1 # SCM
        pool_info = self.pool.pool.pool_query()
        initial_free_space =\
            pool_info.pi_space.ps_space.s_free[storage_index]


        # local params
        params = self.params.get("mdtest_params", "/run/mdtest/*")

        # Disable the aggregation
        self.log.info("Disabling aggregation")
        self.pool.set_property("reclaim", "disabled")

        # update mdtest params
        self.mdtest_cmd.api.update(params[0])
        self.mdtest_cmd.write_bytes.update(params[1])
        self.mdtest_cmd.branching_factor.update(params[2])
        # if branching factor is 1 use num_of_files_dirs
        # else use items option of mdtest
        self.mdtest_cmd.num_of_files_dirs.update(params[3])
        self.mdtest_cmd.depth.update(params[4])
        mdtest_data_size = params[1] * params[3]
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

        self.log.info("Waiting for 120 seconds")
        time.sleep(120)

        pool_info = self.pool.pool.pool_query()
        final_free_space =\
            pool_info.pi_space.ps_space.s_free[storage_index]

        self.log.info("Verifying the aggregation deleted the punched" +
                      " records and space is reclaimed")
        self.log.info("{} == {}".format(
            final_free_space, initial_free_space))
        self.assertTrue(final_free_space == initial_free_space)
