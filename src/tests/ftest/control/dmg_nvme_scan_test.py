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
from control_test_base import ControlTestBase


class DmgNvmeScanTest(ControlTestBase):
    # pylint: disable=too-many-ancestors
    """Dmg command test.

    Test Class Description:
        Simple test to verify the scan function of the dmg tool.

    :avocado: recursive
    """

    def test_dmg_nvme_scan_basic(self):
        """JIRA ID: DAOS-2485.

        Test Description:
            Test basic dmg functionality to scan the nvme storage on the system.

        :avocado: tags=all,tiny,pr,dmg,nvme_scan,basic
        """
        self.run_dmg_command()
