#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
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
        variants = self.params.get("storage_ratio", '/run/*')
        dmg_command = self.get_dmg_command()
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
            scm_size = variant[0]
            nvme_size = variant[1]
            expected_result = variant[2]
            try:
                # Create a pool
                dmg_command.pool_create(
                    scm_size=scm_size, nvme_size=nvme_size, use_json=False)
                if expected_result == 'FAIL':
                    errors.append(
                        "Pool create succeeded when it's expected to fail! " +
                        "storage_ratio used: {}".format(variant))
                elif ('WARNING' in expected_result and
                      'SCM:NVMe ratio is less than' not in
                      dmg_command.result.stdout):
                    errors.append(
                        "No expected SCM:NVMe ratio warning message! " +
                        "storage_ratio used: {}".format(variant))
            except CommandFailure:
                if expected_result != 'FAIL':
                    errors.append(
                        "Pool create failed when it's expected to succeed! " +
                        "storage_ratio used: {}".format(variant))

            if expected_result != "FAIL":
                output = dmg_command.pool_list()
                pool_uuid = output["response"]["pools"][0]["uuid"]
                dmg_command.pool_destroy(pool=pool_uuid)

        if errors:
            self.fail("--- Test failures! ---\n{}".format("\n".join(errors)))
