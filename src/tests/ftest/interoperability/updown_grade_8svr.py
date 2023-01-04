"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from updown_grade_base import UpgradeDowngradeBase

class UpgradeDowngradeTest_EC(UpgradeDowngradeBase):
    """Runs DAOS interoperability test upgrade and downgrade.

    JIRA ID:
        DAOS-10288: DAOS interoperability test DAOS system and pool upgrade on 8 servers.

    Test description: Test DAOS interoperability upgrade and downgrade on 8 servers.
    To launch test:
        (1)sudo yum install all needed RPMs to all hosts
        (2)define repository with RPMs to updown_grade.yaml
        (3)./launch.py test_upgrade_downgrade_ec -ts boro-[..] -tc boro-[..]

    :avocado: recursive
    """

    def test_upgrade_downgrade_ec(self):
        """Run  DAOS interoperability test suites to use ior on 8 servers and EC.

        :avocado: tags=manual
        :avocado: tags=interop
        :avocado: tags=test_upgrade_downgrade_ec
        """
        self.upgrade_and_downgrade()
