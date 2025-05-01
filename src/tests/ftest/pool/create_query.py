"""
(C) Copyright 2021-2024 Intel Corporation.
(C) Copyright 2025 Hewlett Packard Enterprise Development LP

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from test_utils_pool import add_pool


class PoolCreateQueryTests(TestWithServers):
    """Pool create tests.

    All of the tests verify pool create response with 4 servers.
    Each server should be configured with full compliment of NVDIMMs and SSDs.

    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        # Create test-case-specific DAOS log files
        self.update_log_file_names()
        super().setUp()

    def test_create_and_query(self):
        """JIRA ID: DAOS-10339

        Test Description:
            Create a pool and check that the size of the response is close enough to the requested
            size.  Verify that the size of pool is equal to the size returned in the response of the
            pool create.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=pool
        :avocado: tags=PoolCreateQueryTests,test_create_and_query
        """
        # Create pool
        pool = add_pool(self)
        epsilon_bytes = 1 << 20  # 1 MiB
        epsilon_scm_bytes = (
            ((1 << 24) * 8)  # 16 MiB * 8 tgts
            if self.server_managers[0].manager.job.using_control_metadata
            else (1 << 20)  # 1 MiB
        )
        self.assertLessEqual(
            abs(pool.scm_per_rank - pool.scm_size.value), epsilon_scm_bytes,
            "SCM size of the pool created too different from the given size")
        self.assertGreaterEqual(
            pool.scm_per_rank, pool.scm_size.value,
            "SCM size of the pool created should be at least "
            "greater than or equal to the given size")
        self.assertLessEqual(
            abs(pool.nvme_per_rank - pool.nvme_size.value), epsilon_bytes,
            "NVMe size of the pool created too different from the given size")

        # Query pool created
        resp = self.get_dmg_command().pool_query(pool.identifier, show_enabled=True)["response"]
        nb_ranks = len(resp.get("enabled_ranks"))
        tier_stats = resp["tier_stats"]
        self.assertEqual("SCM", tier_stats[0]["media_type"].upper(), "Unexpected tier media type")
        self.assertEqual("NVME", tier_stats[1]["media_type"].upper(), "Unexpected tier media type")
        self.assertEqual(
            pool.scm_per_rank * nb_ranks, tier_stats[0]["total"],
            "SCM size of the pool created not coherent")
        self.assertEqual(
            pool.nvme_per_rank * nb_ranks, tier_stats[1]["total"],
            "NVMe size of the pool created not coherent")
