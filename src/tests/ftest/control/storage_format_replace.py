"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from ClusterShell.NodeSet import NodeSet
from run_utils import run_remote, stop_processes


class StorageFormatReplaceTest(TestWithServers):
    """Verify dmg storage format --replace recovers a rank after storage media loss.

    :avocado: recursive
    """

    def setUp(self):
        """Start servers on every host and format without a pool."""
        super().setUp()
        self.start_servers()

    def pick_rank(self):
        """Pick a rank on a host that is not an MS replica.

        Returns:
            tuple: the host as a NodeSet and its rank
        """
        host = NodeSet(
            self.random.choice(self.server_managers[0].hosts.difference(self.mgmt_svc_replicas)))
        return host, self.server_managers[0].get_host_ranks(host)[0]

    def simulate_storage_loss(self, host):
        """Kill daos_server on the host and wipe its SCM mount, as if its drive was lost.

        Args:
            host (NodeSet): host to simulate storage loss on
        """
        stop_processes(self.log, host, self.server_managers[0].manager.job.command_regex)
        scm_mount = self.server_managers[0].manager.job.yaml.engine_params[0].get_value(
            "scm_mount")
        self.log.info("Wiping %s on %s to simulate storage media loss", scm_mount, host)
        result = run_remote(self.log, host, f"sudo umount {scm_mount}", verbose=False)
        if not result.passed:
            self.log.info("umount %s failed (already unmounted?): %s", scm_mount, result)
        result = run_remote(self.log, host, f"sudo rm -rf {scm_mount}")
        if not result.passed:
            self.fail(f"Unable to clear {scm_mount} on {host}")
        self.server_managers[0].restart(host, start_ranks=False)

    def test_storage_format_replace(self):
        """A rank whose storage was lost rejoins with the same rank after --replace.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,system_startup,storage_format_replace
        :avocado: tags=StorageFormatReplaceTest,test_storage_format_replace
        """
        host, rank = self.pick_rank()

        self.log_step(f"Simulate storage media loss for rank {rank} on {host}")
        self.simulate_storage_loss(host)

        self.log_step(f"Replace rank {rank}'s storage with 'dmg storage format --replace'")
        dmg = self.get_dmg_command()
        dmg.hostlist = host
        dmg.storage_format(replace=True, rank=rank)

        self.log_step(f"Rank {rank} rejoins with its original rank reclaimed")
        if self.server_managers[0].check_rank_state([rank], ["joined"], 60):
            self.fail(f"Rank {rank} did not rejoin after storage format --replace")
