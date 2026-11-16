"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from apricot import TestWithServers
from ClusterShell.NodeSet import NodeSet
from run_utils import run_remote, stop_processes

WAIT_TIMEOUT = 60


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

    def simulate_storage_loss(self, host, rank):
        """Kill daos_server on the host and wipe its SCM mount, as if its drive was lost.

        Args:
            host (NodeSet): host to simulate storage loss on
            rank (int): rank running on the host, stopped before killing its processes so the
                system membership reflects its departure immediately instead of relying on
                swim to detect it
        """
        self.server_managers[0].stop_ranks([rank])
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

        if self.server_managers[0].manager.job.using_control_metadata:
            # In MD-on-SSD mode the superblock lives under control_metadata, not the ramdisk, so
            # it must also be wiped or the engine will find its old identity and skip format.
            metadata_path = self.server_managers[0].manager.job.control_metadata.path.value
            self.log.info(
                "Wiping control metadata path %s on %s to simulate storage media loss",
                metadata_path, host)
            self.server_managers[0].clean_mount(host, metadata_path)

        self.server_managers[0].restart(host, start_ranks=False)
        self.wait_for_rank_states(rank)

    def wait_for_rank_states(self, rank, timeout=WAIT_TIMEOUT):
        """Query the system until the replaced rank is stopped and all other ranks are joined.

        Args:
            rank (int): rank whose storage media loss was simulated
            timeout (int, optional): max seconds to wait. Defaults to WAIT_TIMEOUT.
        """
        other_ranks = [r for r in self.server_managers[0].ranks if r != rank]
        start = time.time()
        while True:
            data = self.server_managers[0].get_current_state()
            self.log.info("System query while waiting for rank %d to leave: %s", rank, data)
            rank_stopped = rank in data and data[rank]["state"] == "stopped"
            others_joined = all(r in data and data[r]["state"] == "joined" for r in other_ranks)
            if rank_stopped and others_joined:
                return
            if time.time() - start > timeout:
                self.fail(
                    f"Timed out waiting for rank {rank} to be stopped and ranks {other_ranks} "
                    f"to stay joined before storage format --replace: {data}")
            time.sleep(1)

    def test_storage_format_replace(self):
        """A rank whose storage was lost rejoins with the same rank after --replace.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm,hw,medium
        :avocado: tags=control,system_startup,storage_format_replace
        :avocado: tags=StorageFormatReplaceTest,test_storage_format_replace
        """
        host, rank = self.pick_rank()

        self.log_step(f"Simulate storage media loss for rank {rank} on {host}")
        self.simulate_storage_loss(host, rank)

        self.log_step(f"Replace rank {rank}'s storage with 'dmg storage format --replace'")
        dmg = self.get_dmg_command()
        dmg.storage_format(replace=True, rank=rank, hosts=host)

        self.log_step(f"Rank {rank} rejoins with its original rank reclaimed")
        if self.server_managers[0].check_rank_state([rank], ["joined"], 60):
            self.fail(f"Rank {rank} did not rejoin after storage format --replace")
