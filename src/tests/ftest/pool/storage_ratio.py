#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from test_utils_pool import TestPool
from command_utils import CommandFailure


class StorageRatio(TestWithServers):
    """Storage Ratio test cases.

    Test class Description:
        Verify the Storage ratio is getting checked during creation.

    :avocado: recursive
    """

    def test_storage_ratio(self):
        """Jira ID: DAOS-2332.

        Test Description:
            Purpose of this test is to verify SCM/NVME
            storage space ratio for pool creation.

        Use case:
        Create Pool with different SCM/NVMe pool storage size ratio and
        verify that pool creation failed if SCM storage size is too low
        compare to NVMe size.
        For now 1% minimum SCM size needed against NVMe. There is no Maximum
        limit.Added tests to verify the Warning message if ration is not 1%.

        :avocado: tags=all,hw,medium,nvme,ib2,full_regression
        :avocado: tags=storage_ratio
        """
        variants = self.params.get("storage_ratio", '/run/pool/*')
        errors = []

        # Sample output with warning.
        # Command '/home/mkano/daos/install/bin/dmg -o
        # /etc/daos/daos_control.yml pool create --group=mkano --nvme-size=200G
        # --scm-size=1G --sys=daos_server --user=mkano' finished with 0 after
        # 1.26520204544s
        # SCM:NVMe ratio is less than 1.00 %, DAOS performance will suffer!
        # Creating DAOS pool with manual per-server storage allocation: 1.0 GB
        # SCM, 200 GB NVMe (0.50% ratio)
        # Pool created with 0.50% SCM/NVMe ratio
        # ---------------------------------------
        # UUID          : 89c5f47e-504a-4fc3-90d2-abe7ccb35356
        # Service Ranks : 0
        # Storage Ranks : [0-1]
        # Total Size    : 402 GB
        # SCM           : 2.0 GB (1.0 GB / rank)
        # NVMe          : 400 GB (200 GB / rank)

        for variant in variants:
            pool = TestPool(self.context, self.get_dmg_command())
            pool.get_params(self)
            scm_size = variant[0]
            nvme_size = variant[1]
            pool.scm_size.update(scm_size)
            pool.nvme_size.update(nvme_size)
            expected_result = variant[2]
            kwargs = {
                "uid": pool.uid,
                "gid": pool.gid,
                "scm_size": pool.scm_size.value,
                "nvme_size": pool.nvme_size.value,
                "group": pool.name.value}
            try:
                # Create a pool
                pool.dmg.pool_create_stdout(**kwargs)
                if expected_result == 'FAIL':
                    errors.append(
                        "Pool create succeeded when it's expected to fail! " +
                        "storage_ratio used: {}".format(variant))
                elif ('WARNING' in expected_result and
                      'SCM:NVMe ratio is less than' not in
                      pool.dmg.result.stdout):
                    errors.append(
                        "No expected SCM:NVMe ratio warning message! " +
                        "storage_ratio used: {}".format(variant))
            except CommandFailure:
                if expected_result != 'FAIL':
                    errors.append(
                        "Pool create failed when it's expected to succeed! " +
                        "storage_ratio used: {}".format(variant))

            pool.destroy()

        if errors:
            self.fail("Test failures!\n{}".format("\n".join(errors)))
