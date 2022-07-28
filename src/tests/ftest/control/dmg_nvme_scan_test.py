#!/usr/bin/python3
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""


import os

from dmg_utils import DmgCommand
from apricot import TestWithServers
from avocado.utils import process


class DmgNvmeScanTest(TestWithServers):
    """Test Class Description:
    Simple test to verify the scan function of the dmg tool.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DmgNvmeScanTest object."""
        super().__init__(*args, **kwargs)
        self.start_agents_once = False
        self.start_servers_once = False
        self.setup_start_agents = False

    def test_dmg_nvme_scan_basic(self):
        """
        JIRA ID: DAOS-2485
        Test Description: Test basic dmg functionality to scan the nvme storage.
        on the system.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,basic,dmg
        :avocado: tags=nvme_scan,test_dmg_nvme_scan_basic
        """
        # Create dmg command
        dmg = DmgCommand(os.path.join(self.prefix, "bin"))
        dmg.get_params(self)

        # Update hostlist value for dmg command
        port = self.params.get("port", "/run/server_config/*")
        servers_with_ports = [
            "{}:{}".format(host, port) for host in self.hostlist_servers]
        dmg.hostlist = servers_with_ports

        try:
            dmg.run()
        except process.CmdError as details:
            self.fail("dmg command failed: {}".format(details))
