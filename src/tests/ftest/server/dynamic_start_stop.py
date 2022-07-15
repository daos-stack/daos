#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers


class DynamicStartStop(TestWithServers):
    """Test Class Description:

    SRS-10-0052 is about stopping the server and SRS-10-0053 is about starting,
    so this test focuses on stopping and starting the new servers in an existing
    system with a pool.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DynamicStartStop object."""
        super().__init__(*args, **kwargs)
        self.dmg_cmd = None
        self.stopped_ranks = set()

    def verify_system_query(self):
        """Verify state of the ranks.

        Call dmg system query --json and verify the State of each rank. If the
        rank is in self.stopped_ranks, verify that its status is Stopped.
        Otherwise, Joined.
        """
        output = self.dmg_cmd.system_query()
        for member in output["response"]["members"]:
            if member["rank"] in self.stopped_ranks:
                self.assertIn(
                    member["state"], ["stopped", "excluded"],
                    "State isn't stopped! Actual: {}".format(member["state"]))
                self.assertEqual(
                    member["info"], "system stop",
                    "Info (Reason) isn't system stop! Actual: {}".format(
                        member["info"]))
            else:
                self.assertEqual(
                    member["state"], "joined",
                    "State isn't joined! Actual: {}".format(member["state"]))

    def stop_server_ranks(self, ranks):
        """Stop one or more server ranks.

        Args:
            ranks (list): [description]
        """
        # Stop the requested server ranks
        ranks_str = ",".join([str(rank) for rank in ranks])
        self.dmg_cmd.system_stop(ranks=ranks_str)

        # Mark which ranks are now stopped
        for manager in self.server_managers:
            manager.update_expected_states(ranks, ["stopped", "excluded"])
        for rank in ranks:
            self.stopped_ranks.add(rank)

        # Verify that the State of the stopped servers is Stopped and Reason is
        # system stop.
        self.verify_system_query()

    def test_dynamic_server_addition(self):
        """JIRA ID: DAOS-3598

        Test Steps:
        1. Start 2 servers and create a poll across these servers.
        2. Start 3 additional servers and let them join the system. Verify with
        dmg system query --verbose.
        3. Stop one of the added servers - Single stop.
        4. Stop two of the remaining added servers - Multiple stop.
        5. Stop one of the original servers - Stopping with pool.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=server
        :avocado: tags=dynamic_start_stop,test_dynamic_server_addition
        """
        self.add_pool()

        extra_servers = self.params.get("test_servers", "/run/extra_servers/*")

        # Start 3 extra servers.
        self.start_additional_servers(additional_servers=extra_servers)

        self.dmg_cmd = self.get_dmg_command()

        # Call dmg system query and verify that the State of all ranks is
        # Joined.
        self.verify_system_query()

        # Stop one of the added servers - Single stop.
        self.stop_server_ranks([4])

        # Stop two of the added servers - Multiple stop.
        self.stop_server_ranks([2, 3])

        # Stop one of the original servers.
        self.stop_server_ranks([1])

        # Stopping newly added server and destroy pool causes -1006. DAOS-5606
        self.pool.skip_cleanup()
