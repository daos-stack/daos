'''
  (C) Copyright 2020-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from ec_utils import ErasureCodeMdtest
from oclass_utils import extract_redundancy_factor


class EcodOnlineRebuildMdtest(ErasureCodeMdtest):
    """EC MDtest on-line rebuild test class.

    Test Class Description: To validate Erasure code object type classes works
                            fine for MDtest benchmark for on-line rebuild.

    :avocado: recursive
    """
    def test_ec_online_rebuild_mdtest(self):
        """Jira ID: DAOS-7320.

        Test Description:
            Test EC object class with MDtest for on-line rebuild.
        Use Cases:
            Create the pool and run MDtest with EC object class.
            While MDtest is running kill a number of ranks equal to the RF.
            Rebuild should finish without any problem.
            MDtest should finish without any failure.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=ec,ec_array,mdtest,ec_online_rebuild
        :avocado: tags=EcodOnlineRebuildMdtest,test_ec_online_rebuild_mdtest
        """
        # Stop a number of ranks equal to the object RF
        num_ranks_to_kill = extract_redundancy_factor(
            self.params.get("dfs_oclass", "/run/mdtest/*"))
        ranks_to_stop = self.random.sample(
            list(self.server_managers[0].ranks), k=num_ranks_to_kill)
        ranks_to_stop = self.random.sample(list(self.server_managers[0].ranks), k=1)
        self.start_online_mdtest(ranks_to_stop)
