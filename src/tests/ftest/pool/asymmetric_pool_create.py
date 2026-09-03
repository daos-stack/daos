"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from collections import defaultdict

from apricot import TestWithServers


class AsymmetricPoolCreate(TestWithServers):
    """Test asymmetric pool create with an excluded system rank.

    :avocado: recursive
    """

    def _get_joined_ranks_by_fault_domain(self):
        """Get joined ranks grouped by fault domain from dmg system query.

        Returns:
            dict: fault domain to sorted joined rank list
        """
        members = self.get_dmg_command().system_query()["response"]["members"]
        ranks_by_domain = defaultdict(list)
        for member in members:
            if member["state"] == "joined":
                ranks_by_domain[member["fault_domain"]].append(member["rank"])

        for ranks in ranks_by_domain.values():
            ranks.sort()
        return ranks_by_domain

    def test_create_with_excluded_rank(self):
        """Test pool create includes an excluded system rank as disabled.

        Test Description:
            Verify each server has at least two joined ranks, stop one rank,
            create a pool with the default rank selection, and confirm dmg pool
            query reports the stopped/excluded rank in the pool's disabled rank
            list.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=pool,control
        :avocado: tags=AsymmetricPoolCreate,test_create_with_excluded_rank
        """
        ranks_by_domain = self._get_joined_ranks_by_fault_domain()
        self.assertGreater(
            len(ranks_by_domain), 0, "No joined ranks found in dmg system query output")
        for fault_domain, ranks in ranks_by_domain.items():
            self.assertGreaterEqual(
                len(ranks), 2,
                f"Fault domain {fault_domain} has fewer than two joined ranks: {ranks}")

        ms_ranks = set(self.server_managers[0].get_host_ranks(self.mgmt_svc_replicas))
        all_ranks = [rank for ranks in ranks_by_domain.values() for rank in ranks]
        candidate_ranks = list(set(all_ranks) - ms_ranks)
        if not candidate_ranks:
            candidate_ranks = all_ranks
        rank_to_stop = self.random.choice(candidate_ranks)

        self.log.info("Stopping rank %s before pool create", rank_to_stop)
        self.server_managers[0].stop_ranks([rank_to_stop], force=True)
        failed = self.server_managers[0].check_rank_state(
            ranks=[rank_to_stop], valid_states=["excluded"], max_checks=30)
        self.assertListEqual(
            failed, [], f"Rank {rank_to_stop} did not reach excluded state before pool create")

        pool = self.get_pool(connect=False)
        self.assertIsNotNone(
            pool.uuid, f"Pool create returned no UUID despite stopped rank {rank_to_stop}")

        query = pool.query(show_enabled=True)["response"]

        # dmg pool query response exposes the pool map version as "version"
        # (pi_map_ver in the engine). pool_layout_ver is the pool global
        # upgrade version and is unrelated.
        pool_map_ver = query.get("version")
        self.assertEqual(
            pool_map_ver, 1,
            f"Initial pool map version should be 1, got {pool_map_ver}: {query}")

        enabled = set(query.get("enabled_ranks", []) or [])
        disabled = set(query.get("disabled_ranks", []) or [])
        self.assertIn(
            rank_to_stop, disabled,
            f"Stopped rank {rank_to_stop} not found in pool query disabled_ranks: {query}")
        self.assertNotIn(
            rank_to_stop, enabled,
            f"Stopped rank {rank_to_stop} unexpectedly appears in enabled_ranks: {query}")
        self.assertEqual(
            enabled | disabled, set(all_ranks),
            f"Initial pool map does not include every system rank: {query}")
