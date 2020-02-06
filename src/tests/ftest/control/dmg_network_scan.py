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

from dmg_utils import DmgCommand
from general_utils import run_cmd
from avocado.utils import process
from apricot import TestWithServers


class DmgNetworkScanTest(TestWithServers):
    """Test Class Description:
    Simple test to verify the network scan function of the dmg tool.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DmgNetworkScanTest object."""
        super(DmgNetworkScanTest, self).__init__(*args, **kwargs)
        self.setup_start_agents = False

    def get_numa_info(self):
        """Get expected values of numa nodes with lstopo."""
        lstopo = "lstopo-no-graphics"
        try:
            output = run_cmd(lstopo)
        except process.CmdError as error:
            # Command failed or possibly timed out
            msg = "Error occurred running '{}': {}".format(lstopo, error)
            self.fail(msg)

        # Separate numa node info, pop first item as it has no useful info
        numas = re.split("NUMANode", output.stdout)
        numas.pop(0)

        numas_devs = {}
        for i, numa in enumerate(numas):
            net_dev = []
            split_numa = numa.split("\n")
            for line in split_numa:
                if "Net" in line or "OpenFabrics" in line:
                    # Get the devices i.e. eth0, ib0, ib1
                    net_dev.append("".join(line.strip().split('"')[1::2]))
            numas_devs[i] = net_dev

        return numas_devs

    def get_dev_provider(self, device):
        """Get expected values of provider with fi_info.

        Args:
            device (str): network device name
        """
        fi_info = os.path.join(self.bin, "fi_info -d {}".format(device))
        try:
            output = process.run(fi_info)
        except process.CmdError as error:
            # Command failed or possibly timed out
            msg = "Error occurred running '{}': {}".format(fi_info, error)
            self.fail(msg)

        # Clean up string
        providers = []
        for line in output.stdout.splitlines():
            if "provider" in line:
                prov = line.strip().split()[-1]
                if prov not in providers:
                    providers.append(prov)

        return providers[0]

    def test_dmg_network_scan_basic(self):
        """
        JIRA ID: DAOS-2516
        Test Description: Test basic dmg functionality to scan the network
        devices on the system.
        :avocado: tags=all,tiny,pr,hw,dmg,network_scan,basic
        """
        # Create dmg command
        dmg = DmgCommand(os.path.join(self.prefix, "bin"))
        dmg.get_params(self)

        # Update hostlist value for dmg command
        port = self.params.get("port", "/run/server_config/*")
        servers_with_ports = [
            "{}:{}".format(host, port) for host in self.hostlist_servers]
        dmg.hostlist.update(",".join(servers_with_ports), "dmg.hostlist")

        try:
            dmg_cmd_out = dmg.run()
        except process.CmdError as details:
            self.fail("dmg command failed: {}".format(details))

        # This parse asumes that the device information is listed on separate
        # lines and with this format 'info_type: info'
        if isinstance(dmg_cmd_out.stdout, str):
            pout = {}
            net_items = ["numa_node:", "iface:", "provider:"]
            for i, line in enumerate(dmg_cmd_out.stdout.splitlines()):
                for j, item in enumerate(net_items):
                    if item in line:
                        pout[i + j] = line.strip('\t')

        # Format the dict of values into list pairs
        dmg_out = [pout.values()[i:(i + 3)] for i in range(0, len(pout), 3)]
        self.log.info("Received data from dmg output: %s", str(dmg_out))

        # Validate dmg output against 3rd party tools: lstopo, fi_info
        covered = []
        for dev in dmg_out:
            dmg_iface = dev[0].split()[1]
            dmg_numa = int(dev[1].split()[1])
            dmg_prov = dev[2].split()[1].split("+")[1]
            tr = {"ib0": "hfi1_0", "ib1": "hfi1_1"}
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
