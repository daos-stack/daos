#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
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
