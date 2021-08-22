#!/usr/bin/python3
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from command_utils_base import CommandFailure


class ListPoolsTest(TestWithServers):
    """Test class for dmg pool list tests.

    Test Class Description:
        This class contains tests for list pool.

    :avocado: recursive
    """
    def __init__(self, *args, **kwargs):
        """Inititialize a ListPoolsTest object."""
        super().__init__(*args, **kwargs)
        self.pool = []

    def run_case(self, rank_lists, sr=None):
        """Run test case.

        Create pools, call dmg pool list to get the list, and compare against
        the UUIDs and service replicas returned at create time.

        Args:
            rank_lists (list): Rank lists. List of list of int.
            sr (str, optional): Service replicas. Defaults to None.

        Raises:
            CommandFailure: if there was an error destoying pools

        Returns:
            list: Error list.

        """
        expected_data = {}

        # Iterate rank lists to create pools. Store the created pool
        # information.
        for rank_list in rank_lists:
            self.pool.append(self.get_pool(create=False))
            self.pool[-1].target_list.update(rank_list)
            self.pool[-1].svcn.update(sr)
            self.pool[-1].create()

            # Key is UUID and value is a dictionary that follows the same
            # pattern as the JSON output.
            expected_data[self.pool[-1].uuid.lower()] = {
                "label": self.pool[-1].label.value,
                "svc_reps": self.pool[-1].svc_ranks
            }

        # Call dmg pool list.
        actual_pools = self.pool[-1].dmg.get_pool_list_all()

        errors = []

        # Verify the number of pools.
        expected_count = 0
        for _ in expected_data:
            expected_count += 1

        actual_count = 0
        for _ in actual_pools:
            actual_count += 1

        if expected_count != actual_count:
            msg = ("Unexpected number of pools! Expected = {}; "
                   "Actual = {}".format(expected_count, actual_count))
            self.log.error(msg)
            errors.append(msg)

        # Verify UUID, label, and svc_reps.
        for actual_pool in actual_pools:
            actual_uuid = actual_pool["uuid"]

            if actual_uuid in expected_data:
                if actual_pool["label"] != expected_data[actual_uuid]["label"]:
                    msg = ("Unexpected label! Expected = {}; "
                           "Actual = {}".format(
                               expected_data[actual_uuid]["label"],
                               actual_pool["label"]))
                    self.log.error(msg)
                    errors.append(msg)

                if actual_pool["svc_reps"] != expected_data[
                    actual_uuid]["svc_reps"]:
                    msg = ("Unexpected svc_reps! Expected = {}; "
                           "Actual = {}".format(
                               expected_data[actual_uuid]["svc_reps"],
                               actual_pool["svc_reps"]))
                    self.log.error(msg)
                    errors.append(msg)
            else:
                msg = ("Unexpected UUID returned from dmg pool list! "
                       "{}".format(actual_uuid))
                self.log.error(msg)
                errors.append(msg)

        # Destroy all the pools to prepare for the next test case.
        for pool in self.pool:
            pool.destroy()

        return errors

    def test_list_pools(self):
        """JIRA ID: DAOS-3459.

        Test Description:
            Create pools in different ranks, call dmg pool list and verify the
            output list matches the output returned when the pools were
            created.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=pool,list_pools
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
            )
        ]

        errors = []

        for test_case, kwargs in test_cases:
            self.log.info("%s", "-" * 80)
            self.log.info("Running test case: %s", test_case)
            self.log.info("%s", "-" * 80)
            try:
                errors.extend(self.run_case(**kwargs))
            except CommandFailure as error:
                self.fail(
                    "Fatal test error detected during: {}: {}".format(
                        test_case, error))
        if errors:
            self.fail("Errors detected:\n  {}".format("\n  ".join(errors)))
