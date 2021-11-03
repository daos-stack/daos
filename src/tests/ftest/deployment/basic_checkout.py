#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from small import IorSmall
from mdtest.small import MdtestSmall
from autotest import ContainerAutotestTest
from ior_smoke import EcodIor
from large_file import DmvrPosixLargeFile

class BasicCheckout(IorSmall, MdtestSmall, ContainerAutotestTest, EcodIor):
    # pylint: disable=too-many-ancestors
    """Test Class Description: Test class wrapping up tests from four
                               different test classes into one. Intent
                               is to run these tests as a basic checkout
                               for newly installed servers.
    :avocado: recursive
    """

    def test_basic_checkout(self):
        """
        Test Description: Bundles four tests into one and run in the
                          following sequence - ior_small, mdtest_small,
                          ec_smoke and autotest.
        :avocado: tags=installation,basiccheckout
        """
        # local param
        ec_ior_flags = self.params.get("ec_ior_flags", "/run/ior/*")

        # run tests
        self.test_ior_small()
        self.test_mdtest_small()

        self.ior_cmd.flags.update(ec_ior_flags)
        self.test_ec()

        self.container.destroy()
        self.pool.destroy()

        self.test_container_autotest()

class BasicCheckoutDm(DmvrPosixLargeFile):
    # pylint: disable=too-many-ancestors
    """Test Class Description: Test class to wrap datamover test to
                               run as part of basic checkout and verify
                               connectivity for lustre FS
    :avocado: recursive
    """

    def test_basic_checkout_dm(self):
        """
        Test Description: Datamover test to check connection and datamover
                          functionality with Lustre fs on newly installed
                          server nodes.
        :avocado: tags=installation,basiccheckout
        """
        # local param
        dm_ior_options = self.params.get("dm_ior_options", "/run/ior/dm/*")

        # update ior params and run dm test
        self.ior_cmd.flags.update(dm_ior_options[0])
        self.ior_cmd.signature.update(dm_ior_options[1])
        self.ior_cmd.transfer_size.update(dm_ior_options[2])
        self.ior_cmd.block_size.update(dm_ior_options[3])
        self.ior_cmd.dfs_dir_oclass.update(dm_ior_options[4])
        self.ior_cmd.dfs_oclass.update(dm_ior_options[5])
        self.ior_cmd.test_file.update(dm_ior_options[6])
        self.ior_cmd.repetitions.update(dm_ior_options[7])
        self.test_dm_large_file_fs_copy()
