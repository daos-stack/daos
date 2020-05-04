#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

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

from control_test_base import ControlTestBase


class DmgStorageReformatTest(ControlTestBase):
    """Test Class Description:
    Test to verify that dmg storage command reformat option.
    :avocado: recursive
    """

    def test_dmg_storage_reformat(self):
        """
        JIRA ID: DAOS-3854
        Test Description: Test dmg storage reformat functionality.
        :avocado: tags=all,small,full_regression,hw,control,reformat,dmg,basic
        """

        # At this point the server has been started, storage has been formatted
        # We need to get the superblock file information
        sp_info = {}
        for server in self.server_managers:
            scm_mount = server.get_config_value("scm_mount")
            self.get_superblock_info(scm_mount, "uuid")


