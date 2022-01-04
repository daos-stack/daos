#!/usr/bin/python
'''
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from ior_test_base import IorTestBase

class EcodCellSize(IorTestBase):
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-few-public-methods
    """EC IOR class to run tests with different cell size.

    Test Class Description: To validate Erasure code object with different cell
                            sizes.

    :avocado: recursive
    """

    def test_ec_cell_size(self):
        """Jira ID: DAOS-7311.

        Test Description:
            Test Erasure code object with IOR with different cell sizes.
        Use Case:
            Create the medium size of pool and run IOR with supported EC object
            type class with container cell size properties from 64K to 1M.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,ec_ior,ior
        :avocado: tags=ec_cell_size
        """
        obj_class = self.params.get("dfs_oclass", '/run/ior/objectclass/*')

        for oclass in obj_class:
            self.ior_cmd.dfs_oclass.update(oclass)
            self.run_ior_with_pool()
