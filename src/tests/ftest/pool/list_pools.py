"""
  (C) Copyright 2018-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from avocado.core.exceptions import TestFail
from exception_utils import CommandFailure


class ListPoolsTest(TestWithServers):
    """Test class for dmg and daos pool list tests.

    Test Class Description:
        This class contains tests for list pool.

    :avocado: recursive
    """

    def run_case(self, rank_lists, svcn=None):
        """Run test case.

        Create pools, then verify that both dmg and daos tools are able to
        list the pools with the same information that was returned when the
        pools were created.

        Args:
            rank_lists (list): Rank lists. List of list of int.
            svcn (str, optional): Service replicas. Defaults to None.

        Raises:
            CommandFailure: if there was an error destroying pools
            TestFail: if there was an error verifying the created pools

        """
        # Iterate rank lists to create pools. Store the created pool information
        # as a dictionary of pool UUID keys with a service replica list value.
        self.pool = []
        expected_admin_uuids = {}
        for rank_list in rank_lists:
            self.pool.append(self.get_pool(target_list=rank_list, svcn=svcn))
            expected_admin_uuids[self.pool[-1].uuid.lower()] = self.pool[-1].svc_ranks

        # Remove the default ACLs that allow the creator to access the pool.
        # These ACLs don't apply to dmg, but do apply to users.
        offlimits = self.pool[0]
        offlimits.delete_acl('OWNER@')
        offlimits.delete_acl('GROUP@')
        expected_user_uuids = expected_admin_uuids.copy()
        del expected_user_uuids[offlimits.uuid.lower()]

        # Verify the 'dmg pool list' command lists the correct created pool
        # information.
        detected_admin_uuids = {}
        try:
            for data in self.get_dmg_command().get_pool_list_all():
                detected_admin_uuids[data["uuid"]] = data["svc_reps"]
        except KeyError as error:
            self.fail("Error parsing dmg pool list output: {}".format(error))

        self.log.info("Expected admin pool info: %s", str(expected_admin_uuids))
        self.log.info("Detected admin pool info: %s", str(detected_admin_uuids))

        # Compare the expected and detected pool information
        self.assertEqual(
            expected_admin_uuids, detected_admin_uuids,
            "dmg pool info does not list all expected pool UUIDs and their "
            "service replicas")

        # Verify the 'daos pool list' command lists the correct created pool
        # information.
        detected_user_uuids = {}
        try:
            for data in self.get_daos_command().get_pool_list_all():
                detected_user_uuids[data["uuid"]] = data["svc_reps"]
        except KeyError as error:
            self.fail("Error parsing dmg pool list output: {}".format(error))

        self.log.info("Expected user pool info: %s", str(expected_user_uuids))
        self.log.info("Detected user pool info: %s", str(detected_user_uuids))

        # Compare the expected and detected pool information
        self.assertEqual(
            expected_user_uuids, detected_user_uuids,
            "daos pool info does not list all expected pool UUIDs and their "
            "service replicas")

        # Destroy all the pools
        if self.destroy_pools(self.pool):
            self.fail("Error destroying pools")
        self.pool = []

    def test_list_pools(self):
        """JIRA ID: DAOS-3459, DAOS-16038.

        Test Description:
            Create pools in different ranks, then verify that both dmg and
            daos tools are able to list the pools and that the listed output
            matches what was returned when the pools were created.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=pool,daos_cmd
        :avocado: tags=ListPoolsTest,test_list_pools
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
                {"rank_lists": [None for _ in ranks[:3]], "svcn": 3}
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
