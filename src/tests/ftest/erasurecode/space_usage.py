"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from data_utils import dict_extract_values, dict_subtract, list_stats
from general_utils import percent_change
from ior_test_base import IorTestBase
from oclass_utils import calculate_ec_targets_used


class EcodSpaceUsage(IorTestBase):
    """Verifies EC space usage.

    Test Description:
            Verify even space consumption with EC data.
        Use Case:
            Create a pool.
            Create a POSIX container.
            Use IOR to create a large file with EC*GX.
            Verify the expected number of targets were used.
            Verify used targets use within X% of the mean space.

    :avocado: recursive
    """

    def test_ec_space_balanced_ec_4p1gx(self):
        """Jira ID: DAOS-10912.

        Verify space balance with EC_4P1GX.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=ec,ior,pool,query_targets
        :avocado: tags=EcodSpaceUsage,test_ec_space_balanced_ec_4p1gx
        """
        self._run_test(ior_namespace='/run/ior_ec_4p1gx/*')

    def test_ec_space_balanced_ec_4p2gx(self):
        """Jira ID: DAOS-10912.

        Verify space balance with EC_4P2GX.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=ec,ior,pool,query_targets
        :avocado: tags=EcodSpaceUsage,test_ec_space_balanced_ec_4p2gx
        """
        self._run_test(ior_namespace='/run/ior_ec_4p2gx/*')

    def _run_test(self, ior_namespace):
        """Run the test.

        Args:
            ior_namespace (str): IOR namespace to use in the config

        """
        max_diff_percent = self.params.get('max_diff_percent', '/run/space_usage/*')
        rank_list = list(range(self.server_managers[0].engines))
        targets_per_rank = self.server_managers[0].get_config_value("targets")
        total_targets = targets_per_rank * len(rank_list)
        target_idx = ','.join(map(str, range(targets_per_rank)))

        self.pool = self.get_pool()
        self.container = self.get_container(self.pool)

        # Save space per target before running IOR
        space_before = self.pool.get_space_per_target(ranks=rank_list, target_idx=target_idx)

        # Run IOR
        self.ior_cmd.namespace = ior_namespace
        self.ior_cmd.get_params(self)
        self.ior_cmd.set_daos_params(self.pool.identifier, self.container.identifier)
        self.run_ior_with_pool(create_pool=False, create_cont=False, display_space=False)

        # Save space per target after running IOR
        space_after = self.pool.get_space_per_target(ranks=rank_list, target_idx=target_idx)

        # Calculate the difference in space so we know how much the usage increased
        space_diff = dict_subtract(space_after, space_before)
        self.log.debug('File oclass: %s', self.ior_cmd.dfs_oclass.value)
        self.log.debug('Space per target before IOR: %s', space_before)
        self.log.debug('Space per target after IOR : %s', space_after)
        self.log.debug('                difference : %s', space_diff)

        # Keep just the non-zero values. I.e. the targets that were used
        scm_used = list(filter(None, dict_extract_values(space_diff, ['scm', 'used'])))
        nvme_used = list(filter(None, dict_extract_values(space_diff, ['nvme', 'used'])))
        self.log.debug('SCM Targets Used : %s', len(scm_used))
        self.log.debug('NVMe Targets Used: %s', len(nvme_used))

        # Aggregate the space used to get min, max, mean
        scm_aggregated = list_stats(scm_used)
        nvme_aggregated = list_stats(nvme_used)
        self.log.debug('Aggregated SCM : %s', scm_aggregated)
        self.log.debug('Aggregated NVMe: %s', nvme_aggregated)

        # Calculate the max percent diff from the mean
        max_scm_diff = max(
            abs(percent_change(scm_aggregated['mean'], scm_aggregated['max'])),
            abs(percent_change(scm_aggregated['mean'], scm_aggregated['min'])))
        max_nvme_diff = max(
            abs(percent_change(nvme_aggregated['mean'], nvme_aggregated['max'])),
            abs(percent_change(nvme_aggregated['mean'], nvme_aggregated['min'])))
        self.log.info('Max used SCM difference from mean : %.2f%%', max_scm_diff * 100)
        self.log.info('Max used NVMe difference from mean: %.2f%%', max_nvme_diff * 100)

        # Verify the correct number of targets were used
        # Don't enforce SCM, since little metadata is used
        expected_targets = calculate_ec_targets_used(self.ior_cmd.dfs_oclass.value, total_targets)
        if len(nvme_used) != expected_targets:
            self.fail('Incorrect number of targets used!')

        # Verify space usage across targets is balanced
        # Don't enforce SCM, since little metadata is used
        if max_nvme_diff > max_diff_percent:
            self.fail(
                'NVMe space imbalance exceeds {:.2f}% threshold'.format(max_diff_percent * 100))
