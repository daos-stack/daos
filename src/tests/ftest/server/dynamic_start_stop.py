#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

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
        super(DynamicStartStop, self).__init__(*args, **kwargs)
        self.dmg_cmd = None
        self.stopped_ranks = set()

    def verify_system_query_single(self, verifying_rank):
        """Verify state of the given rank eventually becomes "evicted" in JSON.

        Args:
            verifying_rank (int): Rank to verify.
        """
        state = None
        for _ in range(3):
            members = self.dmg_cmd.system_query()
            state = members[verifying_rank]["state"]
            if state == "evicted":
                break
            time.sleep(5)
        self.assertEqual(
            state, "evicted", "State isn't evicted! Actual: {}".format(state))

    def verify_system_query_all(self):
        """Verify state of the ranks.

        Call dmg system query --json and verify the State of each rank. If the
        rank is in self.stopped_ranks, verify that its status is "evicted".
        Otherwise, "joined".
        """
        members = self.dmg_cmd.system_query()
        for rank, member in members.items():
            if int(rank) in self.stopped_ranks:
                self.assertEqual(
                    member["state"], "evicted",
                    "State isn't evicted! Actual: {}".format(member["state"]))
                self.assertEqual(
                    member["reason"], "system stop",
                    "Info (Reason) isn't system stop! Actual: {}".format(
                        member["reason"]))
            else:
                self.assertEqual(
                    member["state"], "joined",
                    "State isn't joined! Actual: {}".format(member["state"]))

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
        :avocado: tags=server
        :avocado: tags=dynamic_start_stop
        """
        self.dmg_cmd = self.get_dmg_command()
        self.add_pool()
        extra_servers = self.params.get("test_servers", "/run/extra_servers/*")

        # Start 3 extra servers.
        self.start_additional_servers(additional_servers=extra_servers)

        # Call dmg system query and verify that the State of all ranks is
        # Joined.
        self.verify_system_query_all()

        # Stop one of the added servers - Single stop.
        #self.dmg_cmd.system_stop(ranks="4")
        self.dmg_cmd.system_stop(ranks="2")

        # Verify that the State of the stopped server is evicted and Reason is
        # system stop.
        self.stopped_ranks.add(4)
        self.verify_system_query_single(4)
        self.verify_system_query_all()

        # Stop two of the added servers - Multiple stop.
        self.dmg_cmd.system_stop(ranks="2,3")

        # Verify that the State of the stopped servers is evicted and Reason is
        # system stop.
        self.stopped_ranks.add(2)
        self.stopped_ranks.add(3)
        self.verify_system_query_single(2)
        self.verify_system_query_single(3)
        self.verify_system_query_all()

        # Stop one of the original servers.
        self.dmg_cmd.system_stop(ranks="1")

        # Verify that the State of the stopped servers is evicted and Reason is
        # system stop.
        self.stopped_ranks.add(1)
        self.verify_system_query_single(1)
        self.verify_system_query_all()
