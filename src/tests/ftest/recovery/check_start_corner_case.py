"""
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from apricot import TestWithServers
from exception_utils import CommandFailure


class DMGCheckStartCornerCaseTest(TestWithServers):
    """Test dmg check start corner cases.

    :avocado: recursive
    """

    def test_start_single_pool(self):
        """Test dmg check start corner cases with single healthy pool.

        1. Create a pool and enable checker.
        2. Start with the pool label. It should not detect any fault.
        3. Start with non-existing pool label. Verify error message.

        Jira ID: DAOS-17820

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=recovery,cat_recov
        :avocado: tags=DMGCheckStartCornerCaseTest,test_start_single_pool
        """
        # 1. Create a pool and enable checker.
        self.log_step("Create a pool and enable checker.")
        pool = self.get_pool(connect=False)
        dmg_command = self.get_dmg_command()
        dmg_command.check_enable()

        # 2. Start with the pool label. It should not detect any fault.
        self.log_step("Start with the pool label. It should not detect any fault.")
        dmg_command.check_start(pool=pool.identifier)
        query_reports = None
        for _ in range(8):
            check_query_out = dmg_command.check_query()
            if check_query_out["response"]["status"] == "COMPLETED":
                query_reports = check_query_out["response"]["reports"]
                break
            time.sleep(5)
        if query_reports:
            msg = (f"dmg check start with healthy pool detected inconsistency! "
                   f"{query_reports}")
            self.fail(msg)

        # 3. Start with non-existing pool label. Verify error message.
        self.log_step("Start with non-existing pool label. Verify error message.")
        try:
            dmg_command.check_start(pool="invalid_label")
            self.fail("dmg check start invalid_label worked!")
        except CommandFailure as command_failure:
            if "unable to find pool service" not in str(command_failure):
                msg = (f"dmg check start invalid_label didn't return expected message! "
                       f"{command_failure}")
                self.fail(msg)
            self.log.info("dmg check start invalid_label failed as expected.")

        dmg_command.check_disable()
