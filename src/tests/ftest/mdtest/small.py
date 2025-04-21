'''
  (C) Copyright 2019-2023 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from mdtest_test_base import MdtestBase


class MdtestSmall(MdtestBase):
    """Test class Description: Verify MDTest functionality with various configurations.

    :avocado: recursive
    """

    def test_mdtest_small(self):
        """Jira ID: DAOS-2493, DAOS-10054.

        Test Description:
            Verify MDTest functionality with various configurations.

        Use Cases:
            Run MDTest with various APIs and configurations.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=mdtest,checksum,dfs,dfuse
        :avocado: tags=MdtestSmall,test_mdtest_small
        """
        mdtest_params = self.params.get("mdtest_params", "/run/mdtest/*")
        self.run_mdtest_multiple_variants(mdtest_params)
