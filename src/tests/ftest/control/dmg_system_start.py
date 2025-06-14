"""
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from control_test_base import ControlTestBase
from general_utils import list_to_str


class DmgSystemStartTest(ControlTestBase):
    """Test dmg system start command.

    Test Class Description:
        Verify parameters of dmg system start command.

    :avocado: recursive
    """

    def all_ranks(self):
        'Get a list of all ranks in the system.'
        return list(self.server_managers[0].ranks.keys())

    def random_rank(self):
        'Select a random rank from the list of ranks.'
        return self.random.choice(self.all_ranks())

    def start_ranks(self, ranks=None, ignore_admin_excluded=False):
        'Start requested ranks and log a helpful debug message.'
        if ranks:
            self.log_step(f"Starting ranks {ranks}")
            ranks = list_to_str(value=ranks)
        else:
            self.log_step("Starting all ranks")
        return self.dmg.system_start(ranks=ranks, ignore_admin_excluded=ignore_admin_excluded)

    def test_system_start_excluded_rank(self):
        """
        Test starting the system with ranks admin excluded.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dmg,control
        :avocado: tags=DmgSystemStartTest,test_system_start_excluded_rank
        """
        exclude_rank = self.random_rank()

        self.log_step(f"Setup: Excluding rank: {exclude_rank}")
        self.dmg.system_exclude(ranks=[exclude_rank], rank_hosts=None)

        self.log_step("Setup: Stopping all ranks")
        self.dmg.system_stop()
        self.log_step("Starting all ranks including excluded rank - expect failure")
        with self.dmg.no_exception():
            self.start_ranks(ranks=self.all_ranks())
            self.log_step(self.dmg.result)
            self.assertNotEqual(
                self.dmg.result.exit_status, 0,
                "start admin-excluded rank without --ignore-admin-excluded should fail"
            )

        self.log_step("Starting all ranks including excluded rank with \
                      --ignore-admin-excluded - expect success")
        self.start_ranks(ranks=self.all_ranks(), ignore_admin_excluded=True)

        self.log_step("Stopping all ranks")
        self.dmg.system_stop()

        self.log_step("Starting system without specifying ranks - expect success")
        self.start_ranks()

        self.log_step("Clearing admin-excluded state")
        self.dmg.system_clear_exclude(ranks=[exclude_rank], rank_hosts=None)

        self.log_step("Restarting previously-excluded rank")
        self.start_ranks(ranks=[exclude_rank])
