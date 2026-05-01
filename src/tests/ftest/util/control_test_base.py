"""
  (C) Copyright 2020-2022 Intel Corporation.
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from apricot import TestWithServers
from ClusterShell.NodeSet import NodeSet


class ControlTestBase(TestWithServers):
    """Defines common methods for control tests.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a ControlTestBase object."""
        super().__init__(*args, **kwargs)
        self.dmg = None

    def setUp(self):
        """Set up each test case."""
        super().setUp()
        self.dmg = self.get_dmg_command()

    def verify_dmg_storage_scan(self, verify_method):
        """Call dmg storage scan and run the given method with the output.

        Args:
            verify_method (method): Method that uses the generated output. Must
                return list of errors.
        """
        errors = []

        for manager in self.server_managers:
            data = manager.dmg.storage_scan(verbose=True)

            if manager.dmg.result.exit_status == 0:
                for struct_hash in data["response"]["HostStorage"]:
                    hash_dict = data["response"]["HostStorage"][struct_hash]
                    hosts = NodeSet(hash_dict["hosts"].split(":")[0])
                    if hosts in manager.hosts:
                        errors.extend(verify_method(hash_dict["storage"]))
            else:
                errors.append("dmg storage scan failed!")

        if errors:
            self.fail("\n--- Errors found! ---\n{}".format("\n".join(errors)))

    def get_all_ranks(self):
        """Get list of all ranks in the system.

        Returns:
            list: List of all rank numbers
        """
        return list(self.server_managers[0].ranks.keys())

    def get_rank_state(self, rank):
        """Get the state of a rank.

        Args:
            rank (int): Rank number

        Returns:
            str: Current state of the rank
        """
        data = self.dmg.system_query(ranks="%s" % rank)
        if data["status"] != 0:
            self.fail("Cmd dmg system query failed")
        if "response" in data and "members" in data["response"]:
            if data["response"]["members"] is None:
                self.fail("No members returned from dmg system query")
            for member in data["response"]["members"]:
                return member["state"].lower()
        self.fail("No member state returned from dmg system query")
        return None

    def exclude_rank_and_wait_restart(self, rank, expect_restart=True, timeout=30):
        """Exclude a rank and wait for it to self-terminate and potentially restart.

        Args:
            rank (int): Rank to exclude
            expect_restart (bool): Whether automatic restart is expected
            timeout (int): Maximum seconds to wait for restart

        Returns:
            tuple: (restarted, final_state) - whether rank restarted and its final state
        """
        self.log_step("Excluding rank %s", rank)
        self.dmg.system_exclude(ranks=[rank], rank_hosts=None)

        # Wait for rank to self-terminate (should go to AdminExcluded state)
        self.log_step("Waiting for rank %s to self-terminate", rank)
        time.sleep(2)

        # Check if rank is adminexcluded
        failed_ranks = self.server_managers[0].check_rank_state(
            ranks=[rank], valid_states=["adminexcluded"], max_checks=10)
        if failed_ranks:
            self.fail("Rank %s did not reach AdminExcluded state after exclusion" % rank)

        # After triggering rank exclusion with dmg system exclude, clear
        # AdminExcluded state so rank can join on auto-restart. This enables
        # mimic of rank exclusion via SWIM inactivity detection.
        self.log_step("Clearing exclusion for rank %s", rank)
        self.dmg.system_clear_exclude(ranks=[rank], rank_hosts=None)

        # Check if rank is excluded
        failed_ranks = self.server_managers[0].check_rank_state(
            ranks=[rank], valid_states=["excluded"], max_checks=10)
        if failed_ranks:
            self.fail("Rank %s did not reach Excluded state after clear-excluded" % rank)

        if expect_restart:
            # Wait for automatic restart (rank should go to Joined state)
            self.log_step("Waiting for rank %s to automatically restart", rank)
            start_time = time.time()
            restarted = False

            while time.time() - start_time < timeout:
                time.sleep(2)
                # Check if rank has rejoined
                failed_ranks = self.server_managers[0].check_rank_state(
                    ranks=[rank], valid_states=["joined"], max_checks=1)
                if not failed_ranks:
                    restarted = True
                    break

            if restarted:
                self.log.info("Rank %s automatically restarted and rejoined", rank)
                return (True, "joined")
            state = self.get_rank_state(rank)
            self.log.error("Rank %s (%s) did not restart within %ss", rank, state, timeout)
            return (False, state)
        # Verify rank stays AdminExcluded (no automatic restart)
        self.log_step("Verifying rank %s does not automatically restart", rank)
        time.sleep(timeout)

        failed_ranks = self.server_managers[0].check_rank_state(
            ranks=[rank], valid_states=["adminexcluded"], max_checks=1)
        if failed_ranks:
            state = self.get_rank_state(rank)
            self.log.error("Rank %s (%s) unexpectedly restarted", rank, state)
            return (True, state)
        return (False, "adminexcluded")

    def get_rank_incarnation(self, rank):
        """Get the incarnation number of a rank.

        The incarnation number increments each time a rank restarts, allowing
        verification that a rank has actually restarted rather than just
        remaining in the same state.

        Args:
            rank (int): Rank number

        Returns:
            int: Current incarnation number of the rank, or None if not found

        Raises:
            None - logs error and returns None on failure
        """
        try:
            data = self.dmg.system_query(ranks=f"{rank}")
            if data.get("status") != 0:
                self.log.error("dmg system query failed for rank %s", rank)
                return None

            if "response" not in data or "members" not in data["response"]:
                self.log.error("Invalid response from dmg system query for rank %s", rank)
                return None

            members = data["response"]["members"]
            if not members:
                self.log.error("No members returned from dmg system query for rank %s", rank)
                return None

            for member in members:
                if member.get("rank") == rank:
                    incarnation = member.get("incarnation")
                    if incarnation is not None:
                        self.log.debug("Rank %s incarnation: %s", rank, incarnation)
                        return incarnation
                    self.log.error("No incarnation field for rank %s", rank)
                    return None

            self.log.error("Rank %s not found in system query response", rank)
            return None

        except Exception as error:  # pylint: disable=broad-exception-caught
            # Catch all exceptions to prevent test framework crashes during rank queries
            self.log.error("Exception getting incarnation for rank %s: %s", rank, error)
            return None
