"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from ClusterShell.NodeSet import NodeSet
from run_utils import stop_processes


class SystemStartupTest(TestWithServers):
    """Verify that engines start only when the system is told to start them.

    :avocado: recursive
    """

    def setUp(self):
        """Start servers on every host but the last, which test_dmg_system_extend adds."""
        super().setUp()
        self.extra_host = NodeSet(self.hostlist_servers[-1])
        self.start_servers({
            self.server_group: {
                "hosts": self.hostlist_servers.difference(self.extra_host),
                "mgmt_svc_replicas": self.mgmt_svc_replicas,
                "svr_config_file": None,
                "dmg_config_file": None,
                "svr_config_temp": None,
                "dmg_config_temp": None,
            }
        })
        self.start_agents()

    def pick_rank(self):
        """Pick a rank on a host that is not an MS replica.

        Returns:
            tuple: the host as a NodeSet and its rank
        """
        host = NodeSet(
            self.random.choice(self.server_managers[0].hosts.difference(self.mgmt_svc_replicas)))
        return host, self.server_managers[0].get_host_ranks(host)[0]

    def restart_server(self, host):
        """Kill daos_server on a host and start it again.

        Args:
            host (NodeSet): host to restart
        """
        stop_processes(self.log, host, self.server_managers[0].manager.job.command_regex)
        self.server_managers[0].restart(host)

    def verify_rank_state(self, rank, state, checks):
        """Fail unless the rank reaches the state within the number of checks.

        Args:
            rank (int): rank to check
            state (str): expected state
            checks (int): number of one second checks
        """
        if self.server_managers[0].check_rank_state([rank], [state], checks):
            self.fail(f"Rank {rank} did not reach the {state} state")

    def test_dmg_system_init(self):
        """Initialize is repeatable until a pool exists.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,system_startup
        :avocado: tags=SystemStartupTest,test_dmg_system_init
        """
        dmg = self.get_dmg_command()
        self.log_step("Initialize an initialized system without pools")
        dmg.system_initialize()
        self.log_step("Create a pool")
        self.get_pool(connect=False)
        self.log_step("Initialize is refused once a pool exists")
        with dmg.no_exception():
            result = dmg.system_initialize()
        if result.exit_status == 0 or "already initialized" not in result.stderr_text:
            self.fail("Initialize was not refused on a system with pools")

    def test_dmg_system_extend(self):
        """Extend starts the engines on a newly formatted host.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,system_startup
        :avocado: tags=SystemStartupTest,test_dmg_system_extend
        """
        self.log_step(f"Format {self.extra_host} and extend the system to it")
        self.start_additional_servers(self.extra_host)
        members = self.get_dmg_command().system_query()["response"]["members"]
        joined = [member["rank"] for member in members if member["state"] == "joined"]
        self.assertEqual(
            len(joined), len(self.hostlist_servers), f"Unexpected joined ranks: {joined}")

    def test_restart_rejoins(self):
        """A rank that was not stopped rejoins when its server restarts.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,system_startup
        :avocado: tags=SystemStartupTest,test_restart_rejoins
        """
        host, rank = self.pick_rank()
        self.log_step(f"Kill and restart daos_server on {host}")
        self.restart_server(host)
        self.log_step(f"Rank {rank} rejoins on its own")
        self.verify_rank_state(rank, "joined", 60)
