"""
  (C) Copyright 2020-2023 Intel Corporation.
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from random import choice

from apricot import TestWithServers
from exception_utils import CommandFailure
from general_utils import DaosTestError


class PoolServicesFaultInjection(TestWithServers):
    """Test class Fault domains.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a PoolServicesFaultInjection object."""
        super().__init__(*args, **kwargs)
        self.failed_requests = 0
        self.object_class = None
        self.number_servers = 0

    def setUp(self):
        """Set up each test case."""
        super().setUp()
        self.failed_requests = 0
        self.object_class = self.params.get("object_class", "/run/*")
        self.number_servers = len(self.hostlist_servers) - 1

    def look_missed_request(self, cmd_stderr, msg=b"err: DER_TIMEDOUT"):
        """Read dmg_stderr for the msg string.

        If found, the instance attribute self.failed_request is increased by 1.
        """
        if msg in cmd_stderr:
            self.failed_requests += 1

    def create_and_write_into_container(self):
        """Create a container.

        Write objects into container
        Close container
        Look for missed requests shown by daos command.
        """
        # Get and write into container
        self.container = self.get_container(self.pool)
        self.look_missed_request(self.container.daos.result.stderr)

        # This is done through IOR
        # No daos/dmg message to catch
        for server in range(0, self.number_servers):
            self.container.write_objects(server, obj_class=self.object_class)
        # Close container to avoid problems when deleting
        # The container and/or pool
        self.container.close()
        self.look_missed_request(self.container.daos.result.stderr)

    def exclude_and_reintegrate(self):
        """Exclude a random pool rank, wait for rebuild, reintegrate it, wait again.

        Each dmg call uses --wait so it blocks until the triggered rebuild
        completes; the missed-request check inspects the cumulative dmg stderr
        from the op and its rebuild polling.

        The rank is picked from the pool's *enabled* ranks rather than from
        the host index range — the pool's --nranks may be smaller than the
        cluster's rank count, so not every host index maps to an in-pool rank.
        """
        query = self.pool.dmg.pool_query(self.pool.identifier, show_enabled=True)
        enabled_ranks = query["response"].get("enabled_ranks") or []
        if not enabled_ranks:
            self.fail("Pool has no enabled ranks; cannot run exclude/reintegrate.")

        rank_to_exclude = choice(enabled_ranks)  # nosec
        self.pool.exclude([rank_to_exclude], wait=True)
        self.look_missed_request(self.pool.dmg.result.stderr)

        self.pool.reintegrate(str(rank_to_exclude), wait=True)
        self.look_missed_request(self.pool.dmg.result.stderr)

    def _clean(self):
        """Destroy container and pool.

        Look for missed request on each executed command
        """
        if self.container:
            self.container.destroy()
            self.look_missed_request(
                self.container.daos.result.stderr)
        if self.pool:
            self.pool.destroy(force=1)
            self.look_missed_request(self.pool.dmg.result.stderr)

    def test_pool_services(self):
        """Test the following pool commands:
                Create
                Create container
                Write container (not a pool command)
                Exclude rank from pool
                Reintegrate rank to pool
                List
                Destroy

            All the request have fault injection of a probability of 5% which
            attempts to mimic a flaky internet connection. Daos tools should
            re-attempt the requests without failing.

            The only passes if the following two conditions pass:
                All commands executed successfully
                There was at least one missed request, which should be an
                injected fault of real network issue.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=fault_injection,pool,faults,rebuild
        :avocado: tags=PoolServicesFaultInjection,test_pool_services
        """
        failed_commands = 0
        dmg_command = self.get_dmg_command()
        number_pools = self.params.get("number_pools", "/run/*")
        for _ in range(number_pools):
            try:
                # Create pool
                self.pool = self.get_pool()
                self.look_missed_request(self.pool.dmg.result.stderr)

                # Container section
                self.create_and_write_into_container()

                # Remove rank and add it back
                self.exclude_and_reintegrate()

                # Simple dmg commands
                dmg_command.system_query()
                self.look_missed_request(dmg_command.result.stderr)
                dmg_command.pool_list()
                self.look_missed_request(dmg_command.result.stderr)

                # Delete pool and container
                self._clean()

            except CommandFailure as error:
                self.log.error(str(error))
                failed_commands += 1
            except DaosTestError as error:
                self.log.error(str(error))
                failed_commands += 1
            finally:
                if self.pool:
                    self._clean()
        end_message = f"Failed commands: {failed_commands} \n" \
                      f"Failed requests: {self.failed_requests}"
        self.log.info(end_message)
        self.assertEqual(failed_commands, 0, "Execution of command(s) failed.")
        self.assertGreater(self.failed_requests, 0, "No faults detected.")

    def manual_test_create_containers_while_blocking_daos_using_iptables(self):
        """This is a manual test.

        Using CaRT as test pool services is a good way to add noise to the
        tools, however there are other ways to simulate a flaky network
        connection.

        The happy path steps are:
            Backup iptables rules
            Block network access to the interface used by daos
            Attempt to create containers
            Create containers fail
            Restore iptables rules

        Simulating a failing network connection:
            Backup iptables rules
            Block network access to the interface used by daos
            Attempt to create containers
            While the previous command is running,  Restore iptables rules
            Daos tool should create container

        Why this test is not being automated:

            1) Not all fabric hardware support IPoIB
            2) If there is no fabric hw, at least 2 ethernet devices are
               required, this is to avoid losing connection to the host.

        Details and how to:

        First of all, a backup of the current iptables rules is required:

            sudo iptables-save > savedrules.txt

        To restore the backup:

            sudo iptables-restore < savedrules.txt

        If ib0 interface is not enabled:
            1) Make sure the fabric hardware supports IPoIB
            2) sudo modprobe ib_ipoib
            3) Verify the ibX interface is listed: ip -a
            4) Use the IPV4 ip to be used to block the network traffic

        How to block the access:

            sudo iptables -A INPUT -s 192.168.100.64 -j DROP

            Where:
                192.168.100.64 is the ib0 network interface
                It could be an ethernet interface, just make sure it is not the
                same interface used for remote connection. If there is access
                through serial console, you may as well use a single eth device.

            Note:
                Blocking using ip port did not prevent daos to create conts:
                sudo iptables -A INPUT -s 192.168.100.64 -p tcp
                              --destination-port 10001 -j DROP

                    The rules for OUTPUT, tcp and udp combinations were also
                    tried.

        Once iptables has the traffic blocked, attempt to create containers.
        It should fail.

        Afterwards, the idea is to turn on and off the iptable rules to simulate
        a failing network connection, daos tool should re-attempt the failed
        request. This could be coded as:
            A thread/process turning on and off the iptable rules
            A thread/process creating containers

        The test can be extended to other dmg/daos operations.
        """
        raise NotImplementedError()
