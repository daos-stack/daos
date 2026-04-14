"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from control_test_base import ControlTestBase
from general_utils import report_errors


class EngineAutoRestartDisabled(ControlTestBase):
    """Test automatic engine restart disabled configuration.

    Test Class Description:
        Verify that automatic engine restart can be disabled and that
        excluded ranks stay excluded when auto-restart is disabled.

    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        super().setUp()
        self.dmg = self.get_dmg_command()

    def get_all_ranks(self):
        """Get list of all ranks in the system."""
        return list(self.server_managers[0].ranks.keys())

    def test_no_restart_when_disabled(self):
        """Test that engines do not automatically restart when feature is disabled.

        Test Description:
            Server is configured with disable_engine_auto_restart: true.

            1. Exclude a rank from the system
            2. Wait for rank to self-terminate
            3. Wait additional time to verify NO automatic restart occurs
            4. Manually start the rank to verify it can still be started
            5. Verify manual start succeeds

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=dmg,control,engine_auto_restart
        :avocado: tags=EngineAutoRestartDisabled,test_no_restart_when_disabled
        """
        all_ranks = self.get_all_ranks()
        if len(all_ranks) < 2:
            self.skipTest("Test requires at least 2 ranks")

        test_rank = self.random.choice(all_ranks)

        self.log_step(f"Step 1: Excluding rank {test_rank} (auto-restart is DISABLED)")
        self.dmg.system_exclude(ranks=[test_rank], rank_hosts=None)

        # Step 2: Wait for self-termination
        self.log_step(f"Step 2: Waiting for rank {test_rank} to self-terminate")
        time.sleep(5)

        failed_ranks = self.server_managers[0].check_rank_state(
            ranks=[test_rank], valid_states=["excluded"], max_checks=10)
        if failed_ranks:
            self.fail(f"Rank {test_rank} did not reach Excluded state")

        # Step 3: Wait to verify NO automatic restart
        wait_time = 20  # Wait 20 seconds
        self.log_step(f"Step 3: Waiting {wait_time}s to verify NO automatic restart occurs")
        time.sleep(wait_time)

        # Verify rank is still excluded
        failed_ranks = self.server_managers[0].check_rank_state(
            ranks=[test_rank], valid_states=["excluded"], max_checks=1)

        if failed_ranks:
            # Rank is NOT excluded, check if it restarted
            check_joined = self.server_managers[0].check_rank_state(
                ranks=[test_rank], valid_states=["joined"], max_checks=1)
            if not check_joined:
                self.fail(f"Rank {test_rank} unexpectedly restarted when auto-restart disabled!")
            else:
                self.fail(f"Rank {test_rank} in unexpected state (not excluded or joined)")

        self.log.info(f"Confirmed: Rank {test_rank} did NOT automatically restart (as expected)")

        # Step 4: Manually clear exclusion
        self.log_step(f"Step 4: Manually clearing exclusion for rank {test_rank}")
        self.dmg.system_clear_exclude(ranks=[test_rank], rank_hosts=None)

        # Step 5: Manually start the rank
        self.log_step(f"Step 5: Manually starting rank {test_rank}")
        self.dmg.system_start(ranks=f"{test_rank}")

        # Verify manual start succeeds
        failed_ranks = self.server_managers[0].check_rank_state(
            ranks=[test_rank], valid_states=["joined"], max_checks=15)
        if failed_ranks:
            self.fail(f"Manual start of rank {test_rank} failed")

        self.log.info(f"SUCCESS: Rank {test_rank} stayed excluded when auto-restart disabled, "
                      f"and manual start succeeded")

    def test_multiple_ranks_no_restart(self):
        """Test that multiple excluded ranks stay excluded when auto-restart disabled.

        Test Description:
            Server configured with disable_engine_auto_restart: true.

            1. Exclude multiple ranks
            2. Verify all self-terminate and reach Excluded state
            3. Wait to confirm none automatically restart
            4. Manually restart all ranks
            5. Verify all successfully rejoin

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=dmg,control,engine_auto_restart
        :avocado: tags=EngineAutoRestartDisabled,test_multiple_ranks_no_restart
        """
        all_ranks = self.get_all_ranks()
        if len(all_ranks) < 3:
            self.skipTest("Test requires at least 3 ranks")

        # Exclude half the ranks
        num_to_test = max(2, len(all_ranks) // 2)
        test_ranks = self.random.sample(all_ranks, num_to_test)

        self.log_step(f"Step 1: Excluding {num_to_test} ranks: {test_ranks}")

        for rank in test_ranks:
            self.dmg.system_exclude(ranks=[rank], rank_hosts=None)
            time.sleep(1)  # Small delay between exclusions

        # Step 2: Verify all reach Excluded state
        self.log_step("Step 2: Verifying all ranks self-terminate")
        time.sleep(10)

        for rank in test_ranks:
            failed = self.server_managers[0].check_rank_state(
                ranks=[rank], valid_states=["excluded"], max_checks=5)
            if failed:
                self.fail(f"Rank {rank} did not self-terminate")

        # Step 3: Wait and verify none restart
        wait_time = 20
        self.log_step(f"Step 3: Waiting {wait_time}s to verify no automatic restarts")
        time.sleep(wait_time)

        errors = []
        for rank in test_ranks:
            failed = self.server_managers[0].check_rank_state(
                ranks=[rank], valid_states=["excluded"], max_checks=1)
            if failed:
                errors.append(f"Rank {rank} unexpectedly restarted when auto-restart disabled")

        if errors:
            self.fail("\n".join(errors))

        self.log.info(f"Confirmed: None of {test_ranks} automatically restarted")

        # Step 4: Manually clear and restart all
        self.log_step("Step 4: Manually clearing exclusion and restarting ranks")
        self.dmg.system_clear_exclude(ranks=test_ranks, rank_hosts=None)

        for rank in test_ranks:
            self.dmg.system_start(ranks=f"{rank}")

        # Step 5: Verify all rejoin
        self.log_step("Step 5: Verifying all ranks successfully rejoin")
        time.sleep(10)

        for rank in test_ranks:
            failed = self.server_managers[0].check_rank_state(
                ranks=[rank], valid_states=["joined"], max_checks=10)
            if failed:
                errors.append(f"Manual restart of rank {rank} failed")

        report_errors(test=self, errors=errors)

        self.log.info(f"SUCCESS: All {num_to_test} ranks stayed excluded and "
                      f"manual restart succeeded")
