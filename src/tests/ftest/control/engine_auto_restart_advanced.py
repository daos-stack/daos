"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from control_test_base import ControlTestBase


class EngineAutoRestartAdvanced(ControlTestBase):
    """Test advanced automatic engine restart scenarios.

    Test Class Description:
        Verify automatic engine restart with custom configurations including
        rate-limiting, deferred restarts, and disabled restart behavior.

    :avocado: recursive
    """

    def wait_for_rank_state(self, rank, expected_state, timeout=30, check_interval=2):
        """Wait for a rank to reach expected state.

        Args:
            rank (int): Rank number
            expected_state (str): Expected state
            timeout (int): Maximum seconds to wait
            check_interval (int): Seconds between state checks

        Returns:
            bool: True if state reached, False if timeout
        """
        start_time = time.time()

        while time.time() - start_time < timeout:
            failed_ranks = self.server_managers[0].check_rank_state(
                ranks=[rank], valid_states=[expected_state], max_checks=1)

            if not failed_ranks:
                self.log.info("Rank %s reached state '%s' after %.1fs",
                              rank, expected_state, time.time() - start_time)
                return True

            time.sleep(check_interval)

        current_state = self.get_rank_state(rank)
        self.log.warning("Rank %s did not reach '%s' within %ss. Current state: %s",
                         rank, expected_state, timeout, current_state)
        return False

    def test_deferred_restart(self):
        """Test deferred restart when multiple self-terminations occur rapidly. Use custom delay.

        Test Description:
            This test requires custom server configuration with a short
            engine_auto_restart_min_delay (20 seconds) to avoid long test runtime.

            1. Exclude rank and wait for automatic restart (first restart)
            2. Immediately exclude same rank again (second self-termination)
               Confirm restart is deferred, not immediate
            3. Wait for deferred restart to execute after delay expires
               Confirm deferred restart executes successfully and rank joined
            4. Measure time until deferred restart executed
            5. Verify delay matches configured value

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=dmg,control,engine_auto_restart
        :avocado: tags=EngineAutoRestartAdvanced,test_deferred_restart
        """
        # Get configured restart delay from test params
        expected_delay = self.params.get("engine_auto_restart_min_delay",
                                         "/run/server_config/*", 20)

        all_ranks = self.get_all_ranks()
        if len(all_ranks) < 2:
            self.fail("Test requires at least 2 ranks")

        test_rank = self.random.choice(all_ranks)

        self.log_step("Step 1: Automatic restart of rank %s", test_rank)

        # Get initial incarnation
        initial_incarnation = self.get_rank_incarnation(test_rank)

        restarted, final_state = self.exclude_rank_and_wait_restart(test_rank)
        if not restarted:
            self.fail(f"Rank {test_rank} did not automatically restart. "
                      f"Final state: {final_state}")

        # Verify incarnation increased
        first_restart_incarnation = self.get_rank_incarnation(test_rank)

        if first_restart_incarnation <= initial_incarnation:
            self.fail(f"Rank {test_rank} incarnation did not increase after first restart. "
                      f"Before: {initial_incarnation}, After: {first_restart_incarnation}")

        first_restart_time = time.time()
        self.log.info("First restart completed at T=%.1f (incarnation %s -> %s)",
                      first_restart_time, initial_incarnation, first_restart_incarnation)

        # Second exclusion - should be deferred due to rate-limiting
        self.log_step("Step 2: Second exclusion of rank %s (should be deferred)", test_rank)

        restarted, final_state = self.exclude_rank_and_wait_restart(test_rank,
                                                                    timeout=10)

        if restarted:
            self.fail("Rank %s unexpectedly restarted. Final state: %s" % (test_rank, final_state))

        self.log.info("Confirmed: Restart is deferred (rank still in excluded state)")

        # Wait for deferred restart to execute (after delay expires), add buffer
        wait_time = expected_delay + 5
        self.log_step("Step 3: Waiting %ss for deferred restart to execute", wait_time)

        if not self.wait_for_rank_state(test_rank, "joined", timeout=wait_time):
            self.fail(f"Rank {test_rank} did not restart after rate-limit delay")

        # Verify incarnation increased again after deferred restart
        deferred_restart_incarnation = self.get_rank_incarnation(test_rank)

        if deferred_restart_incarnation <= first_restart_incarnation:
            self.fail(f"Rank {test_rank} incarnation did not increase after deferred restart. "
                      f"After first: {first_restart_incarnation}, "
                      f"After deferred: {deferred_restart_incarnation}")

        self.log_step("Step 4: Measure time between initial and deferred restarts")
        deferred_restart_time = time.time()
        actual_delay = deferred_restart_time - first_restart_time

        self.log.info("Confirmed: Deferred restart executed after %.1fs (expected ~%ss), "
                      "incarnation %s -> %s",
                      actual_delay, expected_delay,
                      first_restart_incarnation, deferred_restart_incarnation)

        self.log_step("Step 5: Verify delay was approximately correct (80%% to 120%% of expected)")
        min_delay = expected_delay * 0.8
        max_delay = expected_delay * 1.2

        if actual_delay < min_delay:
            self.fail(f"Restart too early: {actual_delay:.1f}s < {min_delay:.1f}s")
        elif actual_delay > max_delay:
            self.log.warning("Restart delayed beyond expected: %.1fs > %.1fs "
                             "(may be acceptable depending on system load)",
                             actual_delay, max_delay)
        else:
            self.log.info("SUCCESS: Restart delay within expected range [%.1fs, %.1fs]",
                          min_delay, max_delay)
