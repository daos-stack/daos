"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from control_test_base import ControlTestBase


class DmgSupportCollectLogTest(ControlTestBase):
    # pylint: disable=too-many-ancestors
    """Test Class Description:
    Verify the support collect-log function of the dmg tool.
    :avocado: recursive
    """

    def test_dmg_support_collect_log(self):
        """JIRA ID: DAOS-10625
        Test Description: Test that support collect-log command completes successfully.
        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=basic,control,dmg
        :avocado: tags=test_dmg_support_collect_log
        """
        result = self.dmg.support_collect_log()
        status = result["status"]
        self.assertEqual(status, 0, "bad return status")
