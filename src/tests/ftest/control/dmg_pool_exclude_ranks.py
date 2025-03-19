"""
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from control_test_base import ControlTestBase


class DmgPoolExcludeRanks(ControlTestBase):
    """Test dmg pool exclude  command.

    Test Class Description:
        Simple test to verify the dmg exclude --force option.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for dmg pool query."""
        super().setUp()

        # Init the pool
        self.add_pool(connect=False)

    def test_pool_exclude_ranks_basic(self):
        """Test the exclusion when it break Pool RF.

        Test Description:
            1. Create a Pool with DAOS_POOL_RF=0
            2. Attempt to exclude a rank that would break the RF, expecting a failure.
            3. Force exclude the rank, which should succeed despite the RF violation.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dmg,control
        :avocado: tags=DmgPoolExcludeRanks,test_pool_exclude_ranks_basic
        """
        self.log.info("Basic tests of pool exclude command that break Pool RF")

        all_ranks = list(self.server_managers[0].ranks.keys())
        exclude_rank = self.random.choice(all_ranks)

        # Disable failing the test if pool exclude fail
        with self.pool.dmg.no_exception():
            # exclude second rank without force should fail
            self.log_step(f"Excluding rank:{exclude_rank} all_ranks={all_ranks}")
            self.pool.exclude([exclude_rank], tgt_idx=None, force=False)
            self.assertNotEqual(
                self.pool.dmg.result.exit_status, 0,
                "exclude pool without force should fail"
            )
        self.pool.exclude([exclude_rank], tgt_idx=None, force=True)
