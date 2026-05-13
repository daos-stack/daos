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
        """Set up for engine_auto_restart_disabled tests"""
        super().setUp()

        # Make sure we reset the restart state even if the test fails
        self.register_cleanup(self.reset_engine_restart_state)

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
            self.fail("Test requires at least 2 ranks")

        test_rank = self.random.choice(all_ranks)

        self.log_step("Excluding rank {test_rank} (auto-restart is DISABLED)")

        restarted, _ = self.exclude_rank_and_wait_restart(test_rank, timeout=35)

        if restarted:
            self.fail("Rank {test_rank} unexpectedly restarted when auto-restart disabled!")

        self.log.info("Confirmed: Rank %s did NOT automatically restart (as expected)", test_rank)

        # Manually start the rank
        self.log_step("Manually starting rank {test_rank}")
        self.dmg.system_start(ranks=f"{test_rank}")

        # Verify manual start succeeds
        failed_ranks = self.server_managers[0].check_rank_state(
            ranks=[test_rank], valid_states=["joined"], max_checks=15)
        if failed_ranks:
            self.fail(f"Manual start of rank {test_rank} failed")

        self.log.info("SUCCESS: Rank %s stayed excluded when auto-restart disabled, and manual "
                      "start succeeded", test_rank)

    def test_multiple_ranks_no_restart(self):
        """Test that multiple excluded ranks stay excluded when auto-restart disabled.

        Test Description:
            Server configured with disable_engine_auto_restart: true.

            1. Exclude multiple ranks
            2. Verify all self-terminate and reach AdminExcluded state
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
            self.fail("Test requires at least 3 ranks")

        # Exclude half the ranks
        num_to_test = max(2, len(all_ranks) // 2)
        test_ranks = self.random.sample(all_ranks, num_to_test)

        self.log_step("Excluding {num_to_test} ranks: {test_ranks}")

        for rank in test_ranks:
            self.dmg.system_exclude(ranks=[rank], rank_hosts=None)
            time.sleep(1)  # Small delay between exclusions

        # Verify all reach adminexcluded state
        self.log_step("Verifying all ranks get excluded from system")
        time.sleep(10)

        for rank in test_ranks:
            failed = self.server_managers[0].check_rank_state(
                ranks=[rank], valid_states=["adminexcluded"], max_checks=5)
            if failed:
                self.fail("Rank {rank} did not get excluded from system")
            self.dmg.system_clear_exclude(ranks=[rank], rank_hosts=None)

        # Wait and verify none restart
        wait_time = 20
        self.log_step("Waiting {wait_time}s to verify no automatic restarts")
        time.sleep(wait_time)

        errors = []
        for rank in test_ranks:
            failed = self.server_managers[0].check_rank_state(
                ranks=[rank], valid_states=["excluded"], max_checks=1)
            if failed:
                errors.append("Rank {rank} unexpectedly restarted when auto-restart disabled")

        if errors:
            self.fail("\n".join(errors))

        self.log.info("Confirmed: None of %s automatically restarted", test_ranks)

        # Manually restart all
        self.log_step("Manually restart ranks")

        for rank in test_ranks:
            self.dmg.system_start(ranks=f"{rank}")

        # Verify all rejoin
        self.log_step("Verifying all ranks successfully rejoin")
        time.sleep(10)

        for rank in test_ranks:
            failed = self.server_managers[0].check_rank_state(
                ranks=[rank], valid_states=["joined"], max_checks=10)
            if failed:
                errors.append(f"Manual restart of rank {rank} failed")

        report_errors(test=self, errors=errors)

        self.log.info("SUCCESS: All %s ranks stayed excluded and manual restart succeeded",
                      num_to_test)
