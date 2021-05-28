#!/usr/bin/python3
"""
(C) Copyright 2018-2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
from ior_test_base import IorTestBase
from avocado.core.exceptions import TestFail
from general_utils import get_random_string


class IorSmall(IorTestBase):
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-few-public-methods
    """Test class Description: Runs IOR with 1 server with basic parameters.

    :avocado: recursive
    """

    def test_ior_small(self):
        """Jira ID: DAOS-2715, DAOS-3657, DAOS-4909.

        Test Description:
            Purpose of this test is to have small ior test to check basic
            functionality for DFS, MPIIO and HDF5 api

        Use case:
            Run ior with read, write, CheckWrite, CheckRead in ssf mode.
            Run ior with read, write, CheckWrite, CheckRead in fpp mode.
            Run ior with read, write, CheckWrite and access to random
                offset instead of sequential.
            All above three cases to be run with single client and
                multiple client processes in two separate nodes.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=daosio,checksum,iorsmall,mpich
        :avocado: tags=DAOS_5610
        """
        results = []
        cncl_tickets = []
        dfuse_mount_dir = None
        ior_timeout = self.params.get("ior_timeout", '/run/ior/*')
        flags = self.params.get("ior_flags", '/run/ior/iorflags/*')
        apis = self.params.get("ior_api", '/run/ior/iorflags/*')
        mount_dir = self.params.get("mount_dir", "/run/dfuse/*")
        transfer_block_size = self.params.get("transfer_block_size",
                                              '/run/ior/iorflags/*')
        obj_class = self.params.get("obj_class", '/run/ior/iorflags/*')

        for oclass in obj_class:
            self.ior_cmd.dfs_oclass.update(oclass)
            for api in apis:
                if api == "HDF5-VOL":
                    self.ior_cmd.api.update("HDF5")
                    hdf5_plugin_path = self.params.get(
                        "plugin_path", '/run/hdf5_vol/*')
                    flags_w_k = " ".join([flags[0]] + ["-k"])
                    self.ior_cmd.flags.update(flags_w_k, "ior.flags")
                else:
                    # run tests for different variants
                    self.ior_cmd.flags.update(flags[0], "ior.flags")
                    hdf5_plugin_path = None
                    self.ior_cmd.api.update(api)
                for test in transfer_block_size:
                    # update transfer and block size
                    self.ior_cmd.transfer_size.update(test[0])
                    self.ior_cmd.block_size.update(test[1])
                    # run ior
                    if api == "HDF5-VOL":
                        sub_dir = get_random_string(5)
                        dfuse_mount_dir = os.path.join(mount_dir, sub_dir)
                    try:
                        self.run_ior_with_pool(
                            plugin_path=hdf5_plugin_path, timeout=ior_timeout,
                            mount_dir=dfuse_mount_dir)
                        results.append(["PASS", str(self.ior_cmd)])
                    except TestFail:
                        results.append(["FAIL", str(self.ior_cmd)])

        # Running a variant for ior fpp
        self.ior_cmd.flags.update(flags[1])
        self.ior_cmd.api.update(apis[0])
        self.ior_cmd.block_size.update((transfer_block_size[1])[1])
        self.ior_cmd.transfer_size.update((transfer_block_size[1])[0])
        self.ior_cmd.dfs_oclass.update(obj_class[0])
        # run ior
        try:
            self.run_ior_with_pool(plugin_path=None, timeout=ior_timeout)
            results.append(["PASS", str(self.ior_cmd)])
        except TestFail:
            results.append(["FAIL", str(self.ior_cmd)])

        self.log.error("Summary of IOR small test results:")
        errors = False
        for item in results:
            self.log.info("  %s  %s", item[0], item[1])
            if item[0] == "FAIL":
                errors = True
        if errors:
            self.fail("Test FAILED")
        if cncl_tickets:
            self.cancelForTicket(",".join(cncl_tickets))
