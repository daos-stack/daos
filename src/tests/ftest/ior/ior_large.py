#!/usr/bin/python3
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from ior_test_base import IorTestBase
from apricot import skipForTicket


class IorLarge(IorTestBase):
    """Test class Description: Runs IOR with different
                               number of servers.

    :avocado: recursive
    """

    @skipForTicket("DAOS-7257")
    def test_sequential(self):
        """Jira ID: DAOS-1264.

        Test Description:
            Run IOR with 1,64 and 128 clients config sequentially.

        Use Cases:
            Different combinations of 1/64/128 Clients and
            1K/4K/32K/128K/512K/1M transfersize.

        :avocado: tags=all
        :avocado: tags=hw,large
        :avocado: tags=daosio
        :avocado: tags=iorlarge_sequential,iorlarge
        """
        ior_flags = self.params.get("F", "/run/ior/iorflags/sequential/")
        self.ior_cmd.flags.update(ior_flags)
        self.run_ior_with_pool()

    @skipForTicket("DAOS-7257")
    def test_random(self):
        """Jira ID: DAOS-1264.

        Test Description:
            Run IOR with 1,64 and 128 clients config in random order.

        Use Cases:
            Different combinations of 1/64/128 Clients and
            1K/4K/32K/128K/512K/1M transfersize.

        :avocado: tags=all
        :avocado: tags=hw,large
        :avocado: tags=daosio
        :avocado: tags=iorlarge_random,iorlarge
        """
        ior_flags = self.params.get("F", "/run/ior/iorflags/random/")
        self.ior_cmd.flags.update(ior_flags)
        self.run_ior_with_pool()

    @skipForTicket("DAOS-7257")
    def test_fpp(self):
        """Jira ID: DAOS-2491.

        Test Description:
            Run IOR with 1,64 and 128 clients config file-per-process.

        Use Cases:
            Different combinations of 1/64/128 Clients and
            1K/4K/32K/128K/512K/1M transfersize.

        :avocado: tags=all
        :avocado: tags=hw,large
        :avocado: tags=daosio
        :avocado: tags=iorlarge_fpp,iorlarge
        """
        ior_flags = self.params.get("F", "/run/ior/iorflags/fpp/")
        self.ior_cmd.flags.update(ior_flags)
        self.run_ior_with_pool()
