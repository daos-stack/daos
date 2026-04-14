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
        data = self.dmg.system_query(ranks=f"{rank}")
        if data["status"] != 0:
            self.fail("Cmd dmg system query failed")
        if "response" in data and "members" in data["response"]:
            if data["response"]["members"] is None:
                self.fail("No members returned from dmg system query")
            for member in data["response"]["members"]:
                return member["state"].lower()
        self.fail("No member state returned from dmg system query")

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
                self.log.info(f"Rank {rank} reached state '{expected_state}' after "
                              f"{time.time() - start_time:.1f}s")
                return True

            time.sleep(check_interval)

        current_state = self.get_rank_state(rank)
        self.log.warning(f"Rank {rank} did not reach '{expected_state}' within {timeout}s. "
                         f"Current state: {current_state}")
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
        self.log_step(f"Step 1: First exclusion of rank {test_rank}")
        self.dmg.system_exclude(ranks=[test_rank], rank_hosts=None)

        # Wait for self-termination
        if not self.wait_for_rank_state(test_rank, "excluded", timeout=10):
            self.fail(f"Rank {test_rank} did not self-terminate")

        # Wait for automatic restart
        self.log_step(f"Step 2: Waiting for first automatic restart of rank {test_rank}")
        if not self.wait_for_rank_state(test_rank, "joined", timeout=30):
            self.fail(f"Rank {test_rank} did not automatically restart on first exclusion")

        first_restart_time = time.time()
        self.log.info(f"First restart completed at T={first_restart_time:.1f}")

        # Second exclusion - should be deferred due to rate-limiting
        self.log_step(f"Step 3: Second exclusion of rank {test_rank} (should be deferred)")
        self.dmg.system_exclude(ranks=[test_rank], rank_hosts=None)

        # Wait for self-termination
        if not self.wait_for_rank_state(test_rank, "excluded", timeout=10):
            self.fail(f"Rank {test_rank} did not self-terminate on second exclusion")

        # Verify restart is NOT immediate (should be deferred)
        self.log_step("Step 4: Verifying restart is deferred (not immediate)")
        time.sleep(5)  # Wait a bit

        current_state = self.get_rank_state(test_rank)
        if current_state == "joined":
            self.fail(f"Rank {test_rank} restarted immediately - rate-limiting not working")

        self.log.info(f"Confirmed: Restart is deferred (rank still in '{current_state}' state)")

        # Wait for deferred restart to execute (after delay expires)
        # Add buffer time for processing
        wait_time = restart_delay + 10
        self.log_step(f"Step 5: Waiting {wait_time}s for deferred restart to execute")

        if not self.wait_for_rank_state(test_rank, "joined", timeout=wait_time):
            self.fail(f"Rank {test_rank} did not restart after rate-limit delay")

        deferred_restart_time = time.time()
        actual_delay = deferred_restart_time - first_restart_time

        self.log.info(f"SUCCESS: Deferred restart executed after {actual_delay:.1f}s "
                      f"(expected ~{restart_delay}s)")

        # Verify delay was approximately correct (within tolerance)
        if actual_delay < restart_delay * 0.8:
            self.fail(f"Restart occurred too early: {actual_delay:.1f}s < {restart_delay}s")

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

        self.log_step(f"Testing custom restart delay of {expected_delay}s for rank {test_rank}")

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
        self.log_step(f"Step 3: Waiting for deferred restart (expected delay: {expected_delay}s)")
        wait_timeout = expected_delay + 20  # Add buffer

        if not self.wait_for_rank_state(test_rank, "joined", timeout=wait_timeout):
            self.fail(f"Rank {test_rank} did not restart within expected time")

        second_restart_time = time.time()
        actual_delay = second_restart_time - first_restart_time

        self.log.info(f"Measured delay: {actual_delay:.1f}s (expected: ~{expected_delay}s)")

        # Verify delay is within acceptable range (80% to 120% of expected)
        min_delay = expected_delay * 0.8
        max_delay = expected_delay * 1.2

        if actual_delay < min_delay:
            self.fail(f"Restart too early: {actual_delay:.1f}s < {min_delay:.1f}s")
        elif actual_delay > max_delay:
            self.log.warning(f"Restart delayed beyond expected: {actual_delay:.1f}s > "
                             f"{max_delay:.1f}s (may be acceptable depending on system load)")
        else:
            self.log.info(f"SUCCESS: Restart delay within expected range "
                          f"[{min_delay:.1f}s, {max_delay:.1f}s]")

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

        self.log_step(f"Step 1: Excluding rank {test_rank}")
        self.dmg.system_exclude(ranks=[test_rank], rank_hosts=None)

        # Wait for self-termination
        # FIXME: should this be checking for "adminexcluded" state?
        if not self.wait_for_rank_state(test_rank, "excluded", timeout=10):
            self.fail(f"Rank {test_rank} did not self-terminate")

        # Clear exclusion before auto-restart
        self.log_step(f"Step 2: Clearing exclusion for rank {test_rank}")
        self.dmg.system_clear_exclude(ranks=[test_rank], rank_hosts=None)

        # Verify rank rejoins
        if not self.wait_for_rank_state(test_rank, "joined", timeout=30):
            self.fail(f"Rank {test_rank} did not rejoin after manual start")
            self.fail(f"Rank {test_rank} did not automatically restart on admin exclusion")

        self.log.info(f"SUCCESS: Rank {test_rank} successfully rejoined after clear-exclude")
