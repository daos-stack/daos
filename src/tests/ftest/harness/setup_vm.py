"""
(C) Copyright 2021-2023 Intel Corporation.
(C) Copyright 2026 Hewlett Packard Enterprise Development LP

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from setup import TestServerTimeouts


class HarnessSetupVmTest(TestServerTimeouts):
    """Harness setup test cases.

    :avocado: recursive
    """

    def test_setup_vm(self):
        """Verify the TestWithServers.setUp() method.

        Useful for setting up the /etc/daos/daos_server.yml files on multiple hosts.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,server_setup
        :avocado: tags=HarnessSetupVmTest,test_setup_vm
        """
        self._verify_server_timeouts()
