#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from ior_test_base import IorTestBase


# pylint: disable=too-many-ancestors
class IoConsistency(IorTestBase):
    """Test class Description: Verify data consistency using different
                               middlewares. In this case, using DFS and POSIX.

    :avocado: recursive
    """

    def test_ioconsistency(self):
        """Jira ID: DAOS-4778.
        Test Description:
            Run IOR first using DFS and then using POSIX to verify data
            consistency.
        Use Cases:
            Create a pool
            Create POSIX type container.
            Run ior -a DFS with FPP and keep the file.
            Mount the container using dfuse
            Run IOR -a POSIX -r -R to verify data consistency and delete the
            file.
            Try to re-create to the same file name after deletion, which should
            work without issues.
            Repeat the same steps as above for SSF this time.
        :avocado: tags=all,daosio,hw,large,pr,daily_regression,ioconsistency
        :avocado: tags=DAOS_5610
        """

        # test params
        apis_flags = self.params.get("api_flag", "/run/ior/io_consistency/*")

        # loop for both the test cases of DFS and POSIX mentioned above
        # in use cases.
        for _, api_flag in enumerate(apis_flags):
            self.ior_cmd.api.update(api_flag[0])
            self.ior_cmd.flags.update(api_flag[1])
            if api_flag[0] == "POSIX":
                # if api is POSIX do not create new pool and container.
                # Also do not stop dfuse when ior command is complete.
                self.run_ior_with_pool(create_pool=False, stop_dfuse=False)
            else:
                self.run_ior_with_pool()

        # Try to re-create the file with same name and size.
        self.ior_cmd.flags.update(apis_flags[0][1])
        self.run_ior_with_pool(create_pool=False)
