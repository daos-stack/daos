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

from dmg_utils import DmgCommand
from apricot import TestWithServers
from avocado.utils import process


class DmgNvmeScanTest(TestWithServers):
    """Test Class Description:
    Simple test to verify the scan function of the dmg tool.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DmgNvmeScanTest object."""
        super(DmgNvmeScanTest, self).__init__(*args, **kwargs)
        self.setup_start_agents = False

    def test_dmg_nvme_scan_basic(self):
        """
        JIRA ID: DAOS-2485
        Test Description: Test basic dmg functionality to scan the nvme storage.
        on the system.
        :avocado: tags=all,tiny,daily_regression,dmg,nvme_scan,basic
        """
        # Create dmg command
        dmg = DmgCommand(os.path.join(self.prefix, "bin"))
        dmg.get_params(self)

        # Update hostlist value for dmg command
        port = self.params.get("port", "/run/server_config/*")
        servers_with_ports = [
            "{}:{}".format(host, port) for host in self.hostlist_servers]
        dmg.hostlist = servers_with_ports

        try:
            dmg.run()
        except process.CmdError as details:
            self.fail("dmg command failed: {}".format(details))
