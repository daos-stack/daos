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

        self.log_step("Step 1: Excluding rank %s (auto-restart is DISABLED)", test_rank)

        restarted, _ = self.exclude_rank_and_wait_restart(test_rank, timeout=35)

        if restarted:
            self.fail("Rank %s unexpectedly restarted when auto-restart disabled!" % test_rank)

        self.log.info("Confirmed: Rank %s did NOT automatically restart (as expected)", test_rank)

        # Step 4: Manually start the rank
        self.log_step("Step 2: Manually starting rank %s", test_rank)
        self.dmg.system_start(ranks=f"{test_rank}")

        # Verify manual start succeeds
        failed_ranks = self.server_managers[0].check_rank_state(
            ranks=[test_rank], valid_states=["joined"], max_checks=15)
        if failed_ranks:
            self.fail("Manual start of rank %s failed" % test_rank)

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

        # Step 3: Wait and verify none restart
        wait_time = 20
        self.log_step("Step 3: Waiting %ss to verify no automatic restarts", wait_time)
        time.sleep(wait_time)

        errors = []
        for rank in test_ranks:
            failed = self.server_managers[0].check_rank_state(
                ranks=[rank], valid_states=["excluded"], max_checks=1)
            if failed:
                errors.append("Rank %s unexpectedly restarted when auto-restart disabled"
                              % rank)

        if errors:
            self.fail("\n".join(errors))

        self.log.info("Confirmed: None of %s automatically restarted", test_ranks)

        # Step 4: Manually restart all
        self.log_step("Step 4: Manually restart ranks")

        for rank in test_ranks:
            self.dmg.system_start(ranks=f"{rank}")

        # Step 5: Verify all rejoin
        self.log_step("Step 5: Verifying all ranks successfully rejoin")
        time.sleep(10)

        for rank in test_ranks:
            failed = self.server_managers[0].check_rank_state(
                ranks=[rank], valid_states=["joined"], max_checks=10)
            if failed:
                errors.append("Manual restart of rank %s failed" % rank)

        report_errors(test=self, errors=errors)

        self.log.info("SUCCESS: All %s ranks stayed excluded and manual restart succeeded",
                      num_to_test)
