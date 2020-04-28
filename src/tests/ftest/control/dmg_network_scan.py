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
from control_test_base import ControlTestBase


class NetDev(object):
    # pylint: disable=too-few-public-methods
    """A class to represent the information of a network device"""
    def __init__(self, host=None, f_iface=None, providers=None, numa=None):
        """Initialize the network device data object."""
        self.host = host
        self.f_iface = f_iface
        self.providers = providers
        self.numa = numa

    def __repr__(self):
        """Overwrite to display formated devices."""
        self.__str__()

    def __str__(self):
        """Overwrite to display formated devices."""
        return "\n".join("{}: {}".format(key, getattr(self, key, "MISSING"))
                         for key in self.__dict__.keys())

    def __ne__(self, other):
        """Override the default not-equal implementation."""
        return not self.__eq__(other)

    def __eq__(self, other):
        """Override the default implementation to compare devices."""
        status = isinstance(other, NetDev)
        for key in self.__dict__.keys():
            if not status:
                break
            try:
                status &= getattr(self, key) == getattr(other, key)
            except AttributeError:
                status = False
        return status

    def set_dev_info(self, prefix):
        """Get all of this devices' information.

        Args:
            prefix (str): prefix path pointing to daos intall path
        """
        self.set_numa()
        self.set_providers(prefix)

    def set_numa(self):
        """Get the numa node information for this device."""
        if self.f_iface:
            p = "/sys/class/net/{}/device/numa_node".format(self.f_iface)
            if os.path.exists(p):
                self.numa = ''.join(open(p, 'r').read()).rstrip()

    def set_providers(self, prefix):
        """Get the provider information for this device."""
        if self.f_iface:
            fi_info = os.path.join(
                prefix, "bin", "fi_info -d {}".format(self.f_iface))

            # Run command
            out = process.run(fi_info)

            # Parse the list and remove dups
            dups = re.findall(r"(?:provider):\s+([A-Za-z0-9;_+]+)", out.stdout)
            self.providers = list(dict.fromkeys(dups))


class DmgNetworkScanTest(ControlTestBase):
    # pylint: disable=too-many-ancestors
    """Test Class Description:
    Simple test to verify the network scan function of the dmg tool.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DmgNetworkScanTest object."""
        super(DmgNetworkScanTest, self).__init__(*args, **kwargs)
        self.setup_start_agents = False

    @staticmethod
    def get_devs():
        """ Get list of devices."""
        devs = []
        for dev in os.listdir("/sys/class/net/"):
            devs.append(dev)
            # Check if we need to add infiniband dev to list
            p = "/sys/class/net/{}/device/infiniband".format(dev)
            if os.path.exists(p):
                devs.extend(os.listdir(p))
        return devs

    def get_sys_info(self):
        """ Get expected values of numa nodes with lstopo."""
        host = socket.gethostname().split(".")[0]
        sys_net_devs = []
        for dev in self.get_devs():
            dev_info = NetDev(host, dev)
            dev_info.set_dev_info(self.prefix)
            sys_net_devs.append(dev_info)

        return sys_net_devs

    def get_dmg_info(self):
        """ Store the information received from dmg output."""
        # Parse the dmg info
        host = None
        dmg_net_devs = []
        scan_info = self.get_dmg_output("network_scan")

        # Get devices divided
        info = [scan_info[dev:(dev + 3)] for dev in range(0, len(scan_info), 3)]
        for dev in info:
            if dev[0][0] != "":
                host = dev[0][0]
            dev_info = NetDev(host, dev[0][2], [dev[1][2]], dev[2][2])
            dmg_net_devs.append(dev_info)

        # Consolidate the dups
        conso = []
        diff = []
        for idx, i in enumerate(dmg_net_devs):
            for j in dmg_net_devs[idx + 1:]:
                if i.host == j.host and i.f_iface == j.f_iface and \
                        i.numa == j.numa and i.providers != j.providers:
                    providers = list(dict.fromkeys(i.providers + j.providers))
                    conso.append(NetDev(i.host, i.f_iface, providers, i.numa))
        for i in conso:
            for j in dmg_net_devs:
                if i.f_iface != j.f_iface and j not in diff:
                    diff.append(j)

        return conso + diff

    def test_dmg_network_scan_basic(self):
        """
        JIRA ID: DAOS-2516
        Test Description: Test basic dmg functionality to scan the network
        devices on the system.
        :avocado: tags=all,small,pr,hw,dmg,network_scan,basic
        """
        # Get info, both these functions will return a list of NetDev objects
        dmg_info = sorted(self.get_dmg_info())
        sys_info = sorted(self.get_sys_info())

        # Create error msg for user
        dmg_msg = "\n".join("{}".format(i) for i in dmg_info)
        sys_msg = "\n".join("{}".format(i) for i in sys_info)
        msg = "Dmg Info:\n{} \nSysInfo:\n{}".format(dmg_msg, sys_msg)

        # Validate the output with what we expect.
        self.assertEqual(sys_info, dmg_info, msg)
