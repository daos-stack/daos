#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
from __future__ import print_function

import os
import re
import socket

from avocado.utils import process
from apricot import TestWithServers


class NetDev(object):
    # pylint: disable=too-few-public-methods
    """A class to represent the information of a network device"""

    SUPPORTED_PROV = [
        "gni", "psm2", "tcp", "sockets", "verbs", "ofi_rxm"]

    def __init__(self, host=None, f_iface=None, providers=None, numa=None):
        """Initialize the network device data object."""
        self.host = host
        self.f_iface = f_iface
        self.providers = providers
        self.numa = numa

    def __repr__(self):
        """Overwrite to display formatted devices."""
        return self.__str__()

    def __str__(self):
        """Overwrite to display formatted devices."""
        return "\n".join("{}: {}".format(key, getattr(self, key, "MISSING"))
                         for key in self.__dict__)

    def __ne__(self, other):
        """Override the default not-equal implementation."""
        return not self.__eq__(other)

    def __eq__(self, other):
        """Override the default implementation to compare devices."""
        status = isinstance(other, NetDev)
        for key in self.__dict__:
            if not status:
                break
            try:
                status &= getattr(self, key) == getattr(other, key)
            except AttributeError:
                status = False
        return status

    def get_copy(self):
        """Create a copy instance of this object."""
        return NetDev(self.host, self.f_iface, self.providers, self.numa)

    def set_dev_info(self, dev_name, libfabric_prefix):
        """Get all of this devices' information.

        Args:
            dev_name (str): device name or infiniband device name
            prefix (str): prefix path pointing to libfabric install path
        """
        self.set_numa()
        self.set_providers(dev_name, libfabric_prefix)

    def set_numa(self):
        """Get the numa node information for this device."""
        if self.f_iface:
            p = "/sys/class/net/{}/device/numa_node".format(self.f_iface)
            if os.path.exists(p):
                self.numa = ''.join(open(p, 'r').read()).rstrip()

    def set_providers(self, dev_name, libfabric_prefix):
        """Get the provider information."""
        # Setup and run command
        fi_info = os.path.join(libfabric_prefix, "bin",
                               "fi_info -d {}".format(dev_name))
        out = process.run(fi_info)

        # Parse the list and divide the info
        # pylint: disable=no-member
        prov = re.findall(
            r"(?:provider:|domain:)\s+([A-Za-z0-9;_+]+)", out.stdout, re.M)
        # pylint: enable=no-member
        info = [prov[i:(i + 2)] for i in range(0, len(prov), 2)]

        # Get providers that are supported
        supported = []
        for i in info:
            for sup in self.SUPPORTED_PROV:
                if sup in i[0] and "rxd" not in i[0]:
                    supported.append(i)

        # Check that the domain name is in output found.
        providers = []
        for i in supported:
            if i[1] == dev_name:
                providers.append(i[0])

        if self.providers:
            self.providers.extend(providers)
        else:
            self.providers = providers


