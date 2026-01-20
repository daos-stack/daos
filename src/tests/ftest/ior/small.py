"""
(C) Copyright 2018-2024 Intel Corporation.
(C) Copyright 2025 Hewlett Packard Enterprise Development LP

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from avocado.core.exceptions import TestFail
from ior_test_base import IorTestBase


class IorSmall(IorTestBase):
    # pylint: disable=too-few-public-methods
    """Test class Description: Verify basic IOR functionality with various APIs.

    :avocado: recursive
    """

    def test_ior_small(self):
        """Jira ID: DAOS-2715, DAOS-3657, DAOS-4909, DAOS-9947.

        Test Description:
            Verify basic IOR functionality with various APIs.

        Use case:
            Run ior with Read, Write, CheckWrite, CheckRead in SSF mode.
            Repeat for various IOR APIs and configurations.
            Run ior with Read, Write, CheckWrite, CheckRead in FPP mode.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=daosio,mpiio,checksum,mpich,dfuse,ior,dfs,hdf5
        :avocado: tags=IorSmall,test_ior_small
        """
        cncl_tickets = []
        flags = self.params.get("ior_flags", '/run/ior/iorflags/*')
        apis = self.params.get("ior_api", '/run/ior/iorflags/*')
        transfer_block_size = self.params.get("transfer_block_size", '/run/ior/iorflags/*')
        obj_class = self.params.get("obj_class", '/run/ior/iorflags/*')

        results = self.run_ior_multiple_variants(obj_class, apis, transfer_block_size, flags)
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
