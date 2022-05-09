#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import socket

from apricot import TestWithServers
from ClusterShell.NodeSet import NodeSet

from network_utils import NetworkDevice, get_network_information


class DmgNetworkScanTest(TestWithServers):
    # pylint: disable=too-many-ancestors
    """Test Class Description:

    Simple test to verify the network scan function of the dmg tool.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DmgNetworkScanTest object."""
        super().__init__(*args, **kwargs)
        self.start_agents_once = False
        self.start_servers_once = False
        self.setup_start_agents = False
        self.setup_start_servers = False

    def setUp(self):
        """Set up each test case."""
        super().setUp()

        # Run the dmg command locally, unset config to run locally
        self.hostlist_servers = socket.gethostname().split(".")[0].split(",")
        self.access_points = self.hostlist_servers[:1]
        self.start_servers()
        self.dmg = self.get_dmg_command()

    # @staticmethod
    # def clean_info(net_devs):
    #     """Clean up the devices found in the system.

    #     Clean up devices that don't have a numa node and some providers that
    #     might be duplicates, also need to attach 'ofi' to provider name.

    #     Args:
    #         net_devs (NetDev): list of NetDev objects.

    #     Returns:
    #         NetDev: list of NetDev objects.

    #     """
    #     idx_l = []
    #     for idx, dev in enumerate(net_devs):
    #         if dev.f_iface == "lo":
    #             idx_l.append(idx)
    #         elif not dev.numa:
    #             idx_l.append(idx)
    #         else:
    #             if dev.providers:
    #                 no_dups = list(dict.fromkeys(dev.providers))
    #                 dev.providers = ["ofi+" + i for i in no_dups]
    #     _ = [net_devs.pop(idx - i) for i, idx in enumerate(sorted(idx_l))]

    #     return net_devs

    def get_sys_info(self):
        """Get the system device information.

        Returns:
            list: list of NetDev objects.

        """
        localhost = socket.gethostname().split(".")[0]
        hosts = NodeSet(localhost)
        supported = ["gni", "psm2", "tcp", "sockets", "verbs", "ofi_rxm", "dc_mlx5"]
        sys_info = []
        for entry in get_network_information(hosts, supported):
            if entry.device.startswith("ib"):
                sys_info.append(entry)
                sys_info[-1].ib_device = None
                # mapping = {"dc_mlx5": "ucx+dc_x"}
                # dev.providers = ["ofi+" + i for i in no_dups]
        return sys_info

    def get_dmg_info(self):
        """Store the information received from dmg output.

        Returns:
            list: list of NetDev objects.

        """
        host = None
        numa = None
        temp_net_devs = []
        dmg_net_devs = []

        # Get dmg info
        network_out = self.dmg.network_scan()

        # Get devices divided into dictionaries, with iface being the key. e.g.,
        # temp_net_devs =
        # [
        #     {
        #         'ib0': [['ofi+sockets'], 'wolf-154', '0'],
        #         'ib1': [['ofi+sockets'], 'wolf-154', '1'],
        #         'eth0': [['ofi+sockets'], 'wolf-154', '0']
        #     }
        # ]
        host_fabrics = network_out["response"]["HostFabrics"]
        struct_hash = list(host_fabrics.keys())[0]
        interfaces = host_fabrics[struct_hash]["HostFabric"]["Interfaces"]

        # Fill in the dictionary.
        parsed_devs = {}
        for interface in interfaces:
            device = interface["Device"]
            provider = interface["Provider"]
            if device in parsed_devs:
                parsed_devs[device][0].append(provider)
            else:
                host = host_fabrics[struct_hash]["HostSet"].split(":")[0]
                parsed_devs[device] = [
                    [provider], host, str(interface["NumaNode"])]

        temp_net_devs.append(parsed_devs)

        # Create NetDev objects
        for d in temp_net_devs:
            for iface in d:
                for provider in d[iface][0]:
                    dmg_net_devs.append(
                        NetworkDevice(d[iface][1], iface, None, "1", provider, d[iface][2]))

        return dmg_net_devs

    def test_dmg_network_scan_basic(self):
        """JIRA ID: DAOS-2516

        Test Description: Test basic dmg functionality to scan the network
        devices on the system.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,small
        :avocado: tags=dmg,network_scan,basic
        """
        # Get info, both these functions will return a list of NetDev objects
        dmg_info = sorted(
            self.get_dmg_info(), key=lambda x: (x.device, x.provider))
        sys_info = sorted(
            self.get_sys_info(), key=lambda x: (x.device, x.provider))

        # Validate the output with what we expect.
        for title, info in {"SYS INFO": sys_info, "DMG INFO": dmg_info}.items():
            self.log.info("-" * 100)
            self.log.info(title)
            for entry in info:
                self.log.info("  %s", entry)
        self.log.info("-" * 100)
        msg = "\nDmg Info:\n{} \n\nSysInfo:\n{}".format(dmg_info, sys_info)
        self.assertEqual(sys_info, dmg_info, msg)
