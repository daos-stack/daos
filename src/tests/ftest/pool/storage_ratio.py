#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from test_utils_pool import TestPool
from avocado.core.exceptions import TestFail


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
        tests = self.params.get("storage_ratio", '/run/pool/*')
        results = {}
        for num, test in enumerate(tests):
            pool = TestPool(self.context, self.get_dmg_command())
            pool.get_params(self)
            pool.scm_size.update(test[0])
            pool.nvme_size.update(test[1])
            try:
                # Create a pool
                pool.create()
                if 'FAIL' in test[2]:
                    results[num] = 'FAIL'
                elif 'PASS' in test[2]:
                    results[num] = 'PASS'
                elif ('WARNING' in test[2] and
                      'SCM:NVMe ratio is less than' in pool.dmg.result.stdout):
                    results[num] = 'PASS'
                else:
                    results[num] = 'FAIL'
            except TestFail:
                if 'PASS' in test[2]:
                    results[num] = 'FAIL'
                elif 'FAIL' in test[2]:
                    results[num] = 'PASS'

            pool.destroy()

        for key in results:
            if 'FAIL' in results[key]:
                self.fail('Pool Creation {} Suppose to {}'
                          .format(tests[key], tests[key][2]))
