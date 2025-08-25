'''
  (C) Copyright 2020-2023 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from ior_test_base import IorTestBase


class EcodCellSize(IorTestBase):
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
        :avocado: tags=hw,large
        :avocado: tags=ec,ec_ior,ior
        :avocado: tags=EcodCellSize,ec_cell_size,test_ec_cell_size
        """
        pool_cell_sizes = self.params.get("cell_sizes", '/run/pool/*')
        dfs_oclass_list = self.params.get("dfs_oclass", '/run/ior/*')
        transfersize_blocksize = self.params.get("transfersize_blocksize", '/run/ior/*')

        for cell_size in pool_cell_sizes:
            self.pool = self.get_pool(properties=f"ec_cell_sz:{cell_size}")
            for dfs_oclass in dfs_oclass_list:
                self.ior_cmd.dfs_oclass.update(dfs_oclass)
                for transfer_size, block_size in transfersize_blocksize:
                    self.ior_cmd.transfer_size.update(transfer_size)
                    self.ior_cmd.block_size.update(block_size)
                    self.log_step(
                        f"Running IOR with cell size: {cell_size}, "
                        f"object class: {dfs_oclass}, "
                        f"transfer size: {transfer_size}, "
                        f"block size: {block_size}")
                    self.run_ior_with_pool()
            self.pool.destroy()
