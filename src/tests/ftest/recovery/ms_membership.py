"""
  (C) Copyright 2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers
from exception_utils import CommandFailure
from general_utils import report_errors


class MSMembershipTest(TestWithServers):
    """Test Pass 0: Management Service & Membership

    :avocado: recursive
    """

    def test_checker_on_admin_excluded(self):
        """Test checker can only be run when the system status is AdminExcluded.

        1. Call dmg check enable.
        2. Stop 1 random rank
        3. Call dmg system query and check that status of at least one rank is not
        "checkerstarted". We verify that the new checkerstarted state is properly changed.
        4. Call dmg check start. It should show error because the stopped rank is not at
        CheckerStarted state.
        5. Call dmg check query. It should show error because the stopped rank is not at
        CheckerStarted state.
        6. Call dmg check disable. It should work.
        7. Restart the stopped rank for cleanup.

        Jira ID: DAOS-11703

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,cat_recov,ms_membership
        :avocado: tags=MSMembershipTest,test_checker_on_admin_excluded
        """
        dmg_command = self.get_dmg_command()

        # 1. Call dmg check enable.
        dmg_command.check_enable()

        # 2. Stop 1 rank
        rank_to_stop = self.random.choice(list(self.server_managers[0].ranks.keys()))
        dmg_command.system_stop(ranks=rank_to_stop)

        # 3. Call dmg system query and check that status of at least one rank is not
        # "checkerstarted". We verify that the new checkerstarted state is properly
        # changed.
        query_out = dmg_command.system_query()
        all_states = set(member["state"] for member in query_out["response"]["members"])
        # TODO actually verify each rank is in the expected state
        if all_states == set(("checkerstarted")):
            # All rank's status is "checkerstarted".
            self.fail("All rank's status is checkerstarted!")

        # 4. Call dmg check start. It should show error because the stopped rank is not at
        # CheckerStarted state.
        try:
            dmg_command.check_start()
            self.fail("dmg check start did not fail as expected")
        except CommandFailure as error:
            self.log.info("dmg check start is expected to fail. Error: %s", error)

        # 5. Call dmg check query. It should show error because the stopped rank is not at
        # CheckerStarted state.
        try:
            dmg_command.check_query()
            self.fail("dmg check query did not fail as expected")
        except CommandFailure as error:
            self.log.info("dmg check query is expected to fail. Error: %s", error)

        # 6. Call dmg check disable. It should work.
        dmg_command.check_disable()

        # 7. Restart the stopped rank for cleanup.
        self.log.info("Restart stopped rank for cleanup.")
        dmg_command.system_start(ranks=rank_to_stop)

    def test_enable_disable_admin_excluded(self):
        """Test dmg system exclude and clear-exclude.

        Test admin can enable and disable the rank state to AdminExcluded when the rank is
        down.

        1. Stop 1 random rank
        2. Set the stopped rank to AdminExcluded by calling dmg system exclude --ranks=1 and verify
        the state has been changed.
        3. Verify that the checker can be run with AdminExcluded state by calling enable,
        start, query, and disable. Verify that none of the commands returns error.
        4. Disable AdminExcluded of the excluded rank by calling dmg system clear-exclude
        and verify the state has been changed.
        5. Start the ranks again

        Jira ID: DAOS-11704

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery,cat_recov,ms_membership
        :avocado: tags=MSMembershipTest,test_enable_disable_admin_excluded
        """
        errors = []
        dmg_command = self.get_dmg_command()

        self.log_step("Stop 1 random rank")
        rank_to_stop = self.random.choice(list(self.server_managers[0].ranks.keys()))
        dmg_command.system_stop(ranks=rank_to_stop)

        self.log_step("Set the stopped rank to AdminExcluded")
        self.server_managers[-1].system_exclude(ranks=[rank_to_stop])

        self.log_step("Verify that the checker can be run with AdminExcluded state.")
        try:
            dmg_command.check_enable()
            dmg_command.check_start()
            dmg_command.check_query()
            # We need to start after calling dmg system clear-exclude, otherwise the start
            # command will hang.
            dmg_command.check_disable(start=False)
        except CommandFailure as error:
            msg = f"dmg check command failed! {error}"
            errors.append(msg)

        self.log_step(
            "Disable AdminExcluded of the stopped rank and verify the state has been changed.")
        self.server_managers[-1].system_clear_exclude(ranks=[rank_to_stop])

        self.log_step("Verify all ranks are able to restart")
        dmg_command.system_start()
        all_ranks = list(self.server_managers[0].ranks.keys())
        self.server_managers[-1].update_expected_states(all_ranks, ['joined'])
        self.server_managers[-1].check_rank_state(all_ranks, ['joined'])

        report_errors(test=self, errors=errors)
