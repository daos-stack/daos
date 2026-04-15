"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from control_test_base import ControlTestBase
from general_utils import report_errors


class EngineAutoRestartTest(ControlTestBase):
    """Test automatic engine restart on self-termination.

    Test Class Description:
        Verify automatic engine restart behavior when engines self-terminate
        after being excluded from the system.

    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        super().setUp()
        self.dmg = self.get_dmg_command()

    def get_all_ranks(self):
        """Get list of all ranks in the system."""
        return list(self.server_managers[0].ranks.keys())

    def get_rank_state(self, rank):
        """Get the current state of a rank from dmg system query.

        Args:
            rank (int): Rank to query

        Returns:
            str: State of the rank
        """
        data = self.dmg.system_query(ranks=f"{rank}")
        if data["status"] != 0:
            self.fail("Cmd dmg system query failed")
        if "response" in data and "members" in data["response"]:
            if data["response"]["members"] is None:
                self.fail("No members returned from dmg system query")
            for member in data["response"]["members"]:
                return member["state"].lower()
        self.fail("No member state returned from dmg system query")
        return None  # to appease pylint

    def exclude_rank_and_wait_restart(self, rank, expect_restart=True, timeout=30):
        """Exclude a rank and wait for it to self-terminate and potentially restart.

        Args:
            rank (int): Rank to exclude
            expect_restart (bool): Whether automatic restart is expected
            timeout (int): Maximum seconds to wait for restart

        Returns:
            tuple: (restarted, final_state) - whether rank restarted and its final state
        """
        self.log_step(f"Excluding rank {rank}")
        self.dmg.system_exclude(ranks=[rank], rank_hosts=None)

        # Wait for rank to self-terminate (should go to Excluded state)
        self.log_step(f"Waiting for rank {rank} to self-terminate")
        time.sleep(5)

        # Check if rank is excluded
        failed_ranks = self.server_managers[0].check_rank_state(
            ranks=[rank], valid_states=["excluded"], max_checks=10)
        if failed_ranks:
            self.fail(f"Rank {rank} did not reach Excluded state after exclusion")

        if expect_restart:
            # Wait for automatic restart (rank should go to Joined state)
            self.log_step(f"Waiting for rank {rank} to automatically restart")
            start_time = time.time()
            restarted = False

            while time.time() - start_time < timeout:
                time.sleep(2)
                # Check if rank has rejoined
                failed_ranks = self.server_managers[0].check_rank_state(
                    ranks=[rank], valid_states=["joined"], max_checks=1)
                if not failed_ranks:
                    restarted = True
                    break

            if restarted:
                self.log.info(f"Rank {rank} automatically restarted and rejoined")
                return (True, "joined")
            
            state = self.get_rank_state(rank)
            self.log.error("Rank %s (%s) did not restart within %ss", rank, state, timeout)
            return (False, state)
        else:
            # Verify rank stays excluded (no automatic restart)
            self.log_step(f"Verifying rank {rank} does not automatically restart")
            time.sleep(timeout)

            failed_ranks = self.server_managers[0].check_rank_state(
                ranks=[rank], valid_states=["excluded"], max_checks=1)
            if failed_ranks:
                state = self.get_rank_state(rank)
                self.log.error("Rank %s (%s) unexpectedly restarted", rank, state)
                return (True, state)

            return (False, "excluded")

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

        self.log_step(f"Testing automatic restart of rank {test_rank}")

        restarted, final_state = self.exclude_rank_and_wait_restart(test_rank)

        if not restarted:
            self.fail(f"Rank {test_rank} did not automatically restart. Final state: {final_state}")

        self.log.info("SUCCESS: Rank %s automatically restarted after self-termination", test_rank)

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

        self.log_step(f"Testing automatic restart of multiple ranks: {ranks_to_test}")

        errors = []
        results = {}

        for test_rank in ranks_to_test:
            restarted, final_state = self.exclude_rank_and_wait_restart(test_rank)
            results[test_rank] = (restarted, final_state)

            if not restarted:
                errors.append(
                    f"Rank {test_rank} did not automatically restart. State: {final_state}")

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
        self.log.info("Pool service ranks: {%s", pool_svc_ranks)

        # Find a rank not in pool service
        non_svc_ranks = [r for r in all_ranks if r not in pool_svc_ranks]
        if not non_svc_ranks:
            self.skipTest("All ranks are pool service ranks")

        test_rank = self.random.choice(non_svc_ranks)

        self.log_step("Excluding non-service rank %s while pool is active", test_rank)

        restarted, final_state = self.exclude_rank_and_wait_restart(test_rank)

        if not restarted:
            self.fail(f"Rank {test_rank} did not restart. State: {final_state}")

        # Verify pool is still accessible
        self.log_step("Verifying pool is still accessible after rank restart")
        self.pool.query()

        self.log.info(f"SUCCESS: Rank {test_rank} restarted and pool remains accessible")
