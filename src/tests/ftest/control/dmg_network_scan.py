"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from ClusterShell.NodeSet import NodeSet
from exception_utils import CommandFailure
from network_utils import SUPPORTED_PROVIDERS, NetworkDevice, get_network_information


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
        for entry in get_network_information(self.log, self.hostlist_servers, SUPPORTED_PROVIDERS):
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
        return self.get_dmg_network_information(dmg.network_scan())

    def get_dmg_network_information(self, dmg_network_scan):
        """Get the network device information from the dmg network scan output.

        Args:
            dmg_network_scan (dict): the dmg network scan json command output

        Raises:
            CommandFailure: if there was an error processing the dmg network scan output

        Returns:
            list: a list of NetworkDevice objects identifying the network devices on each host

        """
        network_devices = []

        try:
            for host_fabric in dmg_network_scan["response"]["HostFabrics"].values():
                for host in NodeSet(host_fabric["HostSet"].split(":")[0]):
                    for interface in host_fabric["HostFabric"]["Interfaces"]:
                        network_devices.append(
                            NetworkDevice(
                                host, interface["Device"], None, 1, interface["Provider"],
                                interface["NumaNode"])
                        )
        except KeyError as error:
            raise CommandFailure(
                f"Error processing dmg network scan json output: {dmg_network_scan}") from error

        return network_devices

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
