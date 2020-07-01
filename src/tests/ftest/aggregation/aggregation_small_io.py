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

class DaosAggregationIOSmall(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description:

       Run IOR (<4k) with -k option to verify the data is
       written to SCM and after the aggregation, it's moved
       to the Nvme.

    :avocado: recursive
    """

    def test_aggregation_io_small(self):
        """Jira ID: DAOS-3750

        Test Description:
            Purpose of this test is to run ior wht < 4k transfer size
            and verify the data is initially written into SCM and later
            moved to SSD NV DIMMs.

        :avocado: tags=all,pr,hw,large,aggregate,daosio,aggregateiosmall
        """

        # Create pool and container
        self.update_ior_cmd_with_pool()

        # Since the transfer size is 1K, the objects will be inserted
        # into SCM
        scm_index = 0
        ssd_index = 1

        pool_info = self.pool.pool.pool_query()
        initial_scm_free_space =\
            pool_info.pi_space.ps_space.s_free[scm_index]
        initial_ssd_free_space =\
            pool_info.pi_space.ps_space.s_free[ssd_index]

        self.log.info("Initial SCM Free Space = {}".format(
            initial_scm_free_space))
        self.log.info("Initial SSD Free Space = {}".format(
            initial_ssd_free_space))

        # Disable the aggregation
        self.log.info("Disabling the aggregation")
        self.pool.set_property("reclaim", "disabled")

        # Run ior 
        self.run_ior_with_pool()
        pool_info = self.pool.pool.pool_query()
        scm_free_space_after_ior =\
            pool_info.pi_space.ps_space.s_free[scm_index]
        ssd_free_space_after_ior =\
            pool_info.pi_space.ps_space.s_free[ssd_index]
   
        self.log.info("SCM Free Space after ior = {}".format(
            scm_free_space_after_ior))
        self.log.info("SSD Free Space after ior = {}".format(
            ssd_free_space_after_ior))

        self.log.info("Comparing if scm space after ior - {} is less
            than initial free space - {}".format(scm_free_space_after_ior,
            initial_scm_free_space))
        self.assertTrue(scm_free_space_after_ior <=
             (initial_scm_free_space - 3000000000))

        self.log.info("Checking that nothing has been moved to SSD")
        self.assertTrue(ssd_free_space_after_ior == initial_ssd_free_space)

        # Enable the aggregation
        self.log.info("Enabling the aggregation")
        self.pool.set_property("reclaim", "time")
        # wait 90 seconds for files to get old enough for aggregation +
        # 30 seconds for aggregation to start and finish
        self.log.info("Waiting for 120 seconds for aggregation to start \
            and finish")
        time.sleep(120)
        pool_info = self.pool.pool.pool_query()
        scm_free_space_after_aggregate =\
             pool_info.pi_space.ps_space.s_free[scm_index]
        ssd_free_space_after_aggregate =\
             pool_info.pi_space.ps_space.s_free[ssd_index]

        self.log.info("Checking the data is moved to SSD after 
                       aggregation") 
        self.assertTrue(ssd_free_space_after_aggregate ==
            (initial_ssd_free_space - 3000000000))
        self.log.info("Checking the SCM space is reclaimed")
        self.assertTrue(scm_free_space_after_aggregate ==
            initial_scm_free_space)
