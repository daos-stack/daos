"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from datetime import datetime
import time
import contextlib

from ClusterShell.NodeSet import NodeSet

from apricot import TestWithServers
from general_utils import get_journalctl
from run_utils import run_remote


def journalctl_time():
    """Get now() formatted for journalctl."""
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


class Daos12513(TestWithServers):
    """DAOS-12513 manual test

    :avocado: recursive
    """

    def server_journalctl(self, since):
        """Get daos_server journalctl"""
        results = get_journalctl(
            hosts=self.hostlist_servers, since=since, until=journalctl_time(),
            journalctl_type="daos_server")
        self.log.info("journalctl results")
        for result in results:
            self.log.info("hosts = %s", str(result['hosts']))
            self.log.info(str(result['data']))

    def sleep(self, seconds):
        """Sleep."""
        self.log.info('Sleeping for %s seconds', seconds)
        time.sleep(seconds)

    @contextlib.contextmanager
    def journalctl_step(self, step):
        """Capture journalctl for a test step."""
        self.log_step(step)
        t_before = journalctl_time()
        yield
        self.server_journalctl(t_before)

    def test_daos_12513(self):
        """JIRA ID: DAOS-12513

        :avocado: tags=all,manual
        :avocado: tags=vm
        :avocado: tags=Daos12513,test_daos_12513
        """
        num_ranks = len(self.hostlist_servers)
        dmg = self.get_dmg_command()

        with self.journalctl_step('Create and query pool'):
            pool = self.get_pool()
            pool.query()

        with self.journalctl_step('Stop single rank and wait for rebuild'):
            self.server_managers[0].stop_ranks([2], self.d_log)
            dmg.system_query()
            pool.wait_for_rebuild_to_start()
            pool.wait_for_rebuild_to_end()

        with self.journalctl_step('Restart single rank'):
            dmg.system_start(ranks=[2])
            dmg.system_query()
            self.sleep(5)
            self.server_managers[0].update_expected_states([2], ["joined"])

        with self.journalctl_step('Reintegrate rank and wait for rebuild'):
            dmg.pool_list()
            pool.reintegrate("2")
            pool.wait_for_rebuild_to_start()
            pool.wait_for_rebuild_to_end()

        with self.journalctl_step('Exclude 2 ranks and wait for rebuild'):
            pool.exclude([2, 3])
            dmg.system_query()
            pool.wait_for_rebuild_to_start()
            pool.wait_for_rebuild_to_end()

        with self.journalctl_step('Reintegrate 2 ranks and wait for rebuild'):
            pool.reintegrate("2")
            pool.reintegrate("3")
            pool.wait_for_rebuild_to_start()
            pool.wait_for_rebuild_to_end()

        with self.journalctl_step('Crash a server and wait for rebuild'):
            kill_cmd = "sudo -n pkill daos_server --signal KILL && sudo systemctl stop daos_server"
            if not run_remote(self.log, NodeSet(self.hostlist_servers[-1]), kill_cmd):
                self.fail("failed to pkill daos_server")
            pool.wait_for_rebuild_to_start()
            pool.wait_for_rebuild_to_end()

        with self.journalctl_step('Restart server'):
            self.server_managers[0].restart([self.hostlist_servers[-1]], wait=True)

        with self.journalctl_step('Reintegrate all ranks and wait for rebuild'):
            for rank in range(num_ranks):
                pool.reintegrate(str(rank))
            pool.wait_for_rebuild_to_start()
            pool.wait_for_rebuild_to_end()

        with self.journalctl_step('Stop/start all ranks'):
            for rank in range(num_ranks):
                dmg.system_stop(ranks=str(rank))
                dmg.system_start(ranks=str(rank))
                self.sleep(5)
