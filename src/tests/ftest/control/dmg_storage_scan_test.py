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
import traceback

from dmg_utils import DmgCmdUtils
from server_utils import ServerCmdUtils
from cmd_utils import CommandLineFailure
from apricot import TestWithoutServers
from avocado.utils import process

class StorageScanTest(TestWithoutServers):
    """
    Simple test to verify the scan function of the daos_server.

    :avocado: recursive

    """
    def __init__(self, *args, **kwargs):
        super(StorageScanTest, self).__init__(*args, **kwargs)

    def cleanUp(self):
        """ Setup/cleanup for the daos_server to run properly."""

        umount_daos = "umount /mnt/daos; rm -rf /mnt/daos"
        rm_sockets = "rm -rf /tmp/daos_sockets/"
        rm_logs = "rm -rf /tmp/*.log"

        # Clean up the /mnt/daos dir and logs
        try:
            umount_result = process.run(
                umount_daos, verbose = True, ignore_status = True, sudo = True)
        except Exception as excpn:
            raise CommandLineFailure("<Command Error>:{}".format(umount_daos))

        try:
            rm_sockets_result = process.run(
                rm_sockets, verbose = True, ignore_status = True, sudo = True)
        except Exception as excpn:
            raise CommandLineFailure("<Command Error>:{}".format(rm_sockets))

        try:
            rm_logs_result = process.run(
                rm_logs, verbose = True, ignore_status = True, sudo = True)
        except Exception as excpn:
            raise CommandLineFailure("<Command Error>:{}".format(rm_logs))

    def test_server_storage_scan_basic(self):
        """
        Test basic daos_server functionality, to scan the storage on system.

        :avocado: tags=all,pr,dmg,control,amanda

        """
        # Cleanup /mnt/daos
        self.cleanUp()

        # Create and execute daos_server command in the background
        daos_server = ServerCmdUtils()
        daos_server_rc = daos_server.execute(self, self.basepath, bg = True)

        # Start background process
        daos_server_rc.start()

        # Create and execute daos_shell command
        daos_shell = DmgCmdUtils(tool = "daos_shell")
        daos_shell_rc = daos_shell.execute(self, self.basepath, bg = False)

        # Verify that command ran
        # TO-DO

        # Stop backgroung process
        daos_server_rc.stop()