"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers
from general_utils import report_errors
from exception_utils import CommandFailure


class Pass0Test(TestWithServers):
    """Test Pass 0: Management Service & Membership

    :avocado: recursive
    """

    def test_checker_on_admin_excluded(self):
        """Test checker can only be run when the system status is AdminExcluded.

        1. Call dmg check enable.
        2. Stop rank 1.
        3. Call dmg system query and check that status of at least one rank is not
        "checkerstarted". We verify that the new checkerstarted state is properly changed.
        4. Call dmg check start. It should show error because the stopped rank is not at
        CheckerStarted state.
        5. Call dmg check query. It should show error because the stopped rank is not at
        CheckerStarted state.
        6. Call dmg check disable. It should work.
        7. Restart the stopped rank for cleanup.

        Jira ID: DAOS-11703

        :avocado: tags=all,weekly_regression
        :avocado: tags=vm
        :avocado: tags=recovery,pass_0
        :avocado: tags=test_checker_on_admin_excluded
        """
        dmg_command = self.get_dmg_command()

        # 1. Call dmg check enable.
        dmg_command.check_enable()

        # 2. Stop rank 1.
        dmg_command.system_stop(ranks="1")

        # 3. Call dmg system query and check that status of at least one rank is not
        # "checkerstarted". We verify that the new checkerstarted state is properly
        # changed.
        query_out = dmg_command.system_query()
        not_checkerstarted_found = False
        for member in query_out["response"]["members"]:
            if member["state"] != "checkerstarted":
                not_checkerstarted_found = True
                break
        if not not_checkerstarted_found:
            # All rank's status is "checkerstarted".
            self.fail("All rank's status is checkerstarted!")

        # 4. Call dmg check start. It should show error because the stopped rank is not at
        # CheckerStarted state.
        try:
            dmg_command.check_start()
        except CommandFailure as error:
            self.log.info("dmg check start is expected to fail. Error: %s", error)

        # 5. Call dmg check query. It should show error because the stopped rank is not at
        # CheckerStarted state.
        try:
            dmg_command.check_query()
        except CommandFailure as error:
            self.log.info("dmg check query is expected to fail. Error: %s", error)

        # 6. Call dmg check disable. It should work.
        dmg_command.check_disable()

        # 7. Restart the stopped rank for cleanup.
        self.log.info("Restart stopped rank for cleanup.")
        dmg_command.system_start(ranks="1")

    def test_enable_disable_admin_excluded(self):
        """Test admin can enable and disable the rank state to AdminExcluded when the
        rank is down.

        1. Stop rank 1.
        2. Set rank 1 to AdminExcluded state by calling dmg system exclude --ranks=1.
        3. Verify that the rank 1 state is AdminExluded by calling dmg system query.
        4. Verify that the checker can be run with AdminExcluded state by calling enable,
        start, query, and disable. Verify that none of the commands returns error.
        5. Disable AdminExcluded by calling dmg system clear-exclude --ranks=1
        6. Verify that the rank 1 state is Excluded.
        7. Start rank 1 for clean up. (At this point, its state is "Excluded".)

        Jira ID: DAOS-11704

        :avocado: tags=all,weekly_regression
        :avocado: tags=vm
        :avocado: tags=recovery,pass_0
        :avocado: tags=test_enable_disable_admin_excluded
        """
        errors = []
        dmg_command = self.get_dmg_command()

        # 1. Stop rank 1.
        dmg_command.system_stop(ranks="1")

        # 2. Set rank 1 to AdminExcluded state.
        dmg_command.system_exclude(ranks="1", rank_hosts=None)

        # 3. Verify that the rank 1 state is AdminExluded.
        query_out = dmg_command.system_query()
        admin_excluded_found = False
        rank_1_state = None
        for member in query_out["response"]["members"]:
            if member["rank"] == 1:
                rank_1_state = member["state"]
                if rank_1_state == "adminexcluded":
                    admin_excluded_found = True
                break
        if not admin_excluded_found:
            self.fail("Rank 1 state is not AdminExcluded!: {}".format(rank_1_state))

        # 4. Verify that the checker can be run with AdminExcluded state.
        try:
            dmg_command.check_enable()
            dmg_command.check_start()
            dmg_command.check_query()
            dmg_command.check_disable()
        except CommandFailure as error:
            msg = f"dmg check command failed! {error}"
            errors.append(msg)

        # 5. Disable AdminExcluded of rank 1.
        dmg_command.system_clear_exclude(ranks="1", rank_hosts=None)

        # 6. Verify that the rank 1 state is Excluded.
        query_out = dmg_command.system_query()
        excluded_found = False
        rank_1_state = None
        for member in query_out["response"]["members"]:
            if member["rank"] == 1:
                rank_1_state = member["state"]
                if rank_1_state == "excluded":
                    excluded_found = True
                break
        if not excluded_found:
            errors.append("Rank 1 state is not Excluded! {}".format(rank_1_state))

        # 7. Start rank 1 for cleanup.
        dmg_command.system_start(ranks="1")

        report_errors(test=self, errors=errors)
