'''
  (C) Copyright 2021-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from ec_utils import ErasureCodeIor
from oclass_utils import extract_redundancy_factor


class EcodOnlineMultiRankFail(ErasureCodeIor):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: To validate Erasure code object data after killing multiple rank,targets
                            while IOR Write in progress.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a EcOnlineRebuild object."""
        super().__init__(*args, **kwargs)
        self.set_online_rebuild = True

    def test_ec_multiple_rank_failure(self):
        """Jira ID: DAOS-7344.

        Test Description: Test Erasure code object with IOR with multiple rank failure
        Use Case: Create the pool, run IOR with supported EC object type class, kill multiple
                  server ranks, while IOR Write phase is in progress, verify all IOR write
                  finish.Read and verify data.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=ec,ec_online_rebuild,rebuild,ec_fault,ec_multiple_failure
        :avocado: tags=EcodOnlineMultiRankFail,test_ec_multiple_rank_failure
        """
        # Kill a number of ranks equal to the object RF
        num_ranks_to_kill = extract_redundancy_factor(self.obj_class[0][0])
        self.rank_to_kill = self.random.sample(
            list(self.server_managers[0].ranks), k=num_ranks_to_kill)

        # Write IOR data set with different EC object. kill rank, targets or mix of both while IOR
        # Write phase is in progress.
        self.log_step(
            f"Write datasets using IOR and kill rank {self.rank_to_kill} while IOR is running")
        self.ior_write_dataset()

        # Disabled Online rebuild
        self.set_online_rebuild = False

        # Read IOR data and verify for EC object again
        # After killing ranks of targets data should be intact and no data corruption observed.
        self.log_step(f"Read datasets using IOR after killing rank {self.rank_to_kill}")
        self.ior_read_dataset(parity=num_ranks_to_kill)
