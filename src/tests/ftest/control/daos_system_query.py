"""
  (C) Copyright 2023-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers


class DaosSystemQuery(TestWithServers):
    """Test Class Description:
    Test the 'daos system query' command. This command requests client-relevant information
    from the DAOS system.

    :avocado: recursive
    """

    def verify_rank_order(self, rank_list_out, error_msg):
        """Verify rank information are ordered by rank number.

        Example rank_list_out:
        "rank_uris": [
          {
            "rank": 0,
            "uri": "ofi+tcp://10.214.211.28:31416"
          },
          {
            "rank": 1,
            "uri": "ofi+tcp://10.214.211.28:31516"
          }
        ]

        Args:
            rank_list_out (list): Either rank_uris or access_point_rank_uris.
            error_msg (str): Error message.
        """
        rank_numbers = []
        for rank_info in rank_list_out:
            rank_numbers.append(rank_info["rank"])
        rank_numbers_sorted = rank_numbers.copy()
        rank_numbers_sorted.sort()
        self.assertEqual(rank_numbers, rank_numbers_sorted, error_msg)

    def test_daos_system_query(self):
        """JIRA ID: DAOS-7983
        Test Description: Test daos system query.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=control,daos_cmd
        :avocado: tags=DaosSystemQuery,test_daos_system_query
        """
        daos_cmd = self.get_daos_command()

        engines_per_host = self.params.get("engines_per_host", "/run/server_config/*")
        exp_num_ranks = self.server_managers[0].engines

        exp_sys_name = self.server_managers[0].get_config_value("name")
        exp_provider = self.server_managers[0].get_config_value("provider")

        num_ms_replicas = len(self.host_info.mgmt_svc_replicas)
        exp_num_ms_ranks = num_ms_replicas * engines_per_host

        query_output = daos_cmd.system_query()["response"]

        sys_name = query_output["system_name"]
        msg = f"expected system_name='{exp_sys_name}', got '{sys_name}'"
        self.assertEqual(sys_name, exp_sys_name, msg)

        provider = query_output["fabric_provider"]
        if not provider.startswith(exp_provider):
            self.fail("expected fabric_provider='{}', got '{}'".format(exp_provider, provider))

        num_ranks = len(query_output["rank_uris"])
        msg = f"expected {exp_num_ranks} rank URIs, got '{num_ranks}'"
        self.assertEqual(num_ranks, exp_num_ranks, msg)

        num_ap_ranks = len(query_output["access_point_rank_uris"])
        msg = (f"expected {exp_num_ms_ranks} access point rank URIs, got "
               f"'{exp_num_ms_ranks}'")
        self.assertEqual(num_ap_ranks, exp_num_ms_ranks)

        # Verify that elements in rank_uris and access_point_rank_uris are sorted by rank.
        rank_uris = query_output["rank_uris"]
        msg = "rank_uris entries aren't sorted!"
        self.verify_rank_order(rank_list_out=rank_uris, error_msg=msg)

        ap_rank_uris = query_output["access_point_rank_uris"]
        msg = "access_point_rank_uris entries aren't sorted!"
        self.verify_rank_order(rank_list_out=ap_rank_uris, error_msg=msg)
