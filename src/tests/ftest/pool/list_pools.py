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
    """Test class for list pools test.

    Test Class Description:
        This class contains tests for list pool.

    :avocado: recursive
    """

    def compare_pool_info(self, expected_uuids, expected_service_replicas):
        """Compare expected and actual pool UUIDs and service replicas.

        Expected is from pool create output and actual is pool list output.

        Sample output
        wolf-3:10001: connected
        Pool UUID                            Svc Replicas
        ---------                            ------------
        b4a27b5b-688a-4d1e-8c38-363e32eb4f29 1,2,3
        d7e7128f-14fb-4c6f-9c0b-13974fc17274 1,2,3
        0123270f-4cbf-4a0f-a446-5239d4a57bcd 1,2,3

        Args:
            expected_uuids (List of string): UUIDs of all pools created.
            expected_service_replicas (List of list of string): Service
                replicas of all pools. Each pool may have multiple service
                replicas, so it's a list of list.
        """
        stdoutput = self.get_dmg_command().pool_list().stdout
        all_pools = stdoutput.splitlines()[3:]
        actual_uuids = []
        actual_service_replicas = []
        for pool_info in all_pools:
            pool_info_list = pool_info.split()
            actual_uuids.append(pool_info_list[0])
            actual_service_replicas.append(pool_info_list[1].split(","))
        # Create a map of UUID to service replicas for expected and actual and
        # compare.
        actual_map = {}
        for i, actual_uuid in enumerate(actual_uuids):
            actual_map[actual_uuid] = actual_service_replicas[i]
        expected_map = {}
        for i, expected_uuid in enumerate(expected_uuids):
            expected_map[expected_uuid] = expected_service_replicas[i]
        self.assertEqual(actual_map, expected_map)

    def test_list_pools(self):
        """JIRA ID: DAOS-3459

        Test Description:
            Create pools in different ranks, call dmg pool list and verify the
            output list matches the output returned when the pools were
            created.

        :avocado: tags=all,pool,full_regression,list_pools
        """
        # Create 1 pool in each of the 4 hosts
        # Expected -> pool create output
        # Actual -> pool list output
        expected_uuids = []
        expected_service_replicas = []
        for i in range(4):
            stdoutput = self.get_dmg_command().pool_create(
                scm_size="1G", target_list=[i]).stdout
            uuid, service_replicas = \
                get_pool_uuid_service_replicas_from_stdout(stdoutput)
            expected_uuids.append(uuid)
            expected_service_replicas.append(service_replicas.split(","))
        self.compare_pool_info(expected_uuids, expected_service_replicas)

        # Destroy all the pools
        for uuid in expected_uuids:
            self.get_dmg_command().pool_destroy(uuid)

        # Create 1 pool over the first 2 hosts and 1 pool over the second 2
        # hosts
        expected_uuids = []
        expected_service_replicas = []
        rank_list = [[0, 1], [2, 3]]
        for ranks in rank_list:
            stdoutput = self.get_dmg_command().pool_create(
                scm_size="1G", target_list=ranks).stdout
            uuid, service_replicas = \
                get_pool_uuid_service_replicas_from_stdout(stdoutput)
            expected_uuids.append(uuid)
            expected_service_replicas.append(service_replicas.split(","))
        self.compare_pool_info(expected_uuids, expected_service_replicas)
        for uuid in expected_uuids:
            self.get_dmg_command().pool_destroy(uuid)

        # Create 4 pools over 4 hosts
        expected_uuids = []
        expected_service_replicas = []
        for _ in range(4):
            stdoutput = self.get_dmg_command().pool_create(
                scm_size="1G", target_list=[0, 1, 2, 3]).stdout
            uuid, service_replicas = \
                get_pool_uuid_service_replicas_from_stdout(stdoutput)
            expected_uuids.append(uuid)
            expected_service_replicas.append(service_replicas.split(","))
        self.compare_pool_info(expected_uuids, expected_service_replicas)
        for uuid in expected_uuids:
            self.get_dmg_command().pool_destroy(uuid)

        # Create 3 pools over all 6 hosts with --svcn=3. The purpose is to test
        # multiple service replicas. We need 6 hosts. If it's less than 6, we
        # would only get one service replica for a pool.
        expected_uuids = []
        expected_service_replicas = []
        for _ in range(3):
            stdoutput = self.get_dmg_command().pool_create(
                scm_size="1G", svcn="3").stdout
            uuid, service_replicas = \
                get_pool_uuid_service_replicas_from_stdout(stdoutput)
            expected_uuids.append(uuid)
            expected_service_replicas.append(service_replicas.split(","))
        self.compare_pool_info(expected_uuids, expected_service_replicas)
