#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers
from random import randint
from command_utils_base import CommandFailure
from general_utils import DaosTestError


class PoolFaultInjection(TestWithServers):
    """Test class Fault domains
    :avocado: recursive
    """

    def look_missed_request(self, cmd_stderr, msg=b"MS request error"):
        """ Read dmg_stderr for the msg string
        If found, the instance attribute self.failed_request is
        increased by 1.
        """
        if msg in cmd_stderr:
            self.failed_requests += 1

    def create_and_write_into_container(self):
        """ Create a container
        Write objects into container
        Close container
        Look for missed requests shown by daos command.
        """
        # Get and write into container
        self.container = self.get_container(self.pool,
                                            namespace=self.container_namespace)
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
        """ Exclude a random server from the pool
        Wait for rebuild
        Reintegrate the server back
        Wait for rebuild

        Due to the nature of how wait_for_rebuild() is coded
        we can only get the last dmg command output.
        """
        server_to_exclude = randint(0, len(self.hostlist_servers) - 1)
        self.pool.exclude([server_to_exclude])
        self.look_missed_request(self.pool.dmg.result.stderr)

        self.pool.wait_for_rebuild(True)
        self.look_missed_request(self.pool.dmg.result.stderr)
        self.pool.wait_for_rebuild(False)
        self.look_missed_request(self.pool.dmg.result.stderr)
        self.pool.reintegrate(str(server_to_exclude))
        self.pool.wait_for_rebuild(True)
        self.look_missed_request(self.pool.dmg.result.stderr)
        self.pool.wait_for_rebuild(False)
        self.look_missed_request(self.pool.dmg.result.stderr)

    def _clean(self):
        """Destroy container and pool
        Look for missed request on each executed command
        """
        self.container.destroy()
        self.look_missed_request(
            self.container.daos.result.stderr)
        self.pool.destroy(force=1)
        self.look_missed_request(self.pool.dmg.result.stderr)

    def test_pool_services(self):
        """ Test the following pool commands:
                Create
                Delete
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
        :avocado: tags=hw,large
        :avocado: tags=pool_with_faults,test_pool_services
        """
        failed_commands = 0
        dmg_command = self.get_dmg_command()
        pool_namespace = self.params.get("pool", "/run/*")
        number_pools = self.params.get("number_pools", "/run/*")
        self.failed_requests = 0
        self.object_class = self.params.get("object_class", "/run/*")
        self.container_namespace = self.params.get("container", "/run/*")
        self.number_servers = len(self.hostlist_servers) - 1

        for index in range(number_pools):
            try:
                # Create pool
                self.pool = self.get_pool(namespace=pool_namespace)
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

            except CommandFailure as e:
                self.log.error(str(e))
                failed_commands += 1
            except DaosTestError as e:
                self.log.error(str(e))
                failed_commands += 1
            finally:
                if self.pool:
                    self._clean()
        end_message = f"Failed commands: {failed_commands} \n" \
                      f"Failed requests: {self.failed_requests}"
        self.log.info(end_message)
        assert failed_commands == 0, "Execution of command(s) failed."
        assert self.failed_requests > 0, "No faults detected."
