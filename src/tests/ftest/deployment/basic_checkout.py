#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from ior_test_base import IorTestBase
from mdtest_test_base import MdtestBase
from data_mover_test_base import DataMoverTestBase
from command_utils_base import CommandFailure

class BasicCheckout(IorTestBase, MdtestBase):
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
        flags = self.params.get("ior_flags", '/run/ior/iorflags/*')
        apis = self.params.get("ior_api", '/run/ior/iorflags/*')
        dfuse_mount_dir = self.params.get("mount_dir", "/run/dfuse/*")
        transfer_block_size = self.params.get("transfer_block_size",
                                              '/run/ior/iorflags/*')
        obj_class = self.params.get("obj_class", '/run/ior/iorflags/*')
        ec_obj_class = self.params.get("ec_oclass", '/run/ior/*')
        mdtest_params = self.params.get("mdtest_params", "/run/mdtest/*")


        #run ior
        results = self.run_ior_multiple_variants(obj_class, apis, transfer_block_size,
                                                 flags, dfuse_mount_dir)

        #run ior with different ec oclass
        results_ec = self.run_ior_multiple_variants(ec_obj_class, [apis[0]],
                                                    [transfer_block_size[1]],
                                                    [flags[0]], dfuse_mount_dir)
        results = results + results_ec
        self.log.error("Summary of IOR small test results:")
        errors = False
        for item in results:
            self.log.info("  %s  %s", item[0], item[1])
            if item[0] == "FAIL":
                errors = True
        if errors:
            self.fail("Test FAILED")

        #run mdtest
        self.run_mdtest_multiple_variants(mdtest_params)

        #run autotest
        self.log.info("Autotest start")
        daos_cmd = self.get_daos_command()
        try:
            daos_cmd.pool_autotest(pool=self.pool.uuid)
            self.log.info("daos pool autotest passed.")
        except CommandFailure as error:
            self.log.error("Error: %s", error)
            self.fail("daos pool autotest failed!")


class BasicCheckoutDm(DataMoverTestBase):
    # pylint: disable=too-few-public-methods
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
        #run datamover
        self.run_dm_activities_with_ior("FS_COPY", True)
