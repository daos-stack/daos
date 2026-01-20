"""
  (C) Copyright 2023 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from upgrade_downgrade_base import UpgradeDowngradeBase


class UpgradeDowngradeTest(UpgradeDowngradeBase):
    """Runs DAOS interoperability test upgrade and downgrade.

    :avocado: recursive
    """

    def test_upgrade_downgrade(self):
        """Run DAOS interoperability upgrade downgrade test suites to use ior.

        JIRA ID:
            DAOS-10274: DAOS interoperability test user interface test.
            DAOS-10275: DAOS interoperability test system and pool upgrade basic.
            DAOS-10280: DAOS upgrade from 2.0(old) to 2.2(new) and downgrade back to 2.0(old).
            DAOS-10288: DAOS interoperability test DAOS system and pool upgrade on 8 servers.
                        (launch.py -e updown_grade_8svr.yaml)

        :avocado: tags=manual
        :avocado: tags=interop
        :avocado: tags=UpgradeDowngradeTest,test_upgrade_downgrade
        """
        self.upgrade_and_downgrade()

    def test_pool_upgrade_sys_stop_resume(self):
        """Run DAOS pool upgrade when system stop and resume.

        JIRA ID:
            DAOS-10282: DAOS interoperability test pool upgrade with system stop(fault-injection).
            DAOS-10279: DAOS interoperability test pool upgrade interrupt and resume after server
                        upgraded(fault-injection).

        :avocado: tags=manual
        :avocado: tags=interop
        :avocado: tags=UpgradeDowngradeTest,test_pool_upgrade_sys_stop_resume
        """
        self.upgrade_and_downgrade(fault_on_pool_upgrade=True)
