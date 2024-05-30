"""
(C) Copyright 2021-2024 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers


class HarnessSetupTest(TestWithServers):
    """Harness setup test cases.

    :avocado: recursive
    """

    def test_setup(self):
        """Verify the TestWithServers.setUp() method.

        Useful for setting up the /etc/daos/daos_server.yml files on multiple hosts.

        :avocado: tags=all
        :avocado: tags=hw,medium,large
        :avocado: tags=harness
        :avocado: tags=HarnessSetupTest,test_setup
        """

        prepare_timeout = self.params.get('storage_prepare_timeout')
        format_timeout = self.params.get('storage_format_timeout')
        if self.server_managers[0].storage_prepare_timeout.value != prepare_timeout:
            self.fail("Storage prepare was not set correctly from the test yaml")
        if self.server_managers[0].storage_format_timeout.value != format_timeout:
            self.fail("Storage format was not set correctly from the test yaml")
        self.log.info("Test passed!")
