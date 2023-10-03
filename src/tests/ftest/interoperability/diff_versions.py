"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from upgrade_downgrade_base import UpgradeDowngradeBase


class UpgradeDowngradeDiffVersionsTest(UpgradeDowngradeBase):
    """Runs DAOS interoperability test upgrade and downgrade.

    JIRA ID:
        DAOS-10287: Negative test mix server ranks with of 2.0 and 2.2.
        DAOS-10276: Different version of server and client.
        DAOS-10278: Verify new pool create after server upgraded to new version.
        DAOS-10283: Verify formatted 2.0 upgraded to 2.2 pool create system downgrade to 2.0
        DAOS-11689: Interoperability between different version of daos_server and daos_agent
        DAOS-11691: Interoperability between different version of daos_server and libdaos

    Test step:
        (1)Setup
        (2)dmg system stop
        (3)Upgrade 1 server-host to new version
        (4)Negative test - dmg pool query on mix-version servers
        (5)Upgrade rest server-hosts to 2.2
        (6)Restart 2.0 agent
        (7)Verify 2.0 agent connect to 2.2 server, daos and libdaos
        (8)Upgrade agent to 2.2
        (9)Verify pool and containers create on 2.2 agent and server
        (10)Downgrade server to 2.0
        (11)Verify 2.2 agent to 2.0 server, daos and libdaos
        (12)Downgrade agent to 2.0
    To launch test:
        (1)sudo yum install all needed RPMs to all hosts
        (2)define repository with RPMs to updown_grade.yaml
        (3)./launch.py test_diff_versions_agent_server -ts boro-[..] -tc boro-[..]

    :avocado: recursive
    """

    def test_diff_versions_agent_server(self):
        """Run DAOS interoperability test different versions agent to servers.

        :avocado: tags=manual
        :avocado: tags=interop
        :avocado: tags=UpgradeDowngradeDiffVersionsTest,test_diff_versions_agent_server
        """
        self.diff_versions_agent_server()
