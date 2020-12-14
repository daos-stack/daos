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
from general_utils import human_to_bytes


class DaosAggregationIOSmall(IorTestBase):
    # pylint: disable=too-many-ancestors,too-few-public-methods
    """Test class for testing aggregation with small I/O.

    Test class Description:
       Run IOR (<4k) with -k option to verify the data is
       written to SCM and after the aggregation, it's moved
       to the Nvme.

    :avocado: recursive
    """

    def test_aggregation_io_small(self):
        """Jira ID: DAOS-3750.

        Test Description:
            Purpose of this test is to run ior wht < 4k transfer size
            and verify the data is initially written into SCM and later
            moved to SSD NV DIMMs.

        :avocado: tags=all,full_regression,hw,large,aggregate,daosio
        :avocado: tags=aggregateiosmall
        """
        # Create pool and container
        self.update_ior_cmd_with_pool()

        # Since the transfer size is 1K, the objects will be inserted
        # into SCM
        scm_index = 0
        ssd_index = 1
        block_size = human_to_bytes(self.params.get("block_size", "/run/ior/*"))
        num_processes = self.params.get("np", "/run/ior/client_processes/*")
        total_ior = block_size * num_processes

        pool_info = self.pool.get_pool_daos_space()
        initial_scm_free_space = pool_info["s_free"][scm_index]
        initial_ssd_free_space = pool_info["s_free"][ssd_index]
        self.log.info(
            "Initial SCM Free Space = {}".format(initial_scm_free_space))
        self.log.info(
            "Initial SSD Free Space = {}".format(initial_ssd_free_space))

        # Disable the aggregation
        self.log.info("Disabling the aggregation")
        self.pool.set_property("reclaim", "disabled")

        # Run ior
        self.run_ior_with_pool()
        pool_info = self.pool.get_pool_daos_space()
        scm_free_space_after_ior = pool_info["s_free"][scm_index]
        ssd_free_space_after_ior = pool_info["s_free"][ssd_index]
        self.log.info(
            "SCM Free Space after ior = {}".format(scm_free_space_after_ior))
        self.log.info(
            "SSD Free Space after ior = {}".format(ssd_free_space_after_ior))

        self.log.info(
            "Comparing if scm space after ior - {} is less than initial free "
            "space - {}".format(
                scm_free_space_after_ior, initial_scm_free_space))
        self.assertLessEqual(
            scm_free_space_after_ior, (initial_scm_free_space - total_ior),
            "SCM free space after IOR > the initial SCM free space")

        self.log.info("Checking that nothing has been moved to SSD")
        self.assertEqual(
            ssd_free_space_after_ior, initial_ssd_free_space,
            "Detected data moved to SSD after running IOR")

        # Enable the aggregation
        self.log.info("Enabling the aggregation")
        self.pool.set_property("reclaim", "time")
        # wait 90 seconds for files to get old enough for aggregation +
        # 90 seconds for aggregation to start and finish
        wait_time = 180
        self.log.info("Waiting for {} seconds".format(wait_time))
        time.sleep(wait_time)

        pool_info = self.pool.get_pool_daos_space()
        scm_free_space_after_aggregate = pool_info["s_free"][scm_index]
        ssd_free_space_after_aggregate = pool_info["s_free"][ssd_index]

        self.log.info("Checking the data is moved to SSD after aggregation")
        self.log.info(
            "{} == {}".format(
                (initial_ssd_free_space - total_ior),
                ssd_free_space_after_aggregate))
        self.assertEqual(
            (initial_ssd_free_space - total_ior),
            ssd_free_space_after_aggregate,
            "No data detected in SSD after aggregation")
        self.log.info("Checking the SCM space is reclaimed")
        self.log.info(
            "{} > {}".format(
                scm_free_space_after_aggregate, scm_free_space_after_ior))
        self.assertGreater(
            scm_free_space_after_aggregate, scm_free_space_after_ior,
            "SCM space has not been reclaimed")
