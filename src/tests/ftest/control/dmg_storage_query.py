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


class DmgStorageQuery(TestWithServers):
    """Test Class Description:
    Test to verify dmg storage health query commands and device state commands.
    Including: storage query, storage blobstore-health, storage nvme-health,
    storage query device-state.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DmgStorageQuery object."""
        super(DmgStorageQuery, self).__init__(*args, **kwargs)
        self.setup_start_agents = False
        self.setup_start_servers = False

    def test_dmg_storage_query_smd(self):
        """
        JIRA ID: DAOS-3925
        Test Description: Test
        :avocado: tags=all,pr,hw,small,dmg_storage_query,smd,basic
        """

    def test_dmg_storage_query_blobstore_health(self):
        """
        JIRA ID: DAOS-3925
        Test Description: Test
        :avocado: tags=all,pr,hw,small,dmg_storage_query,blobstore,basic
        """

    def test_dmg_storage_query_nvme_health(self):
        """
        JIRA ID: DAOS-3925
        Test Description: Test
        :avocado: tags=all,pr,hw,small,dmg_storage_query,nvme,basic
        """

    def test_dmg_storage_query_device_state(self):
        """
        JIRA ID: DAOS-3925
        Test Description: Test
        :avocado: tags=all,pr,hw,small,dmg_storage_query,device_state,basic
        """
