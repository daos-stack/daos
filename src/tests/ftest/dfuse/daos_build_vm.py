"""
  (C) Copyright 2020-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers
from daos_build import run_build_test


class DaosBuildVM(TestWithServers):
    """Build DAOS over dfuse.

    :avocado: recursive
    """

    def test_dfuse_daos_build_wt_il(self):
        """This test builds DAOS on a dfuse filesystem.

        Use cases:
            Create Pool
            Create Posix container
            Mount dfuse
            Checkout and build DAOS sources.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=daosio,dfs,dfuse,ioil
        :avocado: tags=DaosBuildVM,test_dfuse_daos_build_wt_il
        """
        run_build_test(self, "writethrough", il_lib='libioil.so', run_on_vms=True)

    def test_dfuse_daos_build_wt_pil4dfs(self):
        """This test builds DAOS on a dfuse filesystem.

        Use cases:
            Create Pool
            Create Posix container
            Mount dfuse
            Checkout and build DAOS sources.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=daosio,dfs,dfuse,pil4dfs
        :avocado: tags=DaosBuildVM,test_dfuse_daos_build_wt_pil4dfs
        """
        run_build_test(self, "nocache", il_lib='libpil4dfs.so', run_on_vms=True)
