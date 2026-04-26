"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from control_test_base import ControlTestBase
from general_utils import report_errors


class EngineAutoRestartTest(ControlTestBase):
    """Test automatic engine restart on self-termination.

    Test Class Description:
        Verify automatic engine restart behavior when engines self-terminate
        after being excluded from the system.

    :avocado: recursive
    """

    def test_auto_restart_basic(self):
        """Test basic automatic engine restart after self-termination.

        Test Description:
            1. Exclude a rank from the system
            2. Wait for rank to self-terminate
            3. Verify rank automatically restarts and rejoins the system

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=dmg,control,engine_auto_restart
        :avocado: tags=EngineAutoRestartTest,test_auto_restart_basic
        """
        all_ranks = self.get_all_ranks()
        if len(all_ranks) < 2:
            self.skipTest("Test requires at least 2 ranks")

        test_rank = self.random.choice(all_ranks)

        self.log_step("Testing automatic restart of rank %s", test_rank)

        restarted, final_state = self.exclude_rank_and_wait_restart(test_rank)

        if not restarted:
            self.fail("Rank %s did not automatically restart. Final state: %s"
                      % (test_rank, final_state))

        self.log.info("SUCCESS: Rank %s automatically restarted after self-termination",
                      test_rank)

    def test_auto_restart_multiple_ranks(self):
        """Test automatic restart of multiple ranks.

        Test Description:
            1. Exclude multiple ranks simultaneously
            2. Wait for all to self-terminate
            3. Verify all automatically restart and rejoin

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=dmg,control,engine_auto_restart
        :avocado: tags=EngineAutoRestartTest,test_auto_restart_multiple_ranks
        """
        all_ranks = self.get_all_ranks()
        if len(all_ranks) < 3:
            self.skipTest("Test requires at least 3 ranks")

        # Exclude half the ranks (but keep at least one for quorum)
        num_to_exclude = max(1, len(all_ranks) // 2)
        ranks_to_test = self.random.sample(all_ranks, num_to_exclude)

        self.log_step("Testing automatic restart of multiple ranks: %s", ranks_to_test)

        errors = []
        results = {}

        for test_rank in ranks_to_test:
            restarted, final_state = self.exclude_rank_and_wait_restart(test_rank)
            results[test_rank] = (restarted, final_state)

            if not restarted:
                errors.append(
                    "Rank %s did not automatically restart. State: %s" % (test_rank, final_state))

        # Report results
        self.log.info("=== Multiple Rank Restart Results ===")
        for rank, (restarted, state) in results.items():
            status = "PASS" if restarted else "FAIL"
            self.log.info("Rank %s: %s (final state: %s)", rank, status, state)

        report_errors(test=self, errors=errors)

    def test_auto_restart_with_pool(self):
        """Test automatic restart works with active pools.

        Test Description:
            1. Create a pool
            2. Exclude a rank (not in pool service)
            3. Verify rank automatically restarts
            4. Verify pool remains accessible

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=dmg,control,engine_auto_restart,pool
        :avocado: tags=EngineAutoRestartTest,test_auto_restart_with_pool
        """
        all_ranks = self.get_all_ranks()
        if len(all_ranks) < 2:
            self.skipTest("Test requires at least 2 ranks")

        # Create pool first
        self.add_pool(connect=False)

        # Get pool service ranks to avoid excluding them
        pool_svc_ranks = self.pool.svc_ranks
        self.log.info("Pool service ranks: %s", pool_svc_ranks)

        # Find a rank not in pool service
        non_svc_ranks = [r for r in all_ranks if r not in pool_svc_ranks]
        if not non_svc_ranks:
            self.skipTest("All ranks are pool service ranks")

        test_rank = self.random.choice(non_svc_ranks)

        self.log_step("Excluding non-service rank %s while pool is active", test_rank)

        restarted, final_state = self.exclude_rank_and_wait_restart(test_rank)

        if not restarted:
            self.fail("Rank %s did not restart. State: %s" % (test_rank, final_state))

        # Verify pool is still accessible
        self.log_step("Verifying pool is still accessible after rank restart")
        self.pool.query()

        self.log.info("SUCCESS: Rank %s restarted and pool remains accessible", test_rank)
