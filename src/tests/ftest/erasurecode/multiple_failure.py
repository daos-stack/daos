#!/usr/bin/python
'''
  (C) Copyright 2021-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from ec_utils import ErasureCodeIor
from apricot import skipForTicket

class EcodOnlineMultFail(ErasureCodeIor):
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

    @skipForTicket("DAOS-9051")
    def run_ior_cascade_failure(self):
        """Common function to Write and Read IOR"""
        # Write IOR data set with different EC object. kill rank, targets or mix of both while IOR
        # Write phase is in progress.
        self.ior_write_dataset()

        # Disabled Online rebuild
        self.set_online_rebuild = False

        # Read IOR data and verify for EC object again
        # EC data was written with +2 parity so after killing ranks of targets data should be
        # intact and no data corruption observed.
        self.ior_read_dataset(parity=2)

    @skipForTicket("DAOS-9051")
    def test_ec_multiple_rank_failure(self):
        """Jira ID: DAOS-7344.

        Test Description: Test Erasure code object with IOR with multiple rank failure
        Use Case: Create the pool, run IOR with supported EC object type class, kill multiple
                  server ranks, while IOR Write phase is in progress, verify all IOR write
                  finish.Read and verify data.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,ec_online_rebuild,rebuild,ec_fault,ec_multiple_failure
        :avocado: tags=ec_multiple_rank_failure
        """
        # Kill Two server ranks
        self.rank_to_kill = [self.server_count - 1, self.server_count - 3]
        self.run_ior_cascade_failure()

    def test_ec_multiple_targets_on_same_rank(self):
        """Jira ID: DAOS-7344.

        Test Description: Test Erasure code object with IOR with multiple targets failure
        Use Case: Create the pool, run IOR with supported EC object type class, kill multiple
                  targets on same rank, while IOR Write phase is in progress, verify all IOR write
                  finish.Read and verify data.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,ec_array,ec_online_rebuild,rebuild,ec_fault,ec_multiple_failure
        :avocado: tags=ec_multiple_target_on_same_rank_failure
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
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,ec_array,ec_online_rebuild,rebuild,ec_fault,ec_multiple_failure
        :avocado: tags=ec_multiple_rank_on_diff_target_failure
        """
        # Kill Two targets from different ranks
        self.pool_exclude[2] = "2"
        self.pool_exclude[3] = "3"
        self.run_ior_cascade_failure()

    @skipForTicket("DAOS-9051")
    def test_ec_single_target_rank_failure(self):
        """Jira ID: DAOS-7344.

        Test Description: Test Erasure code object with IOR with single target and rank failure
        Use Case: Create the pool, run IOR with supported EC object type class, kill single
                  server rank and target, while IOR Write phase is in progress, verify all IOR write
                  finish.Read and verify data.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,ec_online_rebuild,rebuild,ec_fault,ec_multiple_failure
        :avocado: tags=ec_single_target_rank_failure
        """
        # Kill One server rank
        self.rank_to_kill = [self.server_count - 1]
        # Kill single target from single rank
        self.pool_exclude[3] = "3"
        self.run_ior_cascade_failure()
