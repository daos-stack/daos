#!/usr/bin/python3
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from ior_test_base import IorTestBase


class NvmeIo(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class for NVMe with IO tests.

    Test Class Description:
        Test the general Metadata operations and boundary conditions.

    :avocado: recursive
    """

    def test_nvme_io(self):
        """Jira ID: DAOS-2082.

        Test Description:
            Test will run IOR with standard and non standard sizes.  IOR will
            be run for all Object type supported. Purpose is to verify pool
            size (SCM and NVMe) for IOR file.

        Use Cases:
            Running multiple IOR on same server start instance.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=daosio,nvme_io
        """

        # Test params
        object_type = self.params.get("object_type", '/run/ior/*')
        ior_seq_pool_qty = self.params.get("ior_sequence_pool_qty", '/run/pool/*')

        # Loop for every object type
        for obj_type in object_type:
            # Loop for every pool size
            for index in range(ior_seq_pool_qty):
                # Create and connect to a pool with namespace
                self.add_pool(namespace="/run/pool/pool_{}/*".format(index))
                stripesize = self.params.get("stripesize", "/run/pool/pool_{}/*".format(index))
                blocksize = self.params.get("blocksize", "/run/pool/pool_{}/*".format(index))
                clientslots = self.params.get("clientslots", "/run/pool/pool_{}/*".format(index))

                # Disable aggregation
                self.pool.set_property()

                # Get the current pool sizes
                self.pool.get_info()
                size_before_ior = self.pool.info

                # Run ior with the parameters specified for this pass
                self.ior_cmd.transfer_size.update(stripesize)
                self.ior_cmd.block_size.update(blocksize)
                self.ior_cmd.dfs_oclass.update(obj_type)
                self.ior_cmd.set_daos_params(self.server_group, self.pool)
                self.run_ior(self.get_ior_job_manager_command(), clientslots)

                # Verify IOR consumed the expected amount from the pool
                self.verify_pool_size(size_before_ior, clientslots)

                errors = self.destroy_pools(self.pool)
                if errors:
                    self.fail(
                        "Errors detected during destroy pool:\n  - {}".format(
                            "\n  - ".join(errors)))
