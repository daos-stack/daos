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


class IorLarge(IorTestBase):
    """Test class Description: Runs IOR with different
                               number of servers.

    :avocado: recursive
    """

    def test_sequential(self):
        """Jira ID: DAOS-1264.

        Test Description:
            Run IOR with 1,64 and 128 clients config sequentially.

        Use Cases:
            Different combinations of 1/64/128 Clients and
            1K/4K/32K/128K/512K/1M transfersize.

        :avocado: tags=all,daosio,iorlarge_sequential,iorlarge
        """
        ior_flags = self.params.get("F", "/run/ior/iorflags/sequential/")
        self.ior_cmd.flags.update(ior_flags)
        self.run_ior_with_pool()

    def test_random(self):
        """Jira ID: DAOS-1264.

        Test Description:
            Run IOR with 1,64 and 128 clients config in random order.

        Use Cases:
            Different combinations of 1/64/128 Clients and
            1K/4K/32K/128K/512K/1M transfersize.

        :avocado: tags=all,daosio,iorlarge_random,iorlarge
        """
        ior_flags = self.params.get("F", "/run/ior/iorflags/random/")
        self.ior_cmd.flags.update(ior_flags)
        self.run_ior_with_pool()

    def test_fpp(self):
        """Jira ID: DAOS-2491.

        Test Description:
            Run IOR with 1,64 and 128 clients config file-per-process.

        Use Cases:
            Different combinations of 1/64/128 Clients and
            1K/4K/32K/128K/512K/1M transfersize.

        :avocado: tags=all,daosio,iorlarge_fpp,iorlarge
        """
        ior_flags = self.params.get("F", "/run/ior/iorflags/fpp/")
        self.ior_cmd.flags.update(ior_flags)
        self.run_ior_with_pool()
