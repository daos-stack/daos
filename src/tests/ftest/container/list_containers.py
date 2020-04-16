#!/usr/bin/python
'''
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
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
'''
from apricot import TestWithServers
from dmg_utils import get_pool_uuid_service_replicas_from_stdout
from daos_utils import DaosCommand


class ListContainerTest(TestWithServers):
    """
    Test Class Description:
        Tests daos pool list-cont. Test the following cases.
        1. Create 1 container, list, and verify the listed container UUID
            matches the UUID returned when created.
        2. Create 1 more container and verify list; 2 total containers
        3. Create 98 more containers and verify list; 100 total containers
        4. Create 2 additional pools and create 10 containers in each pool.
            Verify the container UUIDs in these 2 new pools.

    :avocado: recursive
    """

    def create_list(self, count, pool_uuid, sr, expected_uuids):
        """Create container and call daos pool list-cont to list and verify.

        Args:
            count (Integer): Number of containers to create.
            pool_uuid (String): Pool UUID to create containers in.
            sr (String): Service replicas of the pool to create containers.
            expected_uuids (List of string): List that contains container UUID.
                It should contain all the container UUIDs for the given pool.
        """
        # Create containers and store the container UUIDs into expected_uuids.
        for _ in range(count):
            create_stdout = self.daos_cmd.container_create(
                pool=pool_uuid, svc=sr).stdout
            expected_uuids.append(create_stdout.split()[3])
        expected_uuids.sort()
        # daos pool list-cont returns the date, host name, and container UUID
        # as below:
        # 03/31-21:32:24.53 wolf-3 2f69b198-8478-472e-b6c8-02a451f4de1b
        kwargs = {"pool": pool_uuid, "svc": sr}
        actual_uuids = self.daos_cmd.get_output("pool_list_cont", **kwargs)
        actual_uuids.sort()
        self.assertEqual(expected_uuids, actual_uuids)

    def test_list_container(self):
        """Jira ID: DAOS-3629

        Test Description:
            Test daos pool list-cont

        Use Cases:
            See test cases in the class description.

        :avocado: tags=all,container,full_regression,list_containers
        """
        expected_uuids1 = []
        stdoutput = self.get_dmg_command().pool_create(scm_size="150MB").stdout
        uuid1, service_replicas1 = \
            get_pool_uuid_service_replicas_from_stdout(stdoutput)
        self.daos_cmd = DaosCommand(self.bin)

        # 1. Create 1 container and list.
        self.create_list(1, uuid1, service_replicas1, expected_uuids1)

        # 2. Create 1 more container and list; 2 total.
        self.create_list(1, uuid1, service_replicas1, expected_uuids1)

        # 3. Create 98 more containers and list; 100 total.
        self.create_list(98, uuid1, service_replicas1, expected_uuids1)

        # 4. Create 2 additional pools and create 10 containers in each pool.
        stdoutput = self.get_dmg_command().pool_create(scm_size="150MB").stdout
        uuid2, service_replicas2 = \
            get_pool_uuid_service_replicas_from_stdout(stdoutput)
        stdoutput = self.get_dmg_command().pool_create(scm_size="150MB").stdout
        uuid3, service_replicas3 = \
            get_pool_uuid_service_replicas_from_stdout(stdoutput)
        # Create 10 containers in pool 2 and verify.
        expected_uuids2 = []
        self.create_list(10, uuid2, service_replicas2, expected_uuids2)
        # Create 10 containers in pool 3 and verify.
        expected_uuids3 = []
        self.create_list(10, uuid3, service_replicas3, expected_uuids3)
