#!/usr/bin/python
'''
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from ior_test_base import IorTestBase

class PoolTargetQueryTest(IorTestBase):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: To verify object is writing on expected pool targets based on
                            it's type.
    :avocado: recursive
    """
    def setUp(self):
        """Set up each test case."""
        super().setUp()
        engine_count = self.server_managers[0].get_config_value("engines_per_host")
        self.server_count = len(self.hostlist_servers) * engine_count
        self.target_usage_count = self.params.get("target_usage_count", '/run/ior/objectclass/*')

    def target_space_info(self):
        """Get the NVMe Target Space Information from all Targets.

        return:
            nvme_rank_space (list): nvme free space list from all targets.

        """
        nvme_rank_space = []
        self.pool.connect()

        # Collect the space information for all the ranks and targets
        for _rank in range(self.server_count):
            for _target in range(self.server_managers[0].get_config_value("targets")):
                result = self.pool.pool.target_query(_target, _rank)
                nvme_rank_space.append(result.ta_space.s_free[1])

        return nvme_rank_space

    def test_pool_target_query(self):
        """Jira ID: DAOS-4661.

        Test Description: Test Pool Target space is used based on object type.
        Use Case: Create the pool, Get the Initial NVMe space for all targets,
                  Use IOR to write the specific object type, Get the NVMe
                  space from all targets. Verify that space is getting used based
                  on object type.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=pool
        :avocado: tags=pool_target_query
        """
        self.update_ior_cmd_with_pool()
        # Check the initial size of all targets
        initial_nvme_size = self.target_space_info()
        self.log.info("Initial Target space = %s", initial_nvme_size)

        # Write the IOR data
        self.run_ior_with_pool()

        # Get the size after writing the IOR data
        latest_nvme_size = self.target_space_info()
        self.log.info("Latest Target space = %s", latest_nvme_size)

        # Verify the target sizes is only increasing based on object layout
        # specified in yaml file
        target_count = 0
        for count, initial_size in enumerate(initial_nvme_size):
            if latest_nvme_size[count] != initial_size:
                target_count += 1
        if target_count != self.target_usage_count:
            self.fail("Expected to change the free space capacity for {} targets,"
                      " But it's changed for {} targets".format(self.target_usage_count,
                                                               str(target_count)))
