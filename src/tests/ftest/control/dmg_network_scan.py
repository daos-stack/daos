"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers

from network_utils import get_network_information, get_dmg_network_information, SUPPORTED_PROVIDERS


class DmgNetworkScanTest(TestWithServers):
    """Test Class Description:

    Simple test to verify the network scan function of the dmg tool.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DmgNetworkScanTest object."""
        super().__init__(*args, **kwargs)
        self.setup_start_agents = False

    def get_sys_info(self):
        """Get the system device information.

        Returns:
            list: list of NetworkDevice objects.

        """
        server_provider = self.server_managers[0].get_config_value("provider")
        sys_info = []
        for entry in get_network_information(self.hostlist_servers, SUPPORTED_PROVIDERS):
            if server_provider in entry.provider:
                entry.device = None
                sys_info.append(entry)
        return sys_info

    def get_dmg_info(self):
        """Get the information received from dmg network scan output.

        Returns:
            list: list of NetworkDevice objects.

        """
        dmg = self.get_dmg_command()
        return get_dmg_network_information(dmg.network_scan())

    def test_dmg_network_scan_basic(self):
        """JIRA ID: DAOS-2516

        Test Description: Test basic dmg functionality to scan the network
        devices on the system.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=dmg,control,network_scan,basic
        :avocado: tags=DmgNetworkScanTest,test_dmg_network_scan_basic
        """
        # Get info, both these functions will return a list of NetDev objects
        dmg_info = sorted(
            self.get_dmg_info(), key=lambda x: (x.name, x.provider))
        sys_info = sorted(
            self.get_sys_info(), key=lambda x: (x.name, x.provider))

        # Validate the output with what we expect.
        for title, info in {"SYS INFO": sys_info, "DMG INFO": dmg_info}.items():
            self.log.info("-" * 100)
            self.log.info(title)
            for entry in info:
                self.log.info("  %s", entry)
        self.log.info("-" * 100)
        msg = f"\nDmg Info:\n{dmg_info} \n\nSysInfo:\n{sys_info}"
        self.assertEqual(sys_info, dmg_info, msg)
