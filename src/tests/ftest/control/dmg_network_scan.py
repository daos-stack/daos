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

    def test_dmg_network_scan_basic(self):
        """
        JIRA ID: DAOS-2516
        Test Description: Test basic dmg functionality to scan the network
        devices on the system.
        :avocado: tags=all,tiny,pr,dmg,network_scan,basic
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
                        pout[i+j] = line.strip('\t')

        dmg_dev = []
        exp_dev = self.params.get("options", "/run/expected/")
        dmg_dev_list = [list(pout.values())[i:i + len(exp_dev)]
                        for i in range(0, len(pout.items()), len(exp_dev))]

        # Convert dmg list of string to list of tuples
        for sub_list in dmg_dev_list:
            dmg_dev.append([tuple(map(str, sub.split(': ')))
                            for sub in sub_list])

        # Verify
        try:
            for i, device in enumerate(exp_dev):
                device.sort(key=lambda x: x[0])
                dmg_dev[i].sort(key=lambda x: x[0])
                if device not in dmg_dev:
                    self.fail("Could not find device information on dmg ouput.")
        except DmgFailure as error:
            self.log.error(str(error))
            self.fail("Test failed during dmg output verification.")
