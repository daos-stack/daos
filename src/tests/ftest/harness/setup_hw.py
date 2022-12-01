"""
(C) Copyright 2021-2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers


class HarnessSetupTest(TestWithServers):
    """Harness setup test cases.

    :avocado: recursive
    """

    def test_setup_hw(self):
        """Verify the TestWithServers.setUp() method.

        Useful for setting up the /etc/daos/daos_server.yml files on multiple hosts.

        :avocado: tags=all
        :avocado: tags=hw,medium,large
        :avocado: tags=harness
        :avocado: tags=HarnessSetupTest,test_setup_hw
        """
        self.assertEqual(self.server_managers[0].storage_prepare_timeout.value, 60,
                         "FAILED: storage prepare was not set correctly from the yaml")
        self.assertEqual(self.server_managers[0].storage_format_timeout.value, 60,
                         "FAILED: storage format was not set correctly from the yaml")
        self.log.info("Test passed!")
