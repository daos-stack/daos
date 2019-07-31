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
from ior_test_base import IorTestBase


class IorFourServers(IorTestBase):
    """Test class Description: Runs IOR with 4 servers.

    :avocado: recursive
    """

    def test_fourservers(self):
        """Jira ID: DAOS-1263.

        Test Description:
            Test IOR with four servers.

        Use Cases:
            Different combinations of 1/64/128 Clients,
            1K/4K/32K/128K/512K/1M transfer size.

        :avocado: tags=ior,fourservers
        """
        self.run_ior_with_pool()
