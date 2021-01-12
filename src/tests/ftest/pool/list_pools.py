#!/usr/bin/python
"""
  (C) Copyright 2018-2020 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Governments rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
from apricot import TestWithServers
from avocado.core.exceptions import TestFail
from command_utils_base import CommandFailure


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
            rank_lists (List of list of integer): Rank lists.
            sr (String, optional): Service replicas. Defaults to None.

        Raises:
            CommandFailure: if there was an error destoying pools
            TestFail: if there was an error verifying the created pools

        """
        # Iterate rank lists to create pools. Store the created pool information
        # as a dictionary of pool UUID keys with a service replica list value.
        expected_uuids = {}
        for rank_list in rank_lists:
            data = self.get_dmg_command().pool_create(
                scm_size="1G", target_list=rank_list, svcn=sr)
            expected_uuids[data["uuid"]] = [
                int(svc) for svc in data["svc"].split(",")]

        # Verify the 'dmg pool info' command lists the correct created pool
        # information.  The DmgCommand.pool_info() method returns the command
        # output as a dictionary of pool UUID keys with service replica list
        # values.
        detected_uuids = self.get_dmg_command().pool_list()
        self.log.info("Expected pool info: %s", str(expected_uuids))
        self.log.info("Detected pool info: %s", str(detected_uuids))

        # Destroy all the pools
        for uuid in expected_uuids:
            self.get_dmg_command().pool_destroy(uuid)

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

        :avocado: tags=all,large,pool,full_regression,list_pools
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
                "Create 3 pools using all ranks with --svcn=3",
                {"rank_lists": [None for _ in ranks], "sr": 3}
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
