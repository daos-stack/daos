#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import json
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

    def verify_system_query(self):
        """Verify state of the ranks.

        Call dmg system query --json and verify the State of each rank. If the
        rank is in self.stopped_ranks, verify that its status is Stopped.
        Otherwise, Joined.
        """
        output = self.dmg_cmd.system_query().stdout
        data = json.loads(output)
        members = data["response"]["Members"]
        for member in members:
            if member["Rank"] in self.stopped_ranks:
                self.assertEqual(
                    member["State"],
                    self.dmg_cmd.SYSTEM_QUERY_STATES["STOPPED"],
                    "State isn't Stopped! Actual: {}".format(member["State"]))
                self.assertEqual(
                    member["Info"], "system stop",
                    "Info (Reason) isn't system stop! Actual: {}".format(
                        member["Info"]))
            else:
                self.assertEqual(
                    member["State"], self.dmg_cmd.SYSTEM_QUERY_STATES["JOINED"],
                    "State isn't Joined! Actual: {}".format(member["State"]))

    def test_dynamic_server_addition(self):
        """JIRA ID: DAOS-3598

        Test Steps:
        1. Start 2 servers and create a poll across these servers.
        2. Start 3 additional servers and let them join the system. Verify with
        dmg system query --verbose.
        3. Stop one of the added servers - Single stop.
        4. Stop two of the remaining added servers - Multiple stop.
        5. Stop one of the original servers - Stopping with pool.

        :avocado: tags=all,hw,large,server,full_regression,dynamic_start_stop
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
        self.dmg_cmd.system_stop(ranks="4")

        # Verify that the State of the stopped server is Stopped and Reason is
        # system stop.
        self.stopped_ranks.add(4)
        self.verify_system_query()

        # Stop two of the added servers - Multiple stop.
        self.dmg_cmd.system_stop(ranks="2,3")

        # Verify that the State of the stopped servers is Stopped and Reason is
        # system stop.
        self.stopped_ranks.add(2)
        self.stopped_ranks.add(3)
        self.verify_system_query()

        # Stop one of the original servers.
        self.dmg_cmd.system_stop(ranks="1")

        # Verify that the State of the stopped servers is Stopped and Reason is
        # system stop.
        self.stopped_ranks.add(1)
        self.verify_system_query()

        # Stopping newly added server and destroy pool causes -1006. DAOS-5606
        self.pool = None
