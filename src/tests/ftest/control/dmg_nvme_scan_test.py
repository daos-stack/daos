#!/usr/bin/python
'''
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
kworker/16:1
  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
'''
from __future__ import print_function

import os
import time

from dmg_utils import DmgCommand
from server_utils import ServerCommand
from apricot import TestWithoutServers
from avocado.utils import process

class DmgNvmeScanTest(TestWithoutServers):
    """ Simple test to verify the scan function of the dmg tool.
    :avocado: recursive
    """
    def __init__(self, *args, **kwargs):
        super(DmgNvmeScanTest, self).__init__(*args, **kwargs)

    def cleanup(self):
        # pylint: disable=pylint-no-self-use
        """ Setup/cleanup for the daos_server to run properly."""

        umount_daos = "umount /mnt/daos; rm -rf /mnt/daos"
        rm_sockets = "rm -rf /tmp/daos_sockets/"
        rm_logs = "rm -rf /tmp/*.log"

        try:
            process.run(
                umount_daos, verbose=True, ignore_status=True, sudo=True)
        except Exception:
            raise process.CmdError(umount_daos)

        try:
            process.run(
                rm_sockets, verbose=True, ignore_status=True, sudo=True)
        except Exception:
            raise process.CmdError(rm_sockets)

        try:
            process.run(
                rm_logs, verbose=True, ignore_status=True, sudo=True)
        except Exception:
            raise process.CmdError(rm_logs)

    def test_dmg_nvme_scan_basic(self):
        """ Test basic dmg functionality to scan nvme the storage on system.
        :avocado: tags=all,hw,dmg,control
        """
        self.cleanup()

        # Create daos_server command
        server = ServerCommand()
        server.get_params(self, "/run/daos_server/*")

        # Edit the config value to full path
        server.config.value = os.path.join(
            self.basepath, str(server.config.value))

        # Run server as a background process.
        svr_subproc = server.subproc()
        try:
            pid = svr_subproc.start()
        except process.CmdError as details:
            self.fail("Server command failed: {}".format(details))

        # Wait for server to start
        time.sleep(3)

        # Create daos_shell command
        dmg = DmgCommand("daos_shell")
        dmg.get_params(self, "/run/daos_shell/*")

        # Get server for daos_shell command and assign hostlist value
        test_machines = self.params.get("test_machines", "/run/hosts/*")
        for i, machine in enumerate(test_machines):
            test_machines[i] = "{}:{}".format(machine, dmg.port.value)

        dmg.hostlist.value = ",".join(test_machines)

        try:
            dmg.run()
        except process.CmdError as details:
            self.fail("daos_shell command failed: {}".format(details))

        # Cleanup/kill subprocess
        try:
            process.run("sudo kill -9 {}".format(pid), sudo=True)
        except process.CmdError as details:
            self.fail("Failed to kill server subprocess: {}".format(details))
