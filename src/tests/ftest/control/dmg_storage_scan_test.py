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

from server_utils import ServerCmdUtils
from apricot import TestWithoutServers
# from daos_api import DaosApiError

class StorageScanTest(TestWithoutServers):
    """
    Simple test to verify the scan function of the daos_server.

    :avocado: recursive
    """
    def __init__(self, *args, **kwargs):
        super(StorageScanTest, self).__init__(*args, **kwargs)

    # def setUp(self):
    #     try:
    #         super(StorageScanTest, self).setup()
    #     except DaosApiError as excpn:
    #         print(excpn)
    #         print(traceback.format__exc())
    #         self.fail("Test failed during setup. \n")

    def test_server_storage_scan_basic(self):
        """
        Test basic daos_server functionality, to scan the storage on system.

        :avocado: tags=all,pr,dmg,control,amanda
        """

        # Create daos_server command
        # daos_server = SvrCmdline("/run/server/*")
        # daos_server.run(self.basepath)

        # Verify that command ran

        # Create dmg command
        print("Running daos_server command line!")
        daos_server = ServerCmdUtils()
        # daos_server.command.set_yaml_value("command",self, "/run/daos_server/*")
        # print(daos_server.command.value)
        daos_server.execute(self, self.basepath)

        # Verify that command ran
