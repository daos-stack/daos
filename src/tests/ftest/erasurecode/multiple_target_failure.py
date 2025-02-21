'''
  (C) Copyright 2021-2022 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from ec_utils import ErasureCodeIor
from oclass_utils import extract_redundancy_factor


class EcodOnlineMultiTargetFail(ErasureCodeIor):
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

    def run_ior_cascade_failure(self):
        """Common function to Write and Read IOR"""
        # Write IOR data set with different EC object. kill rank, targets or mix of both while IOR
        # Write phase is in progress.
        self.log_step(
            f"Write datasets using IOR and exclude target {self.pool_exclude} while IOR is running")
        self.ior_write_dataset()

        # Disabled Online rebuild
        self.set_online_rebuild = False

        # Read IOR data and verify for EC object again
        # EC data was written with +2 parity so after killing ranks of targets data should be
        # intact and no data corruption observed.
        self.log_step(f"Read datasets using IOR after exclude target {self.pool_exclude}")
        self.ior_read_dataset(parity=2)

    def test_ec_multiple_targets_on_same_rank(self):
        """Jira ID: DAOS-7344.

        Test Description: Test Erasure code object with IOR with multiple targets failure
        Use Case: Create the pool, run IOR with supported EC object type class, kill multiple
                  targets on same rank, while IOR Write phase is in progress, verify all IOR write
                  finish.Read and verify data.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=ec,ec_array,ec_online_rebuild,rebuild,ec_fault,ec_multiple_failure
        :avocado: tags=EcodOnlineMultiTargetFail,test_ec_multiple_targets_on_same_rank
        """
        # Kill Two targets 2,4 from same rank 2
        self.pool_exclude[2] = "2,4"
        self.run_ior_cascade_failure()

    def test_ec_multiple_targets_on_diff_ranks(self):
        """Jira ID: DAOS-7344.

        Test Description: Test Erasure code object with IOR with multiple targets failure
        Use Case: Create the pool, run IOR with supported EC object type class, kill multiple
                  server ranks, while IOR Write phase is in progress, verify all IOR write
                  finish.Read and verify data.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=ec,ec_array,ec_online_rebuild,rebuild,ec_fault,ec_multiple_failure
        :avocado: tags=EcodOnlineMultiTargetFail,test_ec_multiple_targets_on_diff_ranks
        """
        # Exclude a number of targets equal to the object RF, each from a different rank
        num_ranks_to_kill = extract_redundancy_factor(self.obj_class[0][0])
        num_targets = self.server_managers[-1].get_config_value("targets")
        for rank in self.random.sample(list(self.server_managers[0].ranks), k=num_ranks_to_kill):
            self.pool_exclude[rank] = str(self.random.choice(range(num_targets)))
        self.run_ior_cascade_failure()

    def test_ec_single_target_rank_failure(self):
        """Jira ID: DAOS-7344.

        Test Description: Test Erasure code object with IOR with single target and rank failure
        Use Case: Create the pool, run IOR with supported EC object type class, kill single
                  server rank and target, while IOR Write phase is in progress, verify all IOR write
                  finish.Read and verify data.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=ec,ec_online_rebuild,rebuild,ec_fault,ec_multiple_failure
        :avocado: tags=EcodOnlineMultiTargetFail,test_ec_single_target_rank_failure
        """
        # Kill One server rank
        self.rank_to_kill = self.random.sample(list(self.server_managers[0].ranks), k=1)
        # Kill single target from single rank
        self.pool_exclude[3] = "3"
        self.run_ior_cascade_failure()
