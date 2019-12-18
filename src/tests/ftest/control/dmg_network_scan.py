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

from dmg_utils import DmgCommand, DmgFailure
from apricot import TestWithServers
from avocado.utils import process


class DmgNetworkScanTest(TestWithServers):
    """Test Class Description:
    Simple test to verify the network scan function of the dmg tool.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DmgNetworkScanTest object."""
        super(DmgNetworkScanTest, self).__init__(*args, **kwargs)
        self.setup_start_agents = False
        self.setup_start_servers = False

    def get_net_info(self, provider):
        """Get expected values of domain with fi_info."""
        fi_info = os.path.join(
            self.bin, "fi_info -p {} | grep domain | sort -u".format(provider))
        kwargs = {
            "cmd": fi_info,
            "allow_output_check": "combined",
            "verbose": False,
            "shell": True,
        }
        try:
            output = process.run(**kwargs)
        except process.CmdError as error:
            # Command failed or possibly timed out
            msg = "Error occurred running '{}': {}".format(fi_info, error)
            self.fail(msg)

        device = []
        if (output.stdout != ""):
            device = [line.split()[-1] for line in output.stdout.splitlines()]

        return device

    def get_numa_info(self):
        """Get expected values of numa nodes with lstopo."""
        lstopo = os.path.join(self.bin, "lstopo")
        kwargs = {
            "cmd": lstopo,
            "allow_output_check": "combined",
            "verbose": False,
            "shell": True,
        }
        try:
            output = process.run(**kwargs)
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
        """Get expected values of provider with fi_info."""
        fi_info = os.path.join(
            self.bin, "fi_info -d {} | grep provider | sort -u".format(device))
        kwargs = {
            "cmd": fi_info,
            "allow_output_check": "combined",
            "verbose": False,
            "shell": True,
        }
        try:
            output = process.run(**kwargs)
        except process.CmdError as error:
            # Command failed or possibly timed out
            msg = "Error occurred running '{}': {}".format(fi_info, error)
            self.fail(msg)

        # Clean up string
        provider = "none"
        if (output.stdout != ""):
            provider = output.stdout.splitlines()[0].split()[-1]

        return provider

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
            dmg_out = dmg.run()
        except process.CmdError as details:
            self.fail("dmg command failed: {}".format(details))

        # This parse asumes that the device information is listed on separate
        # lines and with this format 'info_type: info'
        if isinstance(dmg_out.stdout, str):
            pout = {}
            net_items = ["numa_node:", "iface:", "provider:"]
            for i, line in enumerate(dmg_out.stdout.splitlines()):
                for j, item in enumerate(net_items):
                    if item in line:
                        pout[i + j] = line.strip('\t')

        # Get info from tools
        exp_out = []
        exp_devs = self.get_net_info("sockets") + self.get_net_info("psm2")
        t_hfi = {"hfi1_0": "ib0", "hfi1_1": "ib1"}
        for numa, devs in self.get_numa_info().items():
            if devs:
                n_devs = [dev for dev in devs if dev in exp_devs]
                print("Numa devs: {}".format(n_devs))
                numa = [numa] * len(n_devs)
                dev_prov = {dev: self.get_dev_provider(dev) for dev in n_devs}
                for n, d, p in zip(numa, dev_prov.keys(), dev_prov.values()):
                    if d in t_hfi:
                        d = t_hfi[d]
                    net_obj = [
                        'fabric_iface: ' + d,
                        'provider: ofi+' + p,
                        'pinned_numa_node: ' + str(n),
                    ]
                    exp_out.append(net_obj)

        # Format the dict of values into list pairs
        # dmg_out = [list(pout.values())[i:(i + len(exp_out)-1)]
        #                 for i in range(0, len(pout.items()), len(exp_out))]

        print("dmg_out: {}".format(dmg_out))
        print("exp_out: {}".format(exp_out))

        # Verify
        try:
            for i, device in enumerate(exp_out):
                device.sort(key=lambda x: x[0])
                dmg_out[i].sort(key=lambda x: x[0])
                if device not in dmg_out:
                    self.fail("Could not find device information on dmg ouput.")
        except DmgFailure as error:
            self.log.error(str(error))
            self.fail("Test failed during dmg output verification.")
