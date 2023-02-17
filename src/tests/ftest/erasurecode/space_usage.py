"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from ior_test_base import IorTestBase
from data_utils import list_stats, dict_subtract, dict_extract_values
from general_utils import percent_change


class EcodSpaceUsage(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Verifies EC space usage.

    Test Description:
            Verify even space consumption with EC data.
        Use Case:
            Create a pool.
            Create a POSIX container.
            Use IOR to create a large file with EC*GX.
            Verify all targets use within X% of the mean space.

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
        :avocado: tags=ec,ior
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
        self.ior_cmd.set_daos_params(
            self.server_group, self.pool.identifier, self.container.identifier)
        self.run_ior_with_pool(create_pool=False, create_cont=False, display_space=False)

        # Save space per target after running IOR
        space_after = self.pool.get_space_per_target(ranks=rank_list, target_idx=target_idx)

        # Calculate the difference in space so we know how much the usage increased
        space_diff = dict_subtract(space_after, space_before)

        # Aggregate the space used to get min, max, mean
        space_aggregated = {
            'scm': list_stats(dict_extract_values(space_diff, ['scm', 'used'])),
            'nvme': list_stats(dict_extract_values(space_diff, ['nvme', 'used']))
        }

        # Print useful debugging info
        self.log.debug('Space per target before IOR: %s', space_before)
        self.log.debug('Space per target after IOR : %s', space_after)
        self.log.debug('                difference : %s', space_diff)
        self.log.debug('Aggregated SCM : %s', space_aggregated['scm'])
        self.log.debug('Aggregated NVMe: %s', space_aggregated['nvme'])

        # Calculate the max percent diff from the mean
        max_scm_diff = max(
            abs(percent_change(space_aggregated['scm']['mean'], space_aggregated['scm']['max'])),
            abs(percent_change(space_aggregated['scm']['mean'], space_aggregated['scm']['min'])))
        max_nvme_diff = max(
            abs(percent_change(space_aggregated['nvme']['mean'], space_aggregated['nvme']['max'])),
            abs(percent_change(space_aggregated['nvme']['mean'], space_aggregated['nvme']['min'])))

        # Log max percent diff for debugging
        self.log.info('Max used SCM difference from mean : %.2f%%', max_scm_diff * 100)
        self.log.info('Max used NVMe difference from mean: %.2f%%', max_nvme_diff * 100)

        # Sanity check that all targets were queried
        scm_targets = len(space_aggregated['scm']['values'])
        nvme_targets = len(space_aggregated['nvme']['values'])
        if scm_targets != total_targets or nvme_targets != total_targets:
            self.fail('Expected {} total targets queried but got {} and {}'.format(
                total_targets, scm_targets, nvme_targets))

        if max_scm_diff > max_diff_percent or max_nvme_diff > max_diff_percent:
            self.fail('Space imbalance exceeds {:.2f}% threshold'.format(max_diff_percent * 100))
