"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from control_test_base import ControlTestBase


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

        # Exclude half the ranks
        num_to_test = max(2, len(all_ranks) // 2)
        test_ranks = self.random.sample(all_ranks, num_to_test)

        self.log_step("Step 1: Excluding %s ranks: %s", (num_to_test, test_ranks))

        for rank in test_ranks:
            self.dmg.system_exclude(ranks=[rank], rank_hosts=None)
            time.sleep(1)  # Small delay between exclusions

        # Step 2: Verify all reach adminexcluded state
        self.log_step("Step 2: Verifying all ranks get excluded from system")
        time.sleep(10)

        for rank in test_ranks:
            failed = self.server_managers[0].check_rank_state(
                ranks=[rank], valid_states=["adminexcluded"], max_checks=5)
            if failed:
                self.fail("Rank %s did not get excluded from system" % rank)
            self.dmg.system_clear_exclude(ranks=[rank], rank_hosts=None)

        # Step 3: Wait and verify all restart
        wait_time = 20
        self.log_step("Step 3: Waiting %ss to verify all automatically restart", wait_time)
        time.sleep(wait_time)

        errors = []
        for rank in test_ranks:
            failed = self.server_managers[0].check_rank_state(
                ranks=[rank], valid_states=["joined"], max_checks=1)
            if failed:
                errors.append("Rank %s unexpectedly not restarted when auto-restart enabled"
                              % rank)

        if errors:
            self.fail("\n".join(errors))

        self.log.info("SUCCESS: All of %s automatically restarted", test_ranks)

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
        if len(all_ranks) < 4:
            self.skipTest("Test requires at least 4 ranks")

        # Create pool first
        self.add_pool(connect=False)

        test_rank = all_ranks[-1]

        self.log_step("Excluding non-service rank %s while pool is active", test_rank)

        restarted, final_state = self.exclude_rank_and_wait_restart(test_rank)

        if not restarted:
            self.fail("Rank %s did not restart. State: %s" % (test_rank, final_state))

        # Verify pool is still accessible
        self.log_step("Verifying pool is still accessible after rank restart")
        self.pool.query()

        self.log.info("SUCCESS: Rank %s restarted and pool remains accessible", test_rank)
