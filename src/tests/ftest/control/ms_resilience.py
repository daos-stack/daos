"""
(C) Copyright 2021-2023 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import socket
import time

from ClusterShell.NodeSet import NodeSet

from apricot import TestWithServers
from run_utils import stop_processes


def get_hostname(host_addr):
    """Get the hostname of a host.

    Args:
        host_addr (str): address to resolve into hostname.

    Returns:
        NodeSet: hostname of the host.

    """
    if not host_addr:
        return NodeSet()

    fqdn, _, _ = socket.gethostbyaddr(host_addr.split(":")[0])
    return NodeSet(fqdn.split(".")[0])


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

    L_QUERY_TIMER = 30

    def __init__(self, *args, **kwargs):
        """Initialize a ManagementServiceResilience object."""
        super().__init__(*args, **kwargs)
        self.setup_start_servers = False
        self.start_servers_once = False

    def find_pool(self, search_uuid):
        """Find a pool in the output of `dmg pool list`.

        Args:
            uuid (str): Pool UUID to find.

        Returns:
            Pool entry, if found, or None.

        """
        pool_uuids = self.get_dmg_command().get_pool_list_uuids(no_query=True)
        for pool_uuid in pool_uuids:
            if pool_uuid.lower() == search_uuid.lower():
                return pool_uuid
        return None

    def create_pool(self):
        """Create a pool on the server group."""
        self.add_pool(create=False)
        self.pool.name.value = self.server_group
        self.log.info("*** creating pool")
        self.pool.create()

        self.log.info("Pool UUID %s on server group: %s", self.pool.uuid, self.server_group)
        # Verify that the pool persisted.
        while not self.find_pool(self.pool.uuid):
            # Occasionally the pool may not be found
            # immediately after creation if the read
            # is serviced by a non-leader replica.
            self.log.info("Pool %s not found yet.", self.pool.uuid)
            time.sleep(1)
        self.log.info("Found pool in system.")

    def get_leader(self):
        """Fetch the current system leader.

        Returns:
            NodeSet: hostname of the MS leader, or None

        """
        sys_leader_info = self.get_dmg_command().system_leader_query()
        l_addr = sys_leader_info["response"]["current_leader"]

        return get_hostname(l_addr)

    def verify_leader(self, replicas):
        """Verify the leader of the MS is in the replicas.

        Args:
            replicas (NodeSet): host names representing the access points for the MS.

        Returns:
            NodeSet: hostname of the MS leader.

        """
        l_hostname = self.get_leader()
        start = time.time()
        while not l_hostname.intersection(replicas) and (time.time() - start) < self.L_QUERY_TIMER:
            self.log.info("Current leader: <%s>; waiting for new leader to step up", l_hostname)
            time.sleep(1)
            l_hostname = self.get_leader()

        elapsed = time.time() - start
        if not l_hostname:
            self.fail("No leader found after {:.2f}s!".format(elapsed))

        self.log.info("*** found leader (%s) after %.2fs", l_hostname, elapsed)
        return l_hostname

    def verify_dead(self, kill_list):
        """Verify that the expected list of killed servers are marked dead.

        Args:
            kill_list (NodeSet): hostnames of servers to verify are dead.

        Returns:
            None

        """
        hostnames = {}

        while True:
            time.sleep(1)
            dead_list = NodeSet()
            members = self.get_dmg_command().system_query()["response"]["members"]
            for member in members:
                if member["addr"] not in hostnames:
                    hostname = get_hostname(member["addr"])
                    if not hostname:
                        self.fail("Unable to resolve {} to hostname".format(member["addr"]))
                    hostnames[member["addr"]] = hostname

                if member["state"] == "excluded":
                    dead_list.add(hostnames[member["addr"]])

            if len(dead_list) != len(kill_list):
                self.log.info("*** waiting for %d dead servers to be marked dead", len(kill_list))
                continue

            for hostname in kill_list:
                if hostname not in dead_list:
                    self.log.error("Server %s not found in dead_list", hostname)
                    self.fail("Found more dead servers than expected!")

            self.log.info("*** detected %d dead servers", len(dead_list))
            return

    def launch_servers(self, resilience_num):
        """Set up and start the daos_servers.

        Args:
            resilience_num (int): minimum amount of MS replicas to achieve
                resiliency.

        Returns:
            NodeSet: access point hosts where MS has been started.

        """
        self.log.info("*** launching %d servers", resilience_num)
        replicas = NodeSet.fromlist(
            self.random.sample(list(self.hostlist_servers), resilience_num))
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

        # Give SWIM some time to stabilize before we start killing stuff (DAOS-9116)
        time.sleep(2 * len(self.hostlist_servers))

        return replicas

    def kill_servers(self, leader, replicas, num_hosts):
        """Kill a subset of servers in order to simulate failures.

        Args:
            leader (NodeSet): hostname of current leader.
            replicas (NodeSet): replica hostnames.
            num_hosts (int): Number of hosts (including leader) to stop.

        Returns:
            NodeSet: hosts that were stopped.

        """
        kill_list = NodeSet.fromlist(self.random.sample(list(replicas), num_hosts))
        if not leader.intersection(kill_list):
            kill_list.remove(kill_list[-1])
            kill_list.add(leader)
        self.log.info("*** stopping leader (%s) + %d others: %s", leader, num_hosts - 1, kill_list)
        stop_processes(self.log, kill_list, self.server_managers[0].manager.job.command_regex)

        kill_ranks = self.server_managers[0].get_host_ranks(kill_list)
        self.assertGreaterEqual(len(kill_ranks), len(kill_list),
                                "Unable to obtain expected ranks for {}".format(kill_list))
        self.server_managers[0].update_expected_states(
            kill_ranks, ["stopped", "excluded"])

        return kill_list

    def verify_retained_quorum(self, num_hosts):
        """Verify 2N+1 resiliency.

        This method will launch 2 * N + 1 servers, stop the MS leader
        plus just enough replicas to retain quorum, then verify that
        a new leader has been elected from the surviving hosts. Finally,
        the test will verify that the MS is still writable by creating
        a new pool.
        """
        replicas = self.launch_servers((2 * num_hosts) + 1)
        self.log.debug("<<<verify_retained_quorum>>> replicas  = %s", replicas)
        leader = self.verify_leader(replicas)
        self.log.debug("<<<verify_retained_quorum>>> leader    = %s", leader)

        # First, kill the leader plus just enough other replicas to
        # push up to the edge of quorum loss.
        kill_list = self.kill_servers(leader, replicas, num_hosts)
        self.log.debug("<<<verify_retained_quorum>>> kill_list = %s", kill_list)

        # Next, verify that one of the replicas has stepped up as
        # the new leader.
        survivors = replicas.difference(kill_list)
        self.log.debug("<<<verify_retained_quorum>>> survivors = %s", survivors)
        self.verify_leader(survivors)
        self.get_dmg_command().hostlist = self.hostlist_servers

        # Wait until SWIM has marked the replicas as dead in order to
        # avoid long timeouts and other issues.
        self.verify_dead(kill_list)

        # Finally, verify that quorum has been retained by performing
        # write operations.
        self.create_pool()
        self.pool = None

    def verify_regained_quorum(self, num_hosts):
        """Test that even with 2N+1 resiliency lost, reads still work, and
           that quorum can be regained.

        This method will launch 2 * N + 1 servers, then use a kill_list to
        stop the MS leader plus enough replicas to lose quorum. The test will
        verify that the MS still operates in a degraded read-only mode before
        restarting the killed servers in order to check that the MS is once
        again available for writing.
        """
        replicas = self.launch_servers((2 * num_hosts) + 1)
        leader = self.verify_leader(replicas)

        # First, create a pool.
        self.create_pool()

        # Next, kill the leader plus enough other replicas to
        # lose quorum.
        kill_list = self.kill_servers(leader, replicas, num_hosts + 1)

        self.get_dmg_command().hostlist = self.hostlist_servers

        # Now, try to perform some read-only operations to verify
        # that they work on a MS without quorum.
        if not self.get_dmg_command().system_leader_query():
            self.fail("Can't query system after quorum loss.")
        if not self.find_pool(self.pool.uuid):
            self.fail("Can't list pools after quorum loss.")
        self.pool = None

        # Finally, restart the dead servers and verify that quorum is
        # regained, which should allow for write operations to succeed again.
        self.server_managers[0].restart(kill_list, wait=True)
        self.verify_leader(replicas)

        # Dump the current system state.
        self.get_dmg_command().system_query()

        self.create_pool()
        self.pool = None

    def test_ms_resilience_1(self):
        """
        JIRA ID: DAOS-3798

        Test Description:
            Test N=1 management service is accessible after 1 instance is removed.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,ms_resilience,ms_retained_quorum_N_1
        :avocado: tags=ManagementServiceResilience,test_ms_resilience_1
        """
        # Run test cases
        self.verify_retained_quorum(1)

    def test_ms_resilience_2(self):
        """
        JIRA ID: DAOS-3798

        Test Description:
            Test N=2 management service is accessible after 2 instances are removed.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,ms_resilience,ms_retained_quorum_N_2
        :avocado: tags=ManagementServiceResilience,test_ms_resilience_2
        """
        # Run test cases
        self.verify_retained_quorum(2)

    def test_ms_resilience_3(self):
        """
        JIRA ID: DAOS-3798

        Test Description:
            Test N=1 management service is accessible for reading with resiliency
            lost (degraded mode), and then test that quorum can be regained for
            full functionality.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,ms_resilience,ms_regained_quorum_N_1
        :avocado: tags=ManagementServiceResilience,test_ms_resilience_3
        """
        # Run test case
        self.verify_regained_quorum(1)

    def test_ms_resilience_4(self):
        """
        JIRA ID: DAOS-3798

        Test Description:
            Test N=2 management service is accessible for reading with resiliency
            lost (degraded mode), and then test that quorum can be regained for
            full functionality.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,ms_resilience,ms_regained_quorum_N_2
        :avocado: tags=ManagementServiceResilience,test_ms_resilience_4
        """
        # Run test case
        self.verify_regained_quorum(2)
