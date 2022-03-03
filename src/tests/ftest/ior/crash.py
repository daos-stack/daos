#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import time

from ior_test_base import IorTestBase
from dmg_utils import check_system_query_status


class IorCrash(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description:
        Verify DAOS server does not need to be restarted when an application crashes.
    :avocado: recursive
    """

    def setUp(self):
        """Set up test before executing."""
        super().setUp()
        self.dmg = self.get_dmg_command()

    def test_ior_crash(self):
        """Jira ID: DAOS-4332.
           Jira ID: DAOS-9946.

        Test Description:
            Verify DAOS server does not need to be restarted when an application crashes.

        Use Cases:
            Run IOR Write.
            Kill IOR process in the middle of Write.
            Verify DAOS engines did not crash.
            Run IOR Write, Read.
            Kill IOR process in the middle of Read.
            Verify DAOS engines did not crash.
            Run IOR Write, Read, CheckRead.
            Verify IOR completes successfully.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=daosio,ior,dfs
        :avocado: tags=ior_crash
        """
        # Run IOR and crash it in the middle of Write
        self.run_ior_with_pool()
        self.check_subprocess_status()
        time.sleep(self.ior_cmd.sw_deadline.value / 2)
        self.stop_ior()

        # Verify engines did not crash
        scan_info = self.dmg.system_query(verbose=True)
        if not check_system_query_status(scan_info):
            self.fail("One or more engines crashed")

        # Run IOR and crash it in the middle of Read.
        # Must wait for Write to complete first.
        self.run_ior_with_pool()
        time.sleep(self.ior_cmd.sw_deadline.value * 1.5)
        self.check_subprocess_status("read")
        self.stop_ior()

        # Verify engines did not crash
        scan_info = self.dmg.system_query(verbose=True)
        if not check_system_query_status(scan_info):
            self.fail("One or more engines crashed")

        # Run IOR and verify it completes successfully
        self.run_ior_with_pool()
        self.job_manager.wait()

        # Verify engines did not crash
        scan_info = self.dmg.system_query(verbose=True)
        if not check_system_query_status(scan_info):
            self.fail("One or more engines crashed")
