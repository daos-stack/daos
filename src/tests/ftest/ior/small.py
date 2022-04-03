#!/usr/bin/python3
"""
(C) Copyright 2018-2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from ior_test_base import IorTestBase
from avocado.core.exceptions import TestFail


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
        :avocado: tags=daosio,checksum,mpich,dfuse,DAOS_5610
        :avocado: tags=iorsmall
        """
        cncl_tickets = []
        flags = self.params.get("ior_flags", '/run/ior/iorflags/*')
        apis = self.params.get("ior_api", '/run/ior/iorflags/*')
        dfuse_mount_dir = self.params.get("mount_dir", "/run/dfuse/*")
        transfer_block_size = self.params.get("transfer_block_size",
                                              '/run/ior/iorflags/*')
        obj_class = self.params.get("obj_class", '/run/ior/iorflags/*')

        results = self.run_ior_multiple_variants(obj_class, apis, transfer_block_size,
                                                 flags, dfuse_mount_dir)
        # Running a variant for ior fpp
        self.ior_cmd.flags.update(flags[1])
        self.ior_cmd.api.update(apis[0])
        self.ior_cmd.block_size.update((transfer_block_size[1])[1])
        self.ior_cmd.transfer_size.update((transfer_block_size[1])[0])
        self.ior_cmd.dfs_oclass.update(obj_class[0])
        # run ior
        try:
            self.run_ior_with_pool(plugin_path=None, timeout=self.ior_timeout)
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
