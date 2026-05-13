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

        self.log_step("testing automatic restart of rank %s", test_rank)

        # get initial incarnation number
        initial_incarnation = self.get_rank_incarnation(test_rank)
        if initial_incarnation is None:
            self.fail(f"failed to get initial incarnation for rank {test_rank}")

        self.log.info("rank %s initial incarnation: %s", test_rank, initial_incarnation)

        restarted, final_state = self.exclude_rank_and_wait_restart(test_rank)

        if not restarted:
            self.fail(f"rank {test_rank} did not automatically restart. "
                      f"final state: {final_state}")

        # verify incarnation increased after restart
        final_incarnation = self.get_rank_incarnation(test_rank)
        if final_incarnation is None:
            self.fail(f"failed to get final incarnation for rank {test_rank}")

        self.log.info("rank %s final incarnation: %s", test_rank, final_incarnation)

        if final_incarnation <= initial_incarnation:
            self.fail(f"rank {test_rank} incarnation did not increase after restart. "
                      f"before: {initial_incarnation}, after: {final_incarnation}")

        self.log.info("SUCCESS: rank %s automatically restarted after self-termination "
                      "(incarnation %s -> %s)",
                      test_rank, initial_incarnation, final_incarnation)

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

        self.log_step(f"Exclude {num_to_test} ranks: {test_ranks}")

        incs = []
        for rank in test_ranks:
            initial_incarnation = self.get_rank_incarnation(rank)
            if initial_incarnation is None:
                self.fail(f"failed to get initial incarnation for rank {rank}")
            incs.append(initial_incarnation)
            self.dmg.system_exclude(ranks=[rank], rank_hosts=None)
            time.sleep(1)  # small delay between exclusions
            self.dmg.system_clear_exclude(ranks=[rank], rank_hosts=None)

        # Step 3: Wait and verify all restart
        wait_time = 35

        self.log_step("Step 3: Waiting %ss to verify all automatically restart", wait_time)
        time.sleep(wait_time)

        errors = []
        end_incs = []
        for rank in test_ranks:
            failed = self.server_managers[0].check_rank_state(
                ranks=[rank], valid_states=["joined"], max_checks=1)
            if failed:
                errors.append("Rank %s unexpectedly not restarted when auto-restart enabled"
                              % rank)
            end_incarnation = self.get_rank_incarnation(rank)
            if end_incarnation is None:
                self.fail(f"failed to get end incarnation for rank {rank}")
            end_incs.append(end_incarnation)

        if errors:
            self.fail("\n".join(errors))

        # Show changes
        for idx, (old, new) in enumerate(zip(incs, end_incs)):
            actual_rank = test_ranks[idx]
            if new > old:
                self.log.debug("Rank %s: %s -> %s (restarted)", actual_rank, old, new)
            else:
                self.log.debug("Rank %s: %s -> %s (NOT restarted!)", actual_rank, old, new)

        # Verify all increased
        all_increased = all(a > b for b, a in zip(incs, end_incs))
        if not all_increased:
            self.fail("ERROR: Not all ranks restarted!")

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

        # Get initial incarnation
        initial_incarnation = self.get_rank_incarnation(test_rank)
        if initial_incarnation is None:
            self.fail(f"Failed to get initial incarnation for rank {test_rank}")

        restarted, final_state = self.exclude_rank_and_wait_restart(test_rank)

        if not restarted:
            self.fail(f"Rank {test_rank} did not restart. State: {final_state}")

        # Verify incarnation increased
        final_incarnation = self.get_rank_incarnation(test_rank)
        if final_incarnation is None:
            self.fail(f"Failed to get final incarnation for rank {test_rank}")

        if final_incarnation <= initial_incarnation:
            self.fail(f"Rank {test_rank} incarnation did not increase. "
                      f"Before: {initial_incarnation}, After: {final_incarnation}")

        # Verify pool is still accessible
        self.log_step("Verifying pool is still accessible after rank restart")
        self.pool.query()

        self.log.info("SUCCESS: Rank %s restarted (incarnation %s -> %s) and pool remains "
                      "accessible", test_rank, initial_incarnation, final_incarnation)