class DmgNetworkScanTest(TestWithServers):
    # pylint: disable=too-many-ancestors
    """Test Class Description:
    Simple test to verify the network scan function of the dmg tool.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DmgNetworkScanTest object."""
        super(DmgNetworkScanTest, self).__init__(*args, **kwargs)
        self.setup_start_agents = False
        self.setup_start_servers = False

    def setUp(self):
        """Set up each test case."""
        super(DmgNetworkScanTest, self).setUp()

        # Run the dmg command locally, unset config to run locally
        self.hostlist_servers = socket.gethostname().split(".")[0].split(",")
        self.start_servers()
        self.dmg = self.get_dmg_command()

    @staticmethod
    def get_devs():
        """ Get list of devices."""
        devs = {}
        for dev in os.listdir("/sys/class/net/"):
            devs[dev] = [dev]
            # Check if we need to add infiniband dev to list
            p = "/sys/class/net/{}/device/infiniband".format(dev)
            if os.path.exists(p):
                devs[dev].extend(os.listdir(p))
        return devs

    @staticmethod
    def clean_info(net_devs):
        """Clean up the devices found in the system.

        Clean up devices that don't have a numa node and some providers that
        might be duplicates, also need to attach 'ofi' to provider name.

        Args:
            net_devs (NetDev): list of NetDev objects.

        Returns:
            NetDev: list of NetDev objects.

        """
        idx_l = []
        for idx, dev in enumerate(net_devs):
            if dev.f_iface == "lo":
                idx_l.append(idx)
            elif not dev.numa:
                idx_l.append(idx)
            else:
                if dev.providers:
                    no_dups = list(dict.fromkeys(dev.providers))
                    dev.providers = ["ofi+" + i for i in no_dups]
        _ = [net_devs.pop(idx - i) for i, idx in enumerate(sorted(idx_l))]

        return net_devs

    def get_sys_info(self):
        """Get the system device information.

        Returns:
            list: list of NetDev objects.

        """
        sys_net_devs = []

        # Get device names on this system
        dev_names = self.get_devs()
        for dev in dev_names:
            dev_info = NetDev(self.hostlist_servers[-1], dev)
            for dev_name in dev_names[dev]:
                dev_info.set_dev_info(dev_name, self.ofi_prefix)
            sys_net_devs.append(dev_info)

        # Create NetDev per provider to match dmg output
        f_net_devs = []
        for dev in self.clean_info(sys_net_devs):
            if dev.providers:
                for provider in dev.providers:
                    new_dev = dev.get_copy()
                    new_dev.providers = [provider]
                    f_net_devs.append(new_dev)

        # Get the provider specified in server config
        cfg_p = self.server_managers[0].get_config_value("provider")
        if cfg_p:
            f_net_devs = [dev for dev in f_net_devs if dev.providers == [cfg_p]]

        return f_net_devs

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
        scan_info = self.dmg.get_output("network_scan")

        # Get index range to break up information by hostname
        r = [idx for idx, info in enumerate(scan_info) if info[0] != ""]
        r = r[1] - r[0] if len(r) > 1 else len(scan_info)

        # Clean up the output, i.e. remove empty string items from lists
        for idx, info in enumerate(scan_info):
            scan_info[idx] = [i for i in info if i]

        # Get devices divided into dictionaries, with iface being the key
        info = [scan_info[dev:(dev + r)] for dev in range(0, len(scan_info), r)]
        for host_info in info:
            parsed_devs = {}
            host = host_info[0][0]
            for dev_info in host_info[1:]:
                if len(dev_info) == 1:
                    numa = dev_info[0]
                    continue
                for iface in dev_info[1].replace(" ", "").split(","):
                    if iface in parsed_devs:
                        parsed_devs[iface][0].extend(dev_info[0].split())
                    else:
                        parsed_devs[iface] = [dev_info[0].split(), host, numa]
            temp_net_devs.append(parsed_devs)

        # Create NetDev objects
        for d in temp_net_devs:
            for iface in d:
                net_dev = [NetDev(d[iface][1], iface, [provider], d[iface][2])
                           for provider in d[iface][0]]
                dmg_net_devs.extend(net_dev)

        return dmg_net_devs

    def test_dmg_network_scan_basic(self):
        """
        JIRA ID: DAOS-2516
        Test Description: Test basic dmg functionality to scan the network
        devices on the system.
        :avocado: tags=all,small,daily_regression,hw,dmg,network_scan,basic
        """
        # Get info, both these functions will return a list of NetDev objects
        dmg_info = sorted(
            self.get_dmg_info(), key=lambda x: (x.f_iface, x.providers))
        sys_info = sorted(
            self.get_sys_info(), key=lambda x: (x.f_iface, x.providers))

        # Validate the output with what we expect.
        msg = "\nDmg Info:\n{} \n\nSysInfo:\n{}".format(dmg_info, sys_info)
        self.assertEqual(sys_info, dmg_info, msg)
