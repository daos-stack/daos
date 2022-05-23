#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from ec_utils import ErasureCodeIor


class EcodIorHardRebuild(ErasureCodeIor):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: To validate IOR Hard with EC object class for rebuild scenarios

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a EcodIorHardRebuild object."""
        super().__init__(*args, **kwargs)
        self.set_online_rebuild = True

    def test_ec_ior_hard_online_rebuild(self):
        """Jira ID: DAOS-7318.

        Test Description: Test Erasure code object online rebuild with IOR Hard.
        Use Case: Create the pool, run IOR hard with supported EC object type class.
                  kill server, while IOR Write phase is in  progress. Verify IOR write finish.
                  Read and verify data. Read the data again and while read is in progress, kill
                  second server. Read and verify the data after second server killed.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,ec_array,ec_online_rebuild,rebuild,ec_ior,ior_hard
        :avocado: tags=ec_ior_hard_online_rebuild
        """
        # This is IOR Hard so skip the warning messages
        self.fail_on_warning = False

        # Kill last server rank
        self.rank_to_kill = [self.server_count - 1]

        # Run only object type which matches the server count and remove other objects
        tmp_obj_class = []
        for oclass in self.obj_class:
            if oclass[1] == self.server_count:
                tmp_obj_class = oclass
        self.obj_class = [tmp_obj_class]

        # Write IOR data set with different EC object. kill single server while IOR Write phase
        # is in progress.
        self.ior_write_dataset()

        # Do not change the stoneWallingStatusFile during read
        self.ior_cmd.sw_wearout.update(None)

        # remove first ior_read_dataset() calling, as it will zero the stoneWallingStatusFile
        # and then fail the following 2nd ior_read_dataset (details in DAOS-10342).
        # Disabled Online rebuild
        # self.set_online_rebuild = False
        # Read IOR data and verify for different EC object kill single server while IOR Read phase
        # is in progress.
        # self.ior_read_dataset()

        # Enabled Online rebuild during Read phase
        self.set_online_rebuild = True
        # Kill another server rank
        self.rank_to_kill = [self.server_count - 2]
        # Read IOR data and verify for EC object again EC data was written with +2 parity so after
        # killing Two servers data should be intact and no data corruption observed.
        self.ior_read_dataset(parity=2)
