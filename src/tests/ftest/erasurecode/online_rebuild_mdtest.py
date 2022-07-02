#!/usr/bin/python
'''
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from ec_utils import ErasureCodeMdtest

class EcodOnlineRebuildMdtest(ErasureCodeMdtest):
    # pylint: disable=too-many-ancestors
    """EC MDtest on-line rebuild test class.

    Test Class Description: To validate Erasure code object type classes works
                            fine for MDtest benchmark for on-line rebuild.

    :avocado: recursive
    """
    def __init__(self, *args, **kwargs):
        """Initialize a EcOnlineRebuild object."""
        super().__init__(*args, **kwargs)
        self.set_online_rebuild = True

    def test_ec_online_rebuild_mdtest(self):
        """Jira ID: DAOS-7320.

        Test Description:
            Test EC object class with MDtest for on-line rebuild.
        Use Cases:
            Create the pool and run MDtest with EC object class.
            While MDtest is running kill single server.
            Rebuild should finish without any problem.
            MDtest should finish without any failure.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=ec,ec_array,mdtest,ec_online_rebuild
        :avocado: tags=ec_online_rebuild_array,ec_online_rebuild_mdtest
        """
        # Kill last server rank
        self.rank_to_kill = self.server_count - 1

        # Run only object type which matches the server count and
        # remove other objects
        for oclass in self.obj_class:
            if oclass[1] == self.server_count:
                self.obj_class = oclass[0]

        self.start_online_mdtest()
