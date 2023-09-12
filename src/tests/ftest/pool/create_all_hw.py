"""
(C) Copyright 2022-2023 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from general_utils import bytes_to_human
from pool_create_all_base import PoolCreateAllTestBase


class PoolCreateAllHwTests(PoolCreateAllTestBase):
    """Tests pool creation with percentage storage on HW platform.

    :avocado: recursive
    """

    def get_deltas(self, test_name, step_name=None):
        """Returns the size of SCM and NVMe deltas.

        Args:
            test_name (string): Name of the test configuration field.
            step_name (string): Name of the step in the test.

        Returns:
            list: SCM and NVMe delta size in bytes.
        """
        path = os.path.join(os.sep, "run", test_name, "deltas")
        if step_name is not None:
            path = os.path.join(path, step_name)
        path = os.path.join(path, "*")
        return [self.params.get(key, path, 0) for key in ("scm", "nvme")]

    def log_deltas(self, scm_bytes, nvme_bytes, prefix=None):
        """Logs value of SCM and NVMe deltas.

        Args:
            scm_bytes (int): SCM delta value in bytes.
            nvme_bytes (int): NVMe delta value in bytes.
            prefix (string): Name prefix of the delta variable.
        """
        self.log.info(
            "\t- %s=%s (%d Bytes)",
            "scm_delta" if prefix is None else "{}_scm_delta".format(prefix),
            bytes_to_human(scm_bytes),
            scm_bytes)
        self.log.info(
            "\t- %s=%s (%d Bytes)",
            "nvme_delta" if prefix is None else "{}_nvme_delta".format(prefix),
            bytes_to_human(nvme_bytes),
            nvme_bytes)

    def test_one_pool_hw(self):
        """Test the creation of one pool with all the storage capacity.

        Test Description:
            Create a pool with all the capacity of all servers.
            Check that a pool could not be created with more SCM or NVME capacity.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=pool,pool_create_all
        :avocado: tags=PoolCreateAllHwTests,test_one_pool_hw
        """
        deltas_bytes = self.get_deltas("test_one_pool_hw")
        self.log.info("Test basic pool creation with full storage")
        self.log_deltas(*deltas_bytes)

        self.check_pool_full_storage(*deltas_bytes)

    def test_recycle_pools_hw(self):
        """Test the pool creation and destruction.

        Test Description:
            Create a pool with all the capacity of all servers.  Destroy the pool and repeat these
            steps an arbitrary number of times. For each iteration, check that the size of the
            created pool is always the same.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=pool,pool_create_all
        :avocado: tags=PoolCreateAllHwTests,test_recycle_pools_hw
        """
        pool_count = self.params.get("pool_count", "/run/test_recycle_pools_hw/*", 0)
        deltas_bytes = self.get_deltas("test_recycle_pools_hw")
        deltas_bytes[:] = [it * self.engines_count for it in deltas_bytes]
        self.log.info("Test pool creation and destruction")
        self.log.info("\t- pool_count=%d", pool_count)
        self.log_deltas(*deltas_bytes)

        self.check_pool_recycling(pool_count, *deltas_bytes)

    def test_two_pools_hw(self):
        """Test the creation of two pools with 50% and 100% of the available storage.

        Test Description:
            Create a first pool with 50% of all the capacity of all servers. Verify that the pool
            created effectively used 50% of the available storage and also check that the pool is
            well balanced (i.e. more or less use the same size on all the available servers).
            Create a second pool with all the remaining storage. Verify that the pool created
            effectively used all the available storage and there is no more available storage.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=pool,pool_create_all
        :avocado: tags=PoolCreateAllHwTests,test_two_pools_hw
        """
        pool_half_deltas_bytes = self.get_deltas("test_two_pools_hw", "pool_half")
        pool_full_deltas_bytes = self.get_deltas("test_two_pools_hw", "pool_full")
        distribution_deltas_bytes = self.get_deltas("test_two_pools_hw", "distribution")
        self.log.info(
            "Test pool creation of two pools with 50% and 100% of the available storage")
        for name in ('pool_half', 'pool_full', 'distribution'):
            val = locals()["{}_deltas_bytes".format(name)]
            self.log_deltas(*val, prefix=name)

        self.log.info("Creating first pool with half of the available storage: size=50%")
        self.check_pool_half_storage(*pool_half_deltas_bytes)

        self.log.info("Checking data distribution among the different engines")
        self.check_pool_distribution(*distribution_deltas_bytes)

        self.log.info("Creating second pool with all the available storage: size=100%")
        self.check_pool_full_storage(*pool_full_deltas_bytes)
