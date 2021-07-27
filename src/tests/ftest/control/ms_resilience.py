#!/usr/bin/python
"""
(C) Copyright 2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from general_utils import stop_processes
import random
import socket
import time


class ManagementServiceResilience(TestWithServers):
    """Test Class Description:
    Verify that expected MS functionality is retained in various
    failure/recovery scenarios.

    The raft protocol guarantees 2N+1 resiliency, where N is the number of
    nodes that can fail while leaving the cluster in a state where it can
    continue to make progress. e.g. with N=1, a minimum of 3 nodes is needed
    in the cluster in order to tolerate 1 node failure; with N=2, a minimum
    of 5 nodes is required, etc.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Inititialize a ManagementServiceResilience object."""
        super().__init__(*args, **kwargs)
        self.setup_start_servers = False
        self.start_servers_once = False
        self.L_QUERY_TIMER = 300 # adjust down as we learn more
        self.SWIM_SUSP_TIMEOUT = 8

    def find_pool(self, search_uuid):
        """Find a pool in the output of `dmg pool list`.

        Args:
            uuid (str): Pool UUID to find.

        Returns:
            Pool entry, if found, or None.
        """
        for pool_uuid in self.get_dmg_command().pool_list():
            if pool_uuid.lower() == search_uuid.lower():
                return pool_uuid
        return None

    def create_pool(self):
        """Create a pool on the server group."""
        self.add_pool(create=False)
        self.pool.name.value = self.server_group
        self.log.info("*** creating pool")
        self.pool.create()

        self.log.info("Pool UUID %s on server group: %s",
                        self.pool.uuid, self.server_group)
        # Verify that the pool persisted.
        if self.find_pool(self.pool.uuid):
            self.log.info("Found pool in system.")
        else:
            self.fail("No pool found in system.")

    def get_leader(self):
        """Fetch the current system leader.

        Returns:
            str: hostname of the MS leader, or None
        """
        sys_leader_info = self.get_dmg_command().system_leader_query()
        l_addr = sys_leader_info["response"]["CurrentLeader"]

        if not l_addr:
            return None

        l_hostname, _, _ = socket.gethostbyaddr(l_addr.split(":")[0])
        l_hostname = l_hostname.split(".")[0]

        return l_hostname

    def verify_leader(self, replicas):
        """Verify the leader of the MS is in the replicas.

        Args:
            replicas (list): list of hostnames representing the access points
                for the MS.

        Returns:
            str: address of the MS leader.

        """
        l_hostname = None
        start = time.time()
        while not l_hostname and (time.time() - start) < self.L_QUERY_TIMER:
            l_hostname = self.get_leader()
            # Check that the new leader is in the access list
            if l_hostname not in replicas:
                self.log.error("Selected leader <%s> is not within the replicas"
                               " provided to servers", l_hostname)
            time.sleep(1)

        elapsed = time.time() - start
        if not l_hostname:
            self.fail("No leader found after {:.2f}s!".format(elapsed))

        self.log.info("*** found leader (%s) after %.2fs", l_hostname, elapsed)
        return l_hostname

    def launch_servers(self, resilience_num):
        """Setup and start the daos_servers.

        Args:
            resilience_num (int): minimum amount of MS replicas to achieve
                resiliency.

        Returns:
            list: list of access point hosts where MS has been started.

        """
        self.log.info("*** launching %d servers", resilience_num)
        replicas = random.sample(self.hostlist_servers, resilience_num)
        server_groups = {
            self.server_group:
                {
                    "hosts": self.hostlist_servers,
                    "access_points": replicas,
                    "svr_config_file": None,
                    "dmg_config_file": None,
                    "svr_config_temp": None,
                    "dmg_config_temp": None
                },
        }
        self.start_servers(server_groups)
        return replicas

    def kill_servers(self, leader, replicas, N):
        """Kill a subset of servers in order to simulate failures.

        Args:
          leader (str): hostname of current leader.
          replicas (list): list of replica hostnames.
          N (int): Number of hosts (including leader) to stop.

        Returns:
          kill_list: list of hosts that were stopped.

        """
        kill_list = set(random.sample(replicas, N))
        if leader not in kill_list:
            kill_list.pop()
            kill_list.add(leader)
        self.log.info("*** stopping leader (%s) + %d others", leader, N-1)
        stop_processes(kill_list,
                       self.server_managers[0].manager.job.command_regex)

        kill_ranks = self.server_managers[0].get_host_ranks(kill_list)
        self.assertGreaterEqual(len(kill_ranks), len(kill_list),
            "Unable to obtain expected ranks for {}".format(kill_list))
        self.server_managers[0].update_expected_states(
            kill_ranks, ["stopped", "excluded"])

        return kill_list

    def verify_retained_quorum(self, N):
        """Verify 2N+1 resiliency

        This method will launch 2 * N + 1 servers, stop the MS leader
        plus just enough replicas to retain quorum, then verify that
        a new leader has been elected from the surviving hosts. Finally,
        the test will verify that the MS is still writable by creating
        a new pool.
        """
        replicas = self.launch_servers((2 * N) + 1)
        leader = self.verify_leader(replicas)

        # First, kill the leader plus just enough other replicas to
        # push up to the edge of quorum loss.
        kill_list = self.kill_servers(leader, replicas, N)

        # Next, verify that one of the replicas has stepped up as
        # the new leader.
        survivors = [x for x in replicas if x not in kill_list]
        self.verify_leader(survivors)
        self.get_dmg_command().hostlist = self.hostlist_servers

        # Dump the current system state.
        self.get_dmg_command().system_query()

        # Finally, verify that quorum has been retained by performing
        # write operations.
        self.create_pool()
        self.pool = None

    def verify_regained_quorum(self, N):
        """Test that even with 2N+1 resiliency lost, reads still work, and
           that quorum can be regained.

        This method will launch 2 * N + 1 servers, then use a kill_list to
        stop the MS leader plus enough replicas to lose quorum. The test will
        verify that the MS still operates in a degraded read-only mode before
        restarting the killed servers in order to check that the MS is once
        again available for writing.
        """
        replicas = self.launch_servers((2 * N) + 1)
        leader = self.verify_leader(replicas)

        # First, create a pool.
        self.create_pool()

        # Next, kill the leader plus enough other replicas to
        # lose quorum.
        kill_list = self.kill_servers(leader, replicas, N+1)

        self.get_dmg_command().hostlist = self.hostlist_servers

        # Now, try to perform some read-only operations to verify
        # that they work on a MS without quorum.
        if not self.get_dmg_command().system_leader_query():
            self.fail("Can't query system after quorum loss.")
        if not self.find_pool(self.pool.uuid):
            self.fail("Can't list pools after quorum loss.")
        self.pool = None

        # TODO (DAOS-7812): Remove this sleep after we've exposed rank
        # incarnation numbers to the MS via Join/RAS. Until then, we
        # have to make sure that all swim_rank_dead events have trickled
        # in before restarting the servers, otherwise we can encounter
        # a situation where an event for the previous incarnation of a
        # rank causes the current incarnation to be marked excluded.
        time.sleep(self.SWIM_SUSP_TIMEOUT * 2)

        # Finally, restart the dead servers and verify that quorum is
        # regained, which should allow for write operations to succeed again.
        self.server_managers[0].restart(list(kill_list), wait=True)
        self.verify_leader(replicas)

        # Dump the current system state.
        self.get_dmg_command().system_query()

        self.create_pool()
        self.pool = None

    def test_ms_resilience_1(self):
        """
        JIRA ID: DAOS-3798

        Test Description:
            Test management service is accessible after 1 instance is removed.

        N = 1 servers as access_points where, N = failure tolerance,
        resilience_num = minimum amount of MS replicas to achieve resiliency.
        This test case will shutdown 1 server and verify we still have MS
        resiliency.

        :avocado: tags=all,pr,daily_regression,control,ms_resilience
        :avocado: tags=ms_retained_quorum_N_1
        """
        # Run test cases
        self.verify_retained_quorum(1)

    def test_ms_resilience_2(self):
        """
        JIRA ID: DAOS-3798

        Test Description:
            Test management service is accessible after 2 instances are removed.

        N = 2 servers as access_points where, N = failure tolerance,
        resilience_num = minimum amount of MS replicas to achieve resiliency.
        This test case will shutdown 1 server and verify we still have MS
        resiliency.

        :avocado: tags=all,pr,daily_regression,control,ms_resilience
        :avocado: tags=ms_retained_quorum_N_2
        """
        # Run test cases
        self.verify_retained_quorum(2)

    def test_ms_resilience_3(self):
        """
        JIRA ID: DAOS-3798

        Test Description:
            Test management service is accessible for reading with resiliency
            lost.

        N = 1 servers as access_points where, N = failure tolerance,
        resilience_num = minimum amount of MS replicas to achieve resiliency.
        This test case will shutdown 1 + 1 replicas hosts and check we've
        lost resiliency.

        :avocado: tags=all,pr,daily_regression,control,ms_resilience
        :avocado: tags=ms_regained_quorum_N_1
        """
        # Run test case
        self.verify_regained_quorum(1)

    def test_ms_resilience_4(self):
        """
        JIRA ID: DAOS-3798

        Test Description:
            Test management service is accessible for reading with resiliency
            lost.

        N = 2 servers as access_points where, N = failure tolerance,
        resilience_num = minimum amount of MS replicas to achieve resiliency.
        This test case will shutdown 2 + 1 replicas hosts and check we've
        lost resiliency.

        :avocado: tags=all,pr,daily_regression,control,ms_resilience
        :avocado: tags=ms_regained_quorum_N_2
        """
        # Run test case
        self.verify_regained_quorum(2)
