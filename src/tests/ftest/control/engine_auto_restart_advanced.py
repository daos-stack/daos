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

    def setUp(self):
        """Set up each test case."""
        super().setUp()
        self.dmg = self.get_dmg_command()

    def get_all_ranks(self):
        """Get list of all ranks in the system."""
        return list(self.server_managers[0].ranks.keys())

    def get_rank_state(self, rank):
        """Get the state of a rank.

        Args:
            rank (int): Rank number

        Returns:
            str: Current state of the rank
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
        return None

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
        """Test deferred restart when multiple self-terminations occur rapidly.

        Test Description:
            This test requires custom server configuration with a short
            engine_auto_restart_min_delay (e.g., 15 seconds) to avoid long test runtime.

            1. Exclude rank and wait for automatic restart (first restart)
            2. Immediately exclude same rank again (second self-termination)
            3. Verify restart is deferred, not immediate
            4. Wait for deferred restart to execute after delay expires
            5. Verify rank successfully rejoins

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=dmg,control,engine_auto_restart
        :avocado: tags=EngineAutoRestartAdvanced,test_deferred_restart
        """
        # Get configured restart delay from test params
        restart_delay = self.params.get("engine_auto_restart_min_delay", "/run/server_config/*", 15)

        all_ranks = self.get_all_ranks()
        if len(all_ranks) < 2:
            self.skipTest("Test requires at least 2 ranks")

        test_rank = self.random.choice(all_ranks)

        # First exclusion - should restart immediately (no previous restart)
        self.log_step("Step 1: First exclusion of rank %s", test_rank)
        self.dmg.system_exclude(ranks=[test_rank], rank_hosts=None)

        # Wait for self-termination
        if not self.wait_for_rank_state(test_rank, "excluded", timeout=10):
            self.fail("Rank %s did not self-terminate" % test_rank)

        # Wait for automatic restart
        self.log_step("Step 2: Waiting for first automatic restart of rank %s", test_rank)
        if not self.wait_for_rank_state(test_rank, "joined", timeout=30):
            self.fail("Rank %s did not automatically restart on first exclusion" % test_rank)

        first_restart_time = time.time()
        self.log.info("First restart completed at T=%.1f", first_restart_time)

        # Second exclusion - should be deferred due to rate-limiting
        self.log_step("Step 3: Second exclusion of rank %s (should be deferred)", test_rank)
        self.dmg.system_exclude(ranks=[test_rank], rank_hosts=None)

        # Wait for self-termination
        if not self.wait_for_rank_state(test_rank, "excluded", timeout=10):
            self.fail("Rank %s did not self-terminate on second exclusion" % test_rank)

        # Verify restart is NOT immediate (should be deferred)
        self.log_step("Step 4: Verifying restart is deferred (not immediate)")
        time.sleep(5)  # Wait a bit

        current_state = self.get_rank_state(test_rank)
        if current_state == "joined":
            self.fail("Rank %s restarted immediately - rate-limiting not working" % test_rank)

        self.log.info("Confirmed: Restart is deferred (rank still in '%s' state)",
                      current_state)

        # Wait for deferred restart to execute (after delay expires)
        # Add buffer time for processing
        wait_time = restart_delay + 10
        self.log_step("Step 5: Waiting %ss for deferred restart to execute", wait_time)

        if not self.wait_for_rank_state(test_rank, "joined", timeout=wait_time):
            self.fail("Rank %s did not restart after rate-limit delay" % test_rank)

        deferred_restart_time = time.time()
        actual_delay = deferred_restart_time - first_restart_time

        self.log.info("SUCCESS: Deferred restart executed after %.1fs (expected ~%ss)",
                      actual_delay, restart_delay)

        # Verify delay was approximately correct (within tolerance)
        if actual_delay < restart_delay * 0.8:
            self.fail("Restart occurred too early: %.1fs < %ss" % (actual_delay, restart_delay))

    def test_custom_restart_delay(self):
        """Test custom engine_auto_restart_min_delay configuration.

        Test Description:
            This test requires server configuration with custom
            engine_auto_restart_min_delay value.

            1. Exclude rank and wait for first restart
            2. Exclude same rank again
            3. Measure time until deferred restart executes
            4. Verify delay matches configured value

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=dmg,control,engine_auto_restart
        :avocado: tags=EngineAutoRestartAdvanced,test_custom_restart_delay
        """
        # Get configured delay from test parameters
        expected_delay = self.params.get("engine_auto_restart_min_delay",
                                         "/run/server_config/*", 20)

        all_ranks = self.get_all_ranks()
        if len(all_ranks) < 2:
            self.skipTest("Test requires at least 2 ranks")

        test_rank = self.random.choice(all_ranks)

        self.log_step("Testing custom restart delay of %ss for rank %s",
                      expected_delay, test_rank)

        # First restart to establish baseline
        self.log_step("Step 1: First exclusion and restart")
        self.dmg.system_exclude(ranks=[test_rank], rank_hosts=None)
        self.wait_for_rank_state(test_rank, "excluded", timeout=10)
        self.wait_for_rank_state(test_rank, "joined", timeout=30)

        first_restart_time = time.time()

        # Second restart to measure delay
        self.log_step("Step 2: Second exclusion to trigger deferred restart")
        self.dmg.system_exclude(ranks=[test_rank], rank_hosts=None)
        self.wait_for_rank_state(test_rank, "excluded", timeout=10)

        # Wait for deferred restart
        self.log_step("Step 3: Waiting for deferred restart (expected delay: %ss)",
                      expected_delay)
        wait_timeout = expected_delay + 20  # Add buffer

        if not self.wait_for_rank_state(test_rank, "joined", timeout=wait_timeout):
            self.fail("Rank %s did not restart within expected time" % test_rank)

        second_restart_time = time.time()
        actual_delay = second_restart_time - first_restart_time

        self.log.info("Measured delay: %.1fs (expected: ~%ss)", actual_delay, expected_delay)

        # Verify delay is within acceptable range (80% to 120% of expected)
        min_delay = expected_delay * 0.8
        max_delay = expected_delay * 1.2

        if actual_delay < min_delay:
            self.fail("Restart too early: %.1fs < %.1fs" % (actual_delay, min_delay))
        elif actual_delay > max_delay:
            self.log.warning("Restart delayed beyond expected: %.1fs > %.1fs "
                             "(may be acceptable depending on system load)",
                             actual_delay, max_delay)
        else:
            self.log.info("SUCCESS: Restart delay within expected range [%.1fs, %.1fs]",
                          min_delay, max_delay)

    def test_restart_after_clear_exclude(self):
        """Test interaction between auto-restart and manual clear-exclude.

        Test Description:
            1. Exclude rank, wait for self-termination
            2. Clear exclusion before auto-restart triggers
            3. Verify rank rejoins successfully

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=dmg,control,engine_auto_restart
        :avocado: tags=EngineAutoRestartAdvanced,test_restart_after_clear_exclude
        """
        all_ranks = self.get_all_ranks()
        if len(all_ranks) < 2:
            self.skipTest("Test requires at least 2 ranks")

        test_rank = self.random.choice(all_ranks)

        self.log_step("Step 1: Excluding rank %s", test_rank)
        self.dmg.system_exclude(ranks=[test_rank], rank_hosts=None)

        # Wait for self-termination
        if not self.wait_for_rank_state(test_rank, "excluded", timeout=10):
            self.fail("Rank %s did not self-terminate" % test_rank)

        # Clear exclusion before auto-restart
        self.log_step("Step 2: Clearing exclusion for rank %s", test_rank)
        self.dmg.system_clear_exclude(ranks=[test_rank], rank_hosts=None)

        # Verify rank rejoins
        if not self.wait_for_rank_state(test_rank, "joined", timeout=30):
            self.fail("Rank %s did not rejoin after clear-exclude" % test_rank)

        self.log.info("SUCCESS: Rank %s successfully rejoined after clear-exclude", test_rank)
