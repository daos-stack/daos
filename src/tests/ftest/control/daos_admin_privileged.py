#!/usr/bin/python
"""
  (C) Copyright 2018-2020 Intel Corporation.

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
import getpass

from dmg_utils import storage_format
from server_utils import ServerManager, ServerFailed
from command_utils import CommandFailure
from apricot import TestWithServers


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
        self.setup_start_servers = False

    def test_daos_admin_format(self):
        """
        JIRA ID: DAOS-2895
        Test Description: Test daso_admin functionality to perform format
        privileged operations while daos_server is run as normal user.
        :avocado: tags=all,pr,hw,small,daos_admin,basic
        """
        # Verify that daos_admin has the correct permissions
        self.log.info("Checking daos_admin binary permissions")
        file_stats = os.stat("/usr/bin/daos_admin")
        file_perms = oct(file_stats.st_mode)[-4:]
        if file_perms != '4755':
            self.fail("Incorrect daos_admin permissions: {}".format(file_perms))

        # Setup server as non-root
        self.log.info("Preparing to run daos_server as non-root user")
        server = ServerManager(self.bin, os.path.join(self.ompi_prefix, "bin"))
        server.get_params(self)
        server.hosts = (
            self.hostlist_servers, self.workdir, self.hostfile_servers_slots)

        if self.prefix != "/usr":
            if server.runner.export.value is None:
                server.runner.export.value = []
            server.runner.export.value.extend(["PATH"])

        yamlfile = os.path.join(self.tmp, "daos_avocado_test.yaml")
        server.runner.job.set_config(yamlfile)
        server.server_clean()

        # Get user
        user = getpass.getuser()

        # Prep server for format, run command under non-root user
        self.log.info("Performing SCM storage prepare")
        try:
            server.storage_prepare(user, "dcpm")
        except ServerFailed as err:
            self.fail("Failed preparing SCM as non-root user: {}".format(err))

        # Uncomment the below line after DAOS-4287 is resolved
        # Prep server for format, run command under non-root user
        # self.log.info("Performing NVMe storage prepare")
        # try:
        #     server.storage_prepare(user, "nvme")
        # except ServerFailed as err:
        #     self.fail("Failed preparing nvme as non-root user: {}".format(err))

        # Start server
        try:
            self.log.info("Starting server as non-root user")
            server.runner.job.mode = "format"
            server.run()
        except CommandFailure as err:
            # Kill the subprocess, anything that might have started
            server.kill()
            self.fail("Failed starting server as non-root user: {}".format(err))

        # Update hostlist value for dmg command
        port = self.params.get("port", "/run/server_config/*")
        h_ports = [
            "{}:{}".format(host, port) for host in self.hostlist_servers]

        # Run format command under non-root user
        self.log.info("Performing SCM format")
        format_res = storage_format(os.path.join(self.prefix, "bin"), h_ports)
        if format_res is None:
            self.fail("Failed to format storage")

        # Stop server
        try:
            server.stop()
        except ServerFailed as err:
            self.fail("Failed to stop server: {}".format(err))
