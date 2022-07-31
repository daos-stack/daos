#!/usr/bin/python3
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from avocado.core.exceptions import TestFail
from exception_utils import CommandFailure


class ListPoolsTest(TestWithServers):
    """Test class for dmg pool list tests.

    Test Class Description:
        This class contains tests for list pool.

    :avocado: recursive
    """

    def run_case(self, rank_lists, sr=None):
        """Run test case.

        Create pools, call dmg pool list to get the list, and compare against
        the UUIDs and service replicas returned at create time.

        Args:
            rank_lists (list): Rank lists. List of list of int.
            sr (str, optional): Service replicas. Defaults to None.

        Raises:
            CommandFailure: if there was an error destoying pools
            TestFail: if there was an error verifying the created pools

        """
        # Iterate rank lists to create pools. Store the created pool information
        # as a dictionary of pool UUID keys with a service replica list value.
        self.pool = []
        expected_uuids = {}
        for rank_list in rank_lists:
            self.pool.append(self.get_pool(create=False))
            self.pool[-1].target_list.update(rank_list)
            self.pool[-1].svcn.update(sr)
            self.pool[-1].create()
            expected_uuids[self.pool[-1].uuid.lower()] = self.pool[-1].svc_ranks

        # Verify the 'dmg pool info' command lists the correct created pool
        # information.  The DmgCommand.pool_info() method returns the command
        # output as a dictionary of pool UUID keys with service replica list
        # values.
        detected_uuids = {}
        try:
            for data in self.get_dmg_command().get_pool_list_all():
                detected_uuids[data["uuid"]] = data["svc_reps"]
        except KeyError as error:
            self.fail("Error parsing dmg pool list output: {}".format(error))

        self.log.info("Expected pool info: %s", str(expected_uuids))
        self.log.info("Detected pool info: %s", str(detected_uuids))

        # Destroy all the pools
        if self.destroy_pools(self.pool):
            self.fail("Error destroying pools")
        self.pool = []

        # Compare the expected and detected pool information
        self.assertEqual(
            expected_uuids, detected_uuids,
            "dmg pool info does not list all expected pool UUIDs and their "
            "service replicas")

    def test_list_pools(self):
        """JIRA ID: DAOS-3459.

        Test Description:
            Create pools in different ranks, call dmg pool list and verify the
            output list matches the output returned when the pools were
            created.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=pool
        :avocado: tags=list_pools,test_list_pools
        """
        ranks = list(range(len(self.hostlist_servers)))

        # Create pools with different ranks:
        #   1) Create 4 pools each using a different rank number
        #   2) Create 2 pools using 2 ranks per pool
        #   3) Create 4 pools each using the same 4 ranks for each pool
        #   4) Create 3 pools using all ranks with --svcn=3
        test_cases = [
            (
                "Create 4 pools each using a different rank number",
                {"rank_lists": [[rank] for rank in ranks[:4]]}
            ),
            (
                "Create 2 pools using 2 ranks per pool",
                {
                    "rank_lists":
                        [ranks[index:index + 2]
                         for index in range(0, len(ranks[:4]), 2)]
                }
            ),
            (
                "Create 4 pools each using the same 4 ranks for each pool",
                {"rank_lists": [ranks[:4] for _ in ranks[:4]]}
            ),
            (
                "Create 3 pools using all ranks with --nsvc=3",
                {"rank_lists": [None for _ in ranks[:3]], "sr": 3}
            ),
        ]
        errors = []
        for test_case, kwargs in test_cases:
            self.log.info("%s", "-" * 80)
            self.log.info("Running test case: %s", test_case)
            self.log.info("%s", "-" * 80)
            try:
                self.run_case(**kwargs)

            except TestFail as error:
                message = "Error: {}: {}".format(test_case, error)
                self.log.info(message)
                errors.append(message)

            except CommandFailure as error:
                self.fail(
                    "Fatal test error detected during: {}: {}".format(
                        test_case, error))
        if errors:
            self.fail("Errors detected:\n  {}".format("\n  ".join(errors)))
