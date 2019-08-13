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

from dmg_utils import DmgCommand
from server_utils import ServerCommand
from apricot import TestWithoutServers
from avocado.utils import process

class DmgNvmeScanTest(TestWithoutServers):
    """Test Class Description:
    Simple test to verify the scan function of the dmg tool.
    :avocado: recursive
    """

    def test_dmg_nvme_scan_basic(self):
        """
        JIRA ID: DAOS-2485
        Test Description: Test basic dmg functionality to scan nvme the storage
        on system.
        :avocado: tags=all,hw,dmg,control
        """
        # Create daos_server command
        server = ServerCommand(self.hostlist_servers)
        server.get_params(self, "/run/daos_server/*")

        # Update config and start server
        server.update_configuration(self.basepath)
        server.prepare(self.workdir, self.hostfile_servers_slots)
        server.start(self.orterun)

        # Create daos_shell command
        dmg = DmgCommand()
        dmg.get_params(self, "/run/dmg/*")

        # Update hostlist value for dmg command
        ports = self.params.get("ports", "/run/hosts/*")

        # Check that hosts and ports are same length
        self.assertEqual(ports, self.hostlist_servers)

        servers_with_ports = ["{}:{}".format(host, ports[i])
                              for i, host in enumerate(self.hostlist_servers)]
        dmg.hostlist.update(",".join(servers_with_ports), "dmg.hostlist")

        try:
            dmg.run()
        except process.CmdError as details:
            self.fail("daos_shell command failed: {}".format(details))

        # Cleanup/kill server
        server.stop()
