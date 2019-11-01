#!/usr/bin/python
"""
  (C) Copyright 2019 Intel Corporation.

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


class IorEightServersMpiio(IorTestBase):
    """Test class Description: Runs IOR with 8 servers with MPIIO.

    :avocado: recursive
    """

    def test_ssf(self):
        """Test ID: DAOS-2121.

        Test Description:
            Run IOR with 1,64 and 128 clients config in ssf mode.

        Use Cases:
            Different combinations of 1/64/128 Clients, 1K/4K/32K/128K/512K/1M
            transfersize and block size of 32M for 1K transfer size and 128M
            for rest.

        :avocado: tags=ior_ssf
        """
        ior_flags = self.params.get("F", "/run/ior/iorflags/ssf/")
        self.ior_cmd.flags.update(ior_flags)
        self.run_ior_with_pool()

    def test_fpp(self):
        """Test ID: DAOS-2121.

        Test Description:
            Run IOR with 1,64 and 128 clients config in fpp mode.

        Use Cases:
            Different combinations of 1/64/128 Clients, 1K/4K/32K/128K/512K/1M
            transfersize and block size of 32M for 1K transfer size and 128M
            for rest.

        :avocado: tags=ior_fpp
        """
        ior_flags = self.params.get("F", "/run/ior/iorflags/fpp/")
        self.ior_cmd.flags.update(ior_flags)
        self.run_ior_with_pool()
