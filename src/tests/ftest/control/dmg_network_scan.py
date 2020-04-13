#!/usr/bin/python
"""
  (C) Copyright 2018-2019 Intel Corporation.

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

from general_utils import run_cmd
from avocado.utils import process
from control_test_base import ControlTestBase


class NetDev(object):
    """A class to represent the information of a network device"""
    def __init__(self, host, fabric_iface, provider, numa_node):
        """ Initialize the network device data object."""
        self.host = host
        self.fabric_iface = fabric_iface
        self.provider = provider
        self.numa_node = numa_node

class DmgNetworkScanTest(ControlTestBase):
    """Test Class Description:
    Simple test to verify the network scan function of the dmg tool.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DmgNetworkScanTest object."""
        super(DmgNetworkScanTest, self).__init__(*args, **kwargs)
        self.setup_start_agents = False

    def get_sys_info(self):
        """ Get expected values of numa nodes with lstopo."""
        sys_info = {}
        for d in os.listdir("/sys/class/net/"):
            sys_info[d] = {}
            if os.path.exists("/sys/class/net/{}/device/infiniband".format(d)):
                sys_info[d]["dev"] = os.listdir(
                    "/sys/class/net/{}/device/infiniband".format(d))
            if os.path.exists('/sys/class/net/{}/device/numa_node'.format(d)):
                numa_node = ''.join(
                    open('/sys/class/net/{}/device/numa_node'.format(d),
                         'r').read()).rstrip()
                sys_info[d]["numa"] = numa_node
        return sys_info

    def get_dev_provider(self, device):
        """Get expected values of provider with fi_info.

        Args:
            device (str): network device name
        """
        fi_info_dir = os.path.join(self.prefix, 'bin')
        fi_info = os.path.join(fi_info_dir, "fi_info -d {}".format(device))

        # Run the command
        output = process.run(fi_info, ignore_status=True)

        # Command failed or possibly timed out
        if output.exit_status != 0 and output.exit_status != 61:
            msg = "Error occurred running '{}': {}".format(
                fi_info, output.exit_status)
            self.fail(msg)

        # Clean up string
        providers = []
        if output.exit_status == 61:
            return providers
        for line in output.stdout.splitlines():
            if "provider" in line:
                prov = line.strip().split()[-1]
                if prov not in providers:
                    providers.append(prov)

        return providers

    def test_dmg_network_scan_basic(self):
        """
        JIRA ID: DAOS-2516
        Test Description: Test basic dmg functionality to scan the network
        devices on the system.
        :avocado: tags=all,small,pr,hw,dmg,network_scan,basic
        """
        net_scan_info = self.get_dmg_output("network_scan")
        for dev_info in net_scan_info:
            if dev_info[0] != "":


        # Validate dmg output against 3rd party tools: lstopo, fi_info
        covered = []
        for dev in dmg_out:
            dmg_iface = dev[0].split()[1]
            dmg_numa = int(dev[1].split()[1])
            dmg_prov = dev[2].split()[1].split("+")[1]
            tr = {
                "ib0": "hfi1_0", "ib1": "hfi1_1",
                "eth0": "i40iw1", "eth1": "i40iw0"
            }
            for sys_numa, sys_ifaces in self.get_numa_info().items():
                if sys_numa == 0:
                    sys_ifaces.append("lo")
                if sys_numa == dmg_numa and dmg_iface in sys_ifaces:
                    # Verify that the provider is supported for this device
                    if dmg_prov in self.get_dev_provider(dmg_iface):
                        covered.append(dev)
                    if (dmg_iface in tr and
                            dmg_prov in self.get_dev_provider(tr[dmg_iface])):
                        covered.append(dev)

        # Sort both lists
        dmg_out.sort()
        covered.sort()

        if covered != dmg_out:
            self.fail("dmg output does not match network devices found.")
