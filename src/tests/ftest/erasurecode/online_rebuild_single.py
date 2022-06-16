#!/usr/bin/python
'''
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from ec_utils import ErasureCodeSingle

class EcodOnlineRebuildSingle(ErasureCodeSingle):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: To validate Erasure code object single type data
                            after killing single server while Write in progress
                            kill another server while Read is in progress.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a EcOnlineRebuildSingle object."""
        super().__init__(*args, **kwargs)
        self.set_online_rebuild = True

    def test_ec_online_rebuild_single(self):
        """Jira ID: DAOS-5894.

        Test Description: Test Erasure code object with single data type for
                          online rebuild.
        Use Case: Create the pool, Write the single type data using Api
                  kill single server, while Write phase is in progress,
                  verify write finish without any error.
                  kill another server, while Read phase is in progress.
                  verify read finish without any error with verification.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,ec_single,ec_online_rebuild,rebuild
        :avocado: tags=ec_online_rebuild_single
        """
        # Kill last server rank
        self.rank_to_kill = self.server_count - 1

        # Run only object type which matches the server count and
        # remove other objects
        tmp_obj_class = []
        for oclass in self.obj_class:
            if oclass[1] == self.server_count:
                tmp_obj_class = oclass
        self.obj_class = [tmp_obj_class]

        # Write single data set with different EC object.
        # kill single server while Write phase is in progress.
        self.start_online_single_operation("WRITE")

        # Disabled Online rebuild for reading the same data
        self.set_online_rebuild = False
        # Read data and verify for different EC object
        self.start_online_single_operation("READ")

        # Enabled Online rebuild during Read phase
        self.set_online_rebuild = True
        # Kill another server rank
        self.rank_to_kill = self.server_count - 2
        # Read data and verify while another server being killed during read
        # EC data was written with +2 parity so after killing Two servers data
        # should be intact and no data corruption observed.
        self.start_online_single_operation("READ", parity=2)
