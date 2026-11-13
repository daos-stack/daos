"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers


class StorageFormatStartTest(TestWithServers):
    """Verify dmg storage format --start brings the engines up without initialize.

    :avocado: recursive
    """

    def test_storage_format_start(self):
        """Engines formatted with --start join without dmg system initialize.

        The harness formats with --start and skips dmg system initialize, so
        every rank joining is the whole check.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,system_startup,storage_format_start
        :avocado: tags=StorageFormatStartTest,test_storage_format_start
        """
        self.log_step("Start the servers with dmg storage format --start")
        self.start_servers(start_engines=True)
        ranks = list(self.server_managers[0].ranks.keys())
        self.log_step(f"Ranks {ranks} joined without dmg system initialize")
        if self.server_managers[0].check_rank_state(ranks, ["joined"], 60):
            self.fail("Not every rank joined after dmg storage format --start")
