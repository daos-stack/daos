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

from dmg_utils import DmgCommand, storage_format, storage_prep
from apricot import TestWithServers
from avocado.utils import process

class DaosAdminPrivTest(TestWithServers):
    """Test Class Description:
    Test to verify that daos_server when run as normal user, can perform
    privileged functions.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DaosAdminPrivTest object."""
        super(DaosAdminPrivTest, self).__init__(*args, **kwargs)
        self.setup_start_agents = False

    def test_daos_admin_format(self):
        """
        JIRA ID: DAOS-2895
        Test Description: Test daso_admin functionality to perform format
        privileged operations while daos_server is run as normal user.
        :avocado: tags=all,tiny,pr,daos_admin,basic
        """
        # Check the the server is being run as non-root user
        if self.server_managers[-1].runner.process.is_sudo_enabled():
            self.fail("The server was started under sudo.\n")

        # Verify that daos_admin has the correct permissions
        daos_admin = "/usr/bin/daos_admin"
        os.stat(daos_admin)

        # Update hostlist value for dmg command
        port = self.params.get("port", "/run/server_config/*")
        h_ports = [
            "{}:{}".format(host, port) for host in self.hostlist_servers]

        # Run format command under non-root user
        format_res = storage_format(os.path.join(self.prefix, "bin"), h_ports)

    def test_daos_admin_prepare(self):
        """
        JIRA ID: DAOS-2895
        Test Description: Test daso_admin functionality to perform prepare
        privileged operations while daos_server is run as normal user.
        :avocado: tags=all,tiny,pr,daos_admin,basic
        """
        # Verify that daos_admin has the correct permissions


        # Update hostlist value for dmg command
        port = self.params.get("port", "/run/server_config/*")
        servers_with_ports = [
            "{}:{}".format(host, port) for host in self.hostlist_servers]

        # Run format command under non-root user
        format_res = storage_format(
            os.path.join(self.prefix, "bin"), servers_with_ports, sudo=False)
