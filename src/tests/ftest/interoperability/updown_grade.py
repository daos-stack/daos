"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from upgrade_downgrade_base import UpgradeDowngradeBase


class UpgradeDowngradeTest(UpgradeDowngradeBase):
    """Runs DAOS interoperability test upgrade and downgrade.

    Test description: Test DAOS interoperability upgrade and downgrade.
    Test step:
        (1)Setup and show rpm, dmg and daos versions on all hosts
        (2)Create pool, container and pool attributes
        (3)Setup and run IOR
            (3.a)DFS
            (3.b)HDF5
            (3.c)POSIX symlink to a file
        (4)Dmg system stop
        (5)Upgrade RPMs to specified new version
        (6)Restart servers
        (7)Restart agent
            verify pool attributes
            verify IOR data integrity after upgraded
            (7.a)DFS
            (7.b)HDF5
            (7.c)POSIX symlink to a file
        (8)Dmg pool get-prop after RPMs upgraded before Pool upgraded
        (9)Dmg pool upgrade and get-prop after RPMs upgraded
            (9.a)Enable fault injection during pool upgrade
            (9.b)Pool upgrade without fault-injection
        (10)Create new pool after rpms Upgraded
        (11)Downgrade and cleanup
        (12)Restart servers and agent
    To launch test:
        (1)sudo yum install all needed RPMs to all hosts
        (2)define repository with RPMs to updown_grade.yaml
        (3)./launch.py test_upgrade_downgrade -ts boro-[..] -tc boro-[..]

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
