"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import contextlib
import re

from ClusterShell.NodeSet import NodeSet

from apricot import TestWithServers
from general_utils import get_journalctl, journalctl_time, wait_for_result
from run_utils import run_remote


class ControlLogEntry(TestWithServers):
    """Verify administrative log entries.

    :avocado: recursive
    """

    @contextlib.contextmanager
    def verify_journalctl(self, expected_messages):
        """Capture journalctl in a context and verify expected messages.

        Args:
            expected_messages (list): list of regular expressions to look for
        """
        t_before = journalctl_time()
        yield
        self._verify_journalctl(t_before, expected_messages)

    def _verify_journalctl(self, since, expected_messages):
        """Verify journalctl contains one or more messages.

        Args:
            since (str): start time for journalctl
            expected_messages (list): list of regular expressions to look for
        """
        self.log_step('Verify journalctl output since {}'.format(since))

        not_found = set(expected_messages)
        journalctl_per_hosts = []

        def _search():
            """Look for each message in any host's journalctl."""
            journalctl_results = get_journalctl(
                hosts=self.hostlist_servers, since=since, until=journalctl_time(),
                journalctl_type="daos_server")

            # Convert the journalctl to a dict of hosts : output
            journalctl_per_hosts.append({})
            for result in journalctl_results:
                journalctl_per_hosts[-1][str(result['hosts'])] = result['data']

            for message in not_found.copy():
                if any(map(re.compile(message).search, journalctl_per_hosts[-1].values())):
                    not_found.remove(message)

            return len(not_found) == 0

        # Wait up to 5 seconds for journalctl to contain the messages
        wait_for_result(self.log, _search, timeout=5, delay=1)

        # Print the status of each message
        for message in expected_messages:
            if message in not_found:
                self.log.info('  NOT FOUND: %s', message)
            else:
                self.log.info('      FOUND: %s', message)

        # Print the journalctl in an easy-to-read format
        for hosts, output in sorted(journalctl_per_hosts[-1].items()):
            self.log.debug('journalctl for %s:', hosts)
            for line in output.splitlines():
                self.log.debug('  %s', line)

        # Fail if any message was not found
        if not_found:
            fail_msg = '{} messages not found in journalctl'.format(len(not_found))
            self.log.error(fail_msg)
            for message in not_found:
                self.log.error('  %s', message)
            self.fail(fail_msg)

    def test_control_log_entry(self):
        """Jira ID: DAOS-12513

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=control,dmg,rebuild
        :avocado: tags=ControlLogEntry,test_control_log_entry
        """
        dmg = self.get_dmg_command()

        self.log_step('Create pool')
        pool = self.get_pool()

        self.log_step('Stop 1 random rank and wait for rebuild')
        stop_ranks = self.random.sample(list(self.server_managers[0].ranks), k=1)
        expected = [fr'rank {rank}.*is down' for rank in stop_ranks] \
            + [r'Rebuild \[queued\]', r'Rebuild \[completed\]']
        with self.verify_journalctl(expected):
            self.server_managers[0].stop_ranks(stop_ranks, self.d_log, force=True)
            dmg.system_query()
            pool.wait_for_rebuild_to_start()
            pool.wait_for_rebuild_to_end()

        self.log_step('Restart rank after rebuild')
        expected = [r'Starting I/O Engine instance']
        with self.verify_journalctl(expected):
            self.server_managers[0].start_ranks(stop_ranks, self.d_log)

        self.log_step('Reintegrate rank and wait for rebuild')
        expected = [fr'rank {rank}.*start reintegration' for rank in stop_ranks] \
            + [fr'rank {rank}.*is reintegrated' for rank in stop_ranks] \
            + [r'Reintegrate \[queued\]', r'Reintegrate \[completed\]']
        with self.verify_journalctl(expected):
            for rank in stop_ranks:
                pool.reintegrate(str(rank))
            pool.wait_for_rebuild_to_start()
            pool.wait_for_rebuild_to_end()

        self.log_step('Exclude 2 random ranks and wait for rebuild')
        exclude_ranks = self.random.sample(list(self.server_managers[0].ranks), k=2)
        expected = [fr'rank {rank}.*is down' for rank in exclude_ranks] \
            + [fr'rank {rank}.*is excluded' for rank in exclude_ranks] \
            + [r'Rebuild \[queued\]', r'Rebuild \[completed\]']
        with self.verify_journalctl(expected):
            for rank in exclude_ranks:
                pool.exclude(str(rank))
            pool.wait_for_rebuild_to_start()
            pool.wait_for_rebuild_to_end()

        self.log_step('Reintegrate ranks and wait for rebuild')
        expected = [fr'rank {rank}.*start reintegration' for rank in exclude_ranks] \
            + [fr'rank {rank}.*is reintegrated' for rank in exclude_ranks] \
            + [r'Reintegrate \[queued\]', r'Reintegrate \[completed\]']
        with self.verify_journalctl(expected):
            for rank in exclude_ranks:
                pool.reintegrate(str(rank))
            pool.wait_for_rebuild_to_start()
            pool.wait_for_rebuild_to_end()

        self.log_step('Crash a random host and wait for rebuild')
        kill_host = NodeSet(self.random.choice(self.server_managers[0].hosts))
        kill_ranks = self.server_managers[0].get_host_ranks(kill_host)
        expected = [fr'rank {rank}.*is down' for rank in kill_ranks] \
            + [fr'rank {rank}.*is excluded' for rank in kill_ranks] \
            + [r'Rebuild \[queued\]', r'Rebuild \[completed\]']
        with self.verify_journalctl(expected):
            kill_cmd = "sudo -n pkill daos_server --signal KILL && sudo systemctl stop daos_server"
            if not run_remote(self.log, kill_host, kill_cmd):
                self.fail("failed to pkill daos_server")
            pool.wait_for_rebuild_to_start()
            pool.wait_for_rebuild_to_end()

        self.log_step('Restart server')
        expected = [r'Starting I/O Engine instance', r'Listening on']
        with self.verify_journalctl(expected):
            self.server_managers[0].restart(list(kill_host), wait=True)

        self.log_step('Reintegrate all ranks and wait for rebuild')
        expected = [fr'rank {rank}.*start reintegration' for rank in kill_ranks] \
            + [fr'rank {rank}.*is reintegrated' for rank in kill_ranks] \
            + [r'Reintegrate \[queued\]', r'Reintegrate \[completed\]']
        with self.verify_journalctl(expected):
            for rank in kill_ranks:
                pool.reintegrate(str(rank))
            pool.wait_for_rebuild_to_start()
            pool.wait_for_rebuild_to_end()

        self.log_step('Stop/start 2 random ranks')
        stop_ranks = self.random.sample(list(self.server_managers[0].ranks), k=2)
        expected = [fr'rank {rank}.*exited with 0' for rank in stop_ranks] \
            + [fr'process.*started on rank {rank}' for rank in stop_ranks]
        with self.verify_journalctl(expected):
            self.server_managers[0].stop_ranks(stop_ranks, self.d_log)
            self.server_managers[0].start_ranks(stop_ranks, self.d_log)

        self.log_step('Test passed')
