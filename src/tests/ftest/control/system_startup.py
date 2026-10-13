"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from apricot import TestWithServers
from ClusterShell.NodeSet import NodeSet
from run_utils import run_remote, stop_processes


class SystemStartupTest(TestWithServers):
    """Verify that engines start only when the system is told to start them.

    :avocado: recursive
    """

    def setUp(self):
        """Start servers on every host but the last, which test_extend adds."""
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
        host = NodeSet(self.server_managers[0].hosts.difference(self.mgmt_svc_replicas)[-1])
        return host, self.server_managers[0].get_host_ranks(host)[0]

    def restart_server(self, host):
        """Kill daos_server on a host and start it again without starting its ranks.

        Args:
            host (NodeSet): host to restart
        """
        stop_processes(self.log, host, self.server_managers[0].manager.job.command_regex)
        self.server_managers[0].restart(host, start_ranks=False)

    def engine_running(self, host):
        """Report whether a daos_engine process is running on the host.

        Args:
            host (NodeSet): host to check

        Returns:
            bool: True if an engine process was found
        """
        return run_remote(self.log, host, "pgrep daos_engine", verbose=False).passed

    def verify_no_engine(self, host, seconds):
        """Fail if an engine process starts on the host within the given time.

        Args:
            host (NodeSet): host to check
            seconds (int): seconds to keep checking
        """
        for _ in range(seconds):
            if self.engine_running(host):
                self.fail(f"An engine started on {host}")
            time.sleep(1)

    def restore_restart_on_boot(self):
        """Re-enable restart_engines_on_boot.

        Returns:
            list: no errors, as register_cleanup expects
        """
        self.get_dmg_command().system_set_prop("restart_engines_on_boot:true")
        return []

    def verify_rank_state(self, rank, state, checks):
        """Fail unless the rank reaches the state within the number of checks.

        Args:
            rank (int): rank to check
            state (str): expected state
            checks (int): number of one second checks
        """
        if self.server_managers[0].check_rank_state([rank], [state], checks):
            self.fail(f"Rank {rank} did not reach the {state} state")

    def test_initialize(self):
        """Initialize is repeatable until a pool exists.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,system_startup
        :avocado: tags=SystemStartupTest,test_initialize
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

    def test_extend(self):
        """Extend starts the engines on a newly formatted host.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,system_startup
        :avocado: tags=SystemStartupTest,test_extend
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

    def test_stopped_rank_stays_stopped(self):
        """A rank stopped with dmg stays stopped when its server restarts.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,system_startup
        :avocado: tags=SystemStartupTest,test_stopped_rank_stays_stopped
        """
        dmg = self.get_dmg_command()
        host, rank = self.pick_rank()
        self.log_step(f"Stop rank {rank} and restart daos_server on {host}")
        dmg.system_stop(ranks=str(rank))
        self.verify_rank_state(rank, "stopped", 10)
        self.restart_server(host)
        self.log_step(f"Rank {rank} stays stopped")
        self.verify_no_engine(host, 20)
        self.verify_rank_state(rank, "stopped", 1)
        self.log_step(f"Start rank {rank}")
        dmg.system_start(ranks=str(rank))
        self.verify_rank_state(rank, "joined", 60)

    def test_restart_engines_on_boot(self):
        """No rank rejoins on restart while restart_engines_on_boot is false.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,system_startup
        :avocado: tags=SystemStartupTest,test_restart_engines_on_boot
        """
        dmg = self.get_dmg_command()
        host, rank = self.pick_rank()
        self.log_step("Disable engine restart on boot")
        self.register_cleanup(self.restore_restart_on_boot)
        dmg.system_set_prop("restart_engines_on_boot:false")
        self.log_step(f"Kill and restart daos_server on {host}")
        self.restart_server(host)
        self.log_step(f"Rank {rank} stays down")
        self.verify_no_engine(host, 20)
        self.log_step(f"Start rank {rank}")
        dmg.system_start(ranks=str(rank))
        self.verify_rank_state(rank, "joined", 60)
