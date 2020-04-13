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
from dmg_utils import get_pool_uuid_service_replicas_from_stdout


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
        """
        expected_uuids = []
        expected_service_replicas = []
        # Iterate rank lists to create pools. Store the returned UUID and
        # service replicas in the expected lists.
        for rank_list in rank_lists:
            stdoutput = self.get_dmg_command().pool_create(
                scm_size="1G", target_list=rank_list, svcn=sr).stdout
            uuid, service_replicas = \
                get_pool_uuid_service_replicas_from_stdout(stdoutput)
            expected_uuids.append(uuid)
            expected_service_replicas.append(service_replicas.split(","))

        # Sample dmg pool list output.
        # wolf-3:10001: connected
        # Pool UUID                            Svc Replicas
        # ---------                            ------------
        # b4a27b5b-688a-4d1e-8c38-363e32eb4f29 1,2,3
        # d7e7128f-14fb-4c6f-9c0b-13974fc17274 1,2,3
        # 0123270f-4cbf-4a0f-a446-5239d4a57bcd 1,2,3

        # Call dmg pool list. get_output will parse the stdout with the regex
        # defined in dmg_utils.py and return the output as list of tuple. First
        # element is UUID and second is service replicas.
        output_list = self.get_dmg_command().get_output("pool_list")
        # Create a map of UUID to service replicas for expected and actual and
        # compare.
        actual_map = {}
        for uuid_sr in output_list:
            actual_map[uuid_sr[0]] = uuid_sr[1].split(",").sort()
        expected_map = {}
        for i, expected_uuid in enumerate(expected_uuids):
            expected_map[expected_uuid] = expected_service_replicas[i].sort()
        self.assertEqual(actual_map, expected_map)
        # Destroy all the pools
        for uuid in expected_uuids:
            self.get_dmg_command().pool_destroy(uuid)

    def test_list_pools(self):
        """JIRA ID: DAOS-3459

        Test Description:
            Create pools in different ranks, call dmg pool list and verify the
            output list matches the output returned when the pools were
            created.

        :avocado: tags=all,large,pool,full_regression,list_pools
        """
        # Create 1 pool in each of the 4 hosts.
        self.run_case(rank_lists=[[0], [1], [2], [3]])
        # Create 1 pool over the first 2 hosts and 1 pool over the second 2
        # hosts.
        self.run_case(rank_lists=[[0, 1], [2, 3]])
        # Create 4 pools over 4 hosts.
        self.run_case(
            rank_lists=[[0, 1, 2, 3], [0, 1, 2, 3], [0, 1, 2, 3],
                        [0, 1, 2 ,3]])
        # Create 3 pools over all 6 hosts with --svcn=3. The purpose is to test
        # multiple service replicas. We need 6 hosts. If it's less than 6, we
        # would only get one service replica for a pool.
        self.run_case(rank_lists=[None, None, None], sr="3")
