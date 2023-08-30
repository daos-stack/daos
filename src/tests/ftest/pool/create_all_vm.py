"""
(C) Copyright 2022-2023 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import random

from general_utils import bytes_to_human
from pool_create_all_base import PoolCreateAllTestBase

# SCM Page size
TMPFS_PAGE_SIZE = 4 << 10           # 4 KiB
TMPFS_HUGEPAGE_SIZE = 2 << 20       # 2 MiB

# DAOS-12750 NOTE Extra space storage for managing DAOS metadata sparse files (e.g. daos_system.db)
# which could eventually be compacted during the recycle test.
MD_SPARSE_FILES_SIZE = 9437184      # 9MiB


class PoolCreateAllVmTests(PoolCreateAllTestBase):
    """Tests pool creation with percentage storage on functional platform.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a PoolCreateAllVmTests object."""
        super().__init__(*args, **kwargs)

        self.scm_hugepages_enabled = True

    def setUp(self):
        """Set up each test case."""
        super().setUp()

        self.scm_hugepages_enabled = not self.params.get(
            "scm_hugepages_disabled",
            "/run/server_config/engines/0/storage/0/*",
            False)

    def test_one_pool(self):
        """Test the creation of one pool with all the storage capacity.

        Test Description:
            Create a pool with all the capacity of all servers.
            Check that a pool could not be created with more SCM or NVME capacity.

        :avocado: tags=all,pr
        :avocado: tags=vm
        :avocado: tags=pool,pool_create_all
        :avocado: tags=PoolCreateAllVmTests,test_one_pool
        """
        delta_bytes = self.params.get("delta", "/run/test_one_pool/*", 0)
        self.log.info("Test basic pool creation with full storage")
        self.log.info("\t- delta=%s (%d Bytes)", bytes_to_human(delta_bytes), delta_bytes)
        self.log.info("\t- scm_hugepages_enabled=%s", self.scm_hugepages_enabled)

        self.check_pool_full_storage(delta_bytes)

    def test_rank_filter(self):
        """Test the creation of one pool with filtering the rank to use

        Test Description:
            Create a pool with all the capacity of some servers.
            Check that only the selected servers have been used for creating the pool.
            Check that a pool could not be created with more SCM or NVME capacity.

        :avocado: tags=all,pr
        :avocado: tags=vm
        :avocado: tags=pool,pool_create_all
        :avocado: tags=PoolCreateAllVmTests,test_rank_filter
        """
        ranks = list(range(self.engines_count))
        random.shuffle(ranks)
        ranks = ranks[(len(ranks) // 2):]
        delta_bytes = self.params.get("delta", "/run/test_rank_filter/*", 0)
        self.log.info("Test ranks filtered pool creation with full storage")
        self.log.info("\t- ranks=%s", ranks)
        self.log.info("\t- delta=%s (%d Bytes)", bytes_to_human(delta_bytes), delta_bytes)
        self.log.info("\t- scm_hugepages_enabled=%s", self.scm_hugepages_enabled)

        self.check_pool_full_storage(delta_bytes, ranks=ranks)

    def get_recycle_pools_delta_bytes(self, pool_count):
        """Return the allowed size of SCM storage space lost for a given number of pools.

        As indicated in JIRA tickets DAOS-11987 and DAOS-12428, some SCM storage are lost when
        a pool is successively created and destroyed.  This was observed for SCM on RAM and it will
        be investigated if the same issue arise with SCM on DCPM.  The space lost with SCM on RAM is
        not the same when the huge pages are enabled or not.  When huge pages are disabled,
        approximately 8192 Bytes (i.e. 2 pages) are lost for each cycle.  With huge pages enabled,
        the size of the pages is far bigger than the size of the space leaked at each iteration.
        Thus, it needs several cycles to effectively lost some storage space.  As illustrated on the
        Figures of the JIRA tickets, the storage lost occurs by step of 4MiB (i.e. 2 pages).

        Note:
            This issue could be related to the management of sparse files compaction.  More details
            could be found in the ticket DAOS-13517.

        Args:
            pool_count (int): Number of pool to create and destroy.

        Returns:
            int: SCM storage space lost.
        """
        if self.scm_hugepages_enabled:
            page_count = 2 if pool_count <= 10 else 4
            delta_bytes = page_count * TMPFS_HUGEPAGE_SIZE
        else:
            page_count = 2 * pool_count
            delta_bytes = page_count * TMPFS_PAGE_SIZE

        delta_bytes += MD_SPARSE_FILES_SIZE

        return delta_bytes * self.engines_count

    def test_recycle_pools(self):
        """Test the pool creation and destruction.

        Test Description:
            Create a pool with all the capacity of all servers.  Destroy the pool and repeat these
            steps an arbitrary number of times. For each iteration, check that the size of the
            created pool is always the same.

        :avocado: tags=all,pr
        :avocado: tags=vm
        :avocado: tags=pool,pool_create_all
        :avocado: tags=PoolCreateAllVmTests,test_recycle_pools
        """
        pool_count = self.params.get("pool_count", "/run/test_recycle_pools/*", 0)
        delta_bytes = self.get_recycle_pools_delta_bytes(pool_count)
        self.log.info("Test pool creation and destruction")
        self.log.info("\t- pool_count=%d", pool_count)
        self.log.info("\t- delta=%s (%d Bytes)", bytes_to_human(delta_bytes), delta_bytes)
        self.log.info("\t- scm_hugepages_enabled=%s", self.scm_hugepages_enabled)

        self.check_pool_recycling(pool_count, delta_bytes)

    def test_two_pools(self):
        """Test the creation of two pools with 50% and 100% of the available storage.

        Test Description:
            Create a first pool with 50% of all the capacity of all servers. Verify that the pool
            created effectively used 50% of the available storage and also check that the pool is
            well balanced (i.e. more or less use the same size on all the available servers).
            Create a second pool with all the remaining storage. Verify that the pool created
            effectively used all the available storage and there is no more available storage.

        :avocado: tags=all,pr
        :avocado: tags=vm
        :avocado: tags=pool,pool_create_all
        :avocado: tags=PoolCreateAllVmTests,test_two_pools
        """
        pool_half_delta_bytes = self.params.get("pool_half", "/run/test_two_pools/deltas/*", 0)
        pool_full_delta_bytes = self.params.get("pool_full", "/run/test_two_pools/deltas/*", 0)
        distribution_delta_bytes = self.params.get(
            "distribution",
            "/run/test_two_pools/deltas/*",
            0)
        self.log.info(
            "Test pool creation of two pools with 50% and 100% of the available storage")
        for name in ('pool_half', 'pool_full', 'distribution'):
            val = locals()["{}_delta_bytes".format(name)]
            self.log.info("\t- %s=%s (%d Bytes)", name, bytes_to_human(val), val)
        self.log.info("\t- scm_hugepages_enabled=%s", self.scm_hugepages_enabled)

        self.log.info("Creating first pool with half of the available storage: size=50%")
        self.check_pool_half_storage(pool_half_delta_bytes)

        self.log.info("Checking data distribution among the different engines")
        self.check_pool_distribution(distribution_delta_bytes)

        self.log.info("Creating second pool with all the available storage: size=100%")
        self.check_pool_full_storage(pool_full_delta_bytes)
