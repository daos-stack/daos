'''
  (C) Copyright 2022-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from data_utils import dict_extract_values
from ior_test_base import IorTestBase


class PoolTargetQueryTest(IorTestBase):
    """
    Test Class Description: To verify object is writing on expected pool targets based on
                            it's type.
    :avocado: recursive
    """

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
        :avocado: tags=PoolTargetQueryTest,test_pool_target_query
        """
        target_usage_count = self.params.get('target_usage_count', '/run/ior/objectclass/*')
        notes = self.params.get('notes', '/run/ior/objectclass/*')

        rank_list = list(range(self.server_managers[0].engines))
        targets_per_rank = self.server_managers[0].get_config_value('targets')
        target_idx = ','.join(map(str, range(targets_per_rank)))

        self.update_ior_cmd_with_pool()

        self.log_step('Get initial NVMe free space for each target')
        initial_space = self.pool.get_space_per_target(ranks=rank_list, target_idx=target_idx)
        unique_nvme_free = set(dict_extract_values(initial_space, ['nvme', 'free']))
        if len(unique_nvme_free) != 1:
            self.log.error('unique_nvme_free = %s', str(unique_nvme_free))
            self.fail('Initial nvme free space not equal for all targets')

        self.log_step('Write data with IOR')
        self.run_ior_with_pool()

        self.log_step('Get NVMe free space for each target')
        current_space = self.pool.get_space_per_target(ranks=rank_list, target_idx=target_idx)

        self.log_step(
            f'Verify NVMe free space decreased for {target_usage_count} targets '
            f'with oclass {self.ior_cmd.dfs_oclass.value}')
        self.log.info("Rank\t Target\t Initial NVMe Free\t Latest NVMe Free\t Change")
        self.log.info("----\t ------\t -----------------\t ----------------\t ------")
        num_decrease = 0
        for rank, targets in initial_space.items():
            for target, devices in targets.items():
                intitial_nvme_free = devices['nvme']['free']
                latest_nvme_free = current_space[rank][target]['nvme']['free']
                if latest_nvme_free < intitial_nvme_free:
                    num_decrease += 1
                    status = 'Decrease'
                elif intitial_nvme_free < latest_nvme_free:
                    status = 'Increase'
                else:
                    status = 'No Change'
                self.log.info(
                    "%s\t %s\t %s\t %s\t %s",
                    str(rank).ljust(4),
                    str(target).ljust(6),
                    str(intitial_nvme_free).ljust(20),
                    str(latest_nvme_free).ljust(20),
                    status)

        if num_decrease != target_usage_count:
            self.fail(
                f'Detected free space reduction in only {num_decrease}/{target_usage_count} '
                f'targets, {notes}')

        self.log_step('Test passed')
