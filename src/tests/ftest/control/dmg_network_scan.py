#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
# import os
# import re
from cmath import inf
import socket

# from avocado.utils import process
from apricot import TestWithServers
from ClusterShell.NodeSet import NodeSet

from network_utils import NetworkDevice, get_network_information


# class NetDev():
#     """A class to represent the information of a network device."""

#     SUPPORTED_PROV = ["gni", "psm2", "tcp", "sockets", "verbs", "ofi_rxm", "ucx"]

#     def __init__(self, host=None, f_iface=None, providers=None, numa=None):
#         """Initialize the network device data object."""
#         self.host = host
#         self.f_iface = f_iface
#         self.providers = providers
#         self.numa = numa

#     def __repr__(self):
#         """Overwrite to display formatted devices."""
#         return self.__str__()

#     def __str__(self):
#         """Overwrite to display formatted devices."""
#         return "\n".join("{}: {}".format(key, getattr(self, key, "MISSING"))
#                          for key in self.__dict__)

#     def __ne__(self, other):
#         """Override the default not-equal implementation."""
#         return not self.__eq__(other)

#     def __eq__(self, other):
#         """Override the default implementation to compare devices."""
#         status = isinstance(other, NetDev)
#         for key in self.__dict__:
#             if not status:
#                 break
#             try:
#                 status &= getattr(self, key) == getattr(other, key)
#             except AttributeError:
#                 status = False
#         return status

#     def get_copy(self):
#         """Create a copy instance of this object."""
#         return NetDev(self.host, self.f_iface, self.providers, self.numa)

#     def set_dev_info(self, dev_name, ofi_info, ucx_info):
#         """Get all of this devices' information.

#         Args:
#             dev_name (str): device name or infiniband device name
#             ofi_info (str): provider information from the fi_info command
#             ucx_info (str): provider information from the ucx_info command
#         """
#         self.set_numa()
#         self.set_providers(dev_name, ofi_info, ucx_info)

#     def set_numa(self):
#         """Get the numa node information for this device."""
#         if self.f_iface:
#             p = "/sys/class/net/{}/device/numa_node".format(self.f_iface)
#             if os.path.exists(p):
#                 self.numa = ''.join(open(p, 'r').read()).rstrip()

#     def set_providers(self, dev_name, ofi_info, ucx_info):
#         """Get the provider information.

#         Args:
#             dev_name (str): device name or infiniband device name
#             ofi_info (str): provider information from the fi_info command
#             ucx_info (str): provider information from the ucx_info command
#         """
#         # Parse the list and divide the info
#         info = []
#         for regex, output in ((r"(?:provider:|domain:)\s+([A-Za-z0-9;_+]+)", ofi_info),
#                               (r"(?:Transport:|Device:)\s+([A-Za-z0-9;_+]+)", ucx_info)):
#             data = re.findall(regex, output, re.M)
#             info.extend([data[i:(i + 2)] for i in range(0, len(data), 2)])

#         # Get providers that are supported
#         supported = []
#         for i in info:
#             for sup in self.SUPPORTED_PROV:
#                 if sup in i[0] and "rxd" not in i[0]:
#                     supported.append(i)

#         # Check that the domain name is in output found.
#         providers = []
#         for i in supported:
#             if i[1] == dev_name:
#                 providers.append(i[0])

#         if self.providers:
#             self.providers.extend(providers)
#         else:
#             self.providers = providers


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
    # def get_devs():
    #     """Get list of devices."""
    #     devs = {}
    #     for dev in os.listdir("/sys/class/net/"):
    #         devs[dev] = [dev]
    #         # Check if we need to add infiniband dev to list
    #         p = "/sys/class/net/{}/device/infiniband".format(dev)
    #         if os.path.exists(p):
    #             devs[dev].extend(os.listdir(p))
    #     return devs

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
        # mapping = {"dc_mlx5": "ucx+dc_x"}
        return get_network_information(hosts, supported)

        # sys_net_devs = []

        # # Get device names on this system
        # dev_names = self.get_devs()
        # self.log.debug("get_sys_info() detected: dev_names=%s", dev_names)

        # # Get provider information
        # ofi_info_command = os.path.join(self.ofi_prefix, "bin", "fi_info")
        # ofi_info = process.run(ofi_info_command)
        # ucx_info_command = os.path.join(os.sep, "usr", "bin", "ucx_info -d")
        # ucx_info = process.run(ucx_info_command)
        # for dev in dev_names:
        #     dev_info = NetDev(self.hostlist_servers[-1], dev)
        #     for dev_name in dev_names[dev]:
        #         dev_info.set_dev_info(dev_name, ofi_info.stdout_text, ucx_info.stdout_text)
        #     sys_net_devs.append(dev_info)
        # self.log.debug("get_sys_info() detected: sys_net_devs=%s", sys_net_devs)

        # # Create NetDev per provider to match dmg output
        # f_net_devs = []
        # clean_devs = self.clean_info(sys_net_devs)
        # self.log.debug("get_sys_info() detected: clean_devs=%s", clean_devs)
        # for dev in clean_devs:
        #     if dev.providers:
        #         for provider in dev.providers:
        #             new_dev = dev.get_copy()
        #             new_dev.providers = [provider]
        #             f_net_devs.append(new_dev)
        # self.log.debug("get_sys_info() detected: dev_names=%s", dev_names)

        # # Get the provider specified in server config
        # cfg_p = self.server_managers[0].get_config_value("provider")
        # if cfg_p:
        #     if cfg_p == "ofi+tcp":
        #         cfg_p = "ofi+tcp;ofi_rxm"
        #     elif cfg_p == "ofi+verbs":
        #         cfg_p = "ofi+verbs;ofi_rxm"
        #     f_net_devs = [dev for dev in f_net_devs if dev.providers == [cfg_p]]
        # self.log.debug("get_sys_info() detected: f_net_devs=%s", f_net_devs)

        # return f_net_devs

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
                # net_dev = [NetDev(d[iface][1], iface, [provider], d[iface][2])
                #            for provider in d[iface][0]]
                # dmg_net_devs.extend(net_dev)

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
        # dmg_info = sorted(
        #     self.get_dmg_info(), key=lambda x: (x.f_iface, x.providers))
        # sys_info = sorted(
        #     self.get_sys_info(), key=lambda x: (x.f_iface, x.providers))

        # Validate the output with what we expect.
        self.log.info("-" * 100)
        self.log.info("SYS INFO")
        self.log.info("%s", sys_info)
        self.log.info("-" * 100)
        self.log.info("DMG INFO")
        self.log.info("%s", dmg_info)
        self.log.info("-" * 100)
        msg = "\nDmg Info:\n{} \n\nSysInfo:\n{}".format(dmg_info, sys_info)
        self.assertEqual(sys_info, dmg_info, msg)
