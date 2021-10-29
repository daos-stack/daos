#!/usr/bin/python3
"""
  (C) Copyright 2020-2021 Intel Corporation.

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
        tests = self.params.get("ior_sequence", '/run/ior/*')
        object_type = self.params.get("object_type", '/run/ior/*')

        # Loop for every IOR object type
        for obj_type in object_type:
            for ior_param in tests:
                # There is an issue with 8 bytes transfer size Hence, skip
                # tests case with 8 bytes Transfer size.
                if ior_param[2] == 8:
                    self.log.warning("Skip test because of DAOS-7021")
                    continue

                # Create and connect to a pool
                self.add_pool(create=False)
                self.pool.scm_size.update(ior_param[0])
                self.pool.nvme_size.update(ior_param[1])
                self.pool.create()

                # Disable aggregation
                self.pool.set_property()

                # Get the current pool sizes
                self.pool.get_info()
                size_before_ior = self.pool.info

                # Run ior with the parameters specified for this pass
                self.ior_cmd.transfer_size.update(ior_param[2])
                self.ior_cmd.block_size.update(ior_param[3])
                self.ior_cmd.dfs_oclass.update(obj_type)
                self.ior_cmd.set_daos_params(self.server_group, self.pool)
                self.run_ior(self.get_ior_job_manager_command(), ior_param[4])

                # Verify IOR consumed the expected amount from the pool
                self.verify_pool_size(size_before_ior, ior_param[4])

                errors = self.destroy_pools(self.pool)
                if errors:
                    self.fail(
                        "Errors detected during destroy pool:\n  - {}".format(
                            "\n  - ".join(errors)))
