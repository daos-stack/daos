"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from upgrade_downgrade_base import UpgradeDowngradeBase


class DowngradeTest(UpgradeDowngradeBase):
    """Runs DAOS interoperability test upgrade and downgrade.

    JIRA ID:
        DAOS-10281: DAOS interoperability formatted 2.2(new) storage downgrade 2.0(old)

    Test description: Test DAOS interoperability downgrade with a formatted server storage.
    To launch test:
        (1)sudo yum install all needed RPMs to all hosts
        (2)define repository with RPMs to updown_grade.yaml
        (3)./launch.py test_downgrade -ts boro-[..] -tc boro-[..]

    :avocado: recursive
    """

    def test_downgrade(self):
        """Run  DAOS interoperability test suites to use ior.

        :avocado: tags=manual
        :avocado: tags=interop
        :avocado: tags=DowngradeTest,test_downgrade
        """
        self.upgrade_and_downgrade()
