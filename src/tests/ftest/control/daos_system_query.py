"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers


class DaosSystemQuery(TestWithServers):
    """Test Class Description:
    Test the 'daos system query' command. This command requests client-relevant information
    from the DAOS system.

    :avocado: recursive
    """

    def test_daos_system_query(self):
        """JIRA ID: DAOS-7983
        Test Description: Test daos system query.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=control,daos_cmd
        :avocado: tags=DaosSystemQuery,daos_system_query,test_daos_system_query
        """
        daos_cmd = self.get_daos_command()

        num_servers = len(self.hostlist_servers)
        engines_per_host = self.params.get("engines_per_host", "/run/server_config/*")
        exp_num_ranks = num_servers * engines_per_host

        exp_sys_name = self.server_managers[0].get_config_value("name")
        exp_provider = self.server_managers[0].get_config_value("provider")

        query_output = daos_cmd.system_query()["response"]

        sys_name = query_output["system_name"]
        if sys_name != exp_sys_name:
            self.fail("expected system_name='{}', got '{}'".format(exp_sys_name, sys_name))

        provider = query_output["fabric_provider"]
        if not provider.startswith(exp_provider):
            self.fail("expected fabric_provider='{}', got '{}'".format(exp_provider, provider))

        num_ranks = len(query_output["rank_uris"])
        if num_ranks != exp_num_ranks:
            self.fail("expected {} rank URIs, got '{}'".format(exp_num_ranks, num_ranks))
