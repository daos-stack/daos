#!/usr/bin/python
"""
(C) Copyright 2021-2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from pool_test_base import PoolTestBase


class PoolCreateQueryTests(PoolTestBase):
    # pylint: disable=too-many-ancestors
    """Pool create tests.

    All of the tests verify pool create response with 4 servers.
    Each server should be configured with full compliment of NVDIMMs and SSDs.

    :avocado: recursive
    """

    def test_create_and_query(self):
        """JIRA ID: DAOS-10339

        Test Description:
            Create a pool and check that the size of the response is close enough to the requested
            size.  Verify that the size of pool is equal to the size returned in the response of the
            pool create.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=pool
        :avocado: tags=pool_create_and_query
        """
        # Create pool
        self.add_pool()
        epsilon_bytes = 1 << 20 # 1MiB
        self.assertLessEqual(abs(self.pool.scm_per_rank - self.pool.scm_size.value), epsilon_bytes,
                "SCM size of the pool created too different from the given size")
        self.assertGreaterEqual(self.pool.scm_per_rank, self.pool.scm_size.value,
                "SCM size of the pool created should be at least "
                "greater than or equal to the given size")
        self.assertLessEqual(abs(self.pool.nvme_per_rank - self.pool.nvme_size.value),
                epsilon_bytes,
                "NVMe size of the pool created too different from the given size")

        # Query pool created
        self.pool.set_query_data()
        nb_ranks = self.pool.query_data["response"]["total_nodes"]
        tier_stats = self.pool.query_data["response"]["tier_stats"]
        self.assertEqual("SCM", tier_stats[0]["media_type"].upper(), "Unexpected tier media type")
        self.assertEqual("NVME", tier_stats[1]["media_type"].upper(), "Unexpected tier media type")
        self.assertEqual(self.pool.scm_per_rank * nb_ranks, tier_stats[0]["total"],
                "SCM size of the pool created not coherent")
        self.assertEqual(self.pool.nvme_per_rank * nb_ranks, tier_stats[1]["total"],
                "NVMe size of the pool created not coherent")
