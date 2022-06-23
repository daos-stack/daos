#!/usr/bin/python
"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""


from control_test_base import ControlTestBase

class DmgServerSetLogmasksTest(ControlTestBase):
    # pylint: disable=too-many-ancestors
    """Test Class Description:
    Verify the server set-logmasks function of the dmg tool.
    :avocado: recursive
    """

    def test_dmg_server_set_logmasks(self):
        """
        JIRA ID: DAOS-10900
        Test Description: Test that server set-logmasks command completes successfully.
        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=basic,control,dmg
        :avocado: tags=test_dmg_server_set_logmasks
        """
        result = self.dmg.server_set_logmasks()

        status = result["status"]
        self.assertEqual(status, 0, "bad return status")
